#include "CorePch.h"
#include "Network/Session.h"
#include "Network/ServerService.h"
#include "Network/NetStats.h"
#include "Job/JobQueue.h"

Session::Session() : _recvBuffer(65536)
{
	_socket = ::WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0, WSA_FLAG_OVERLAPPED);
}

Session::~Session()
{
	if (_socket != INVALID_SOCKET)
		::closesocket(_socket);

	LOG_DEBUG("Session", "~Session[{}]", _cid);

	// 보내지 못하고 남은 패킷은 큐와 함께 사라지므로 전체 합에서 뺀다.
	NetStats::SendQueued.fetch_sub(_sendQueued.load());

	_socket = INVALID_SOCKET;
}

HANDLE Session::GetHandle()
{
	return reinterpret_cast<HANDLE>(_socket);
}

void Session::Dispatch(IocpEvent* iocpEvent, int numBytes)
{
	switch (iocpEvent->_type)
	{
	case EventType::Recv:
		ProcessRecv(numBytes);
		break;
	case EventType::Send:
		ProcessSend(numBytes);
		break;

	default:
		LOG_ERROR("Session", "Undefined Event");
		break;
	}
}

void Session::Connected()
{
	_connected.store(true);
	NetStats::Sessions.fetch_add(1);

	GetService()->AddSession(static_pointer_cast<Session>(shared_from_this()));

	OnConnected();
	RegisterRecv();
}

void Session::Disconnect(const WCHAR* cause)
{
	if (_connected.exchange(false) == false)
		return;

	NetStats::Sessions.fetch_sub(1);

	OnDisconnected();
	GetService()->ReleaseSession(static_pointer_cast<Session>(shared_from_this()));
	
	LOG_DEBUG("Session", "DisConnect :{}", Utils::WStringToUTF8(cause));
}

void Session::ForceClose(const WCHAR* cause)
{
	// 직전에 Send한 패킷(SC_KICK 등)이 송신 묶기로 큐에만 있을 수 있다. 끊기 전에 전송을 건다.
	TrySendNow();

	Disconnect(cause);

	// 핸들은 소멸자에서 닫는다(다른 스레드가 같은 핸들로 WSARecv/WSASend를 부르는 중일 수 있어 여기서 closesocket하지 않는다).
	// shutdown 뒤의 WSARecv는 실패하고, 이미 걸려 있던 수신은 CancelIoEx로 끝나 세션 참조가 풀린다.
	::shutdown(_socket, SD_BOTH);
	::CancelIoEx(reinterpret_cast<HANDLE>(_socket), nullptr);
}


void Session::Send(shared_ptr<SendBuffer> sendBuffer)
{
	if (false == IsConnected())
		return;

	NetStats::MessagesOut.fetch_add(1);
	_sendQueue.Push(sendBuffer);
	NetStats::SendQueued.fetch_add(1);
	const int depth = _sendQueued.fetch_add(1) + 1;
	int prevMax = NetStats::SendQueueMax.load();
	while (depth > prevMax && !NetStats::SendQueueMax.compare_exchange_weak(prevMax, depth)) {}

	// 잡 큐의 작업 안에서 보낸 것은 그 큐가 모아서 보낸다. 큐에 넣은 뒤에 등록하므로, 다른 스레드의 FlushSend가
	// 플래그를 내리는 것과 겹쳐도 이 패킷은 그 송신에 실리거나(플래그가 아직 켜져 있었음) 여기서 새로 등록된다.
	if (JobQueue* queue = JobQueue::Current())
	{
		if (_flushPending.exchange(true) == false)
			queue->AddPendingSend(static_pointer_cast<Session>(shared_from_this()));
		return;
	}

	NetStats::SendDirect.fetch_add(1, memory_order_relaxed);
	TrySendNow();
}

void Session::FlushSend(long long nowMs, bool sendNow)
{
	// 큐를 보기 전에 플래그를 내린다 — 이 뒤에 Send된 패킷은 다시 등록된다.
	// 시각을 먼저 적는 것은 다시 등록하는 쪽이 새 시각으로 보낼 때를 계산하게 하려는 것.
	const long long prevMs = _lastFlushMs.exchange(nowMs, memory_order_relaxed);
	_flushPending.store(false);

	NetStats::SendFlushes.fetch_add(1, memory_order_relaxed);
	if (prevMs != 0)
		NetStats::AddMs(NetStats::FlushGapMs, nowMs - prevMs);

	if (sendNow)
		TrySendNow();
}

void Session::TrySendNow()
{
	if (_sendQueue.isEmpty())
		return;

	// 이미 송신 중이면 ProcessSend의 재확인이 큐에 남은 것을 이어서 보낸다.
	bool expected = false;
	if (_sendRegistered.compare_exchange_strong(expected, true))
		RegisterSend();
}

int Session::ProcessData(BYTE* buffer, int len)
{
	int processLen = 0;

	while (true)
	{
		int dataSize = len - processLen;
		if (dataSize < sizeof(PacketHeader))
			break;

		PacketHeader header = *(reinterpret_cast<PacketHeader*>(&buffer[processLen]));
		if (header.size < sizeof(PacketHeader) || header.size > MAX_PACKET_SIZE)
			return -1;
		if (dataSize < header.size)
			break;

		NetStats::PacketsIn.fetch_add(1);
		OnRecvPacket(&buffer[processLen], header.size);

		processLen += header.size;

		// 핸들러가 Disconnect했으면(파싱 실패 등) 같은 수신 묶음의 남은 패킷은 버린다.
		// 계속 처리하면 끊긴 세션의 CS_ENTER_ROOM이 방에 들어가 유령 플레이어가 된다(감사 F1).
		if (false == IsConnected())
			break;
	}
	return processLen;
}

void Session::RegisterRecv()
{
	if (IsConnected() == false)
		return;

	_recvEvent.Init();
	_recvEvent._owner = shared_from_this();

	WSABUF wsaBuf;
	wsaBuf.buf = reinterpret_cast<char*>(_recvBuffer.WritePos());
	wsaBuf.len = _recvBuffer.FreeSize();

	DWORD numOfBytes = 0;
	DWORD flags = 0;
	if (SOCKET_ERROR == ::WSARecv(_socket, &wsaBuf, 1, &numOfBytes, &flags, &_recvEvent, nullptr))
	{
		int errorCode = ::WSAGetLastError();
		if (errorCode != WSA_IO_PENDING)
		{
			_recvEvent._owner = nullptr; // RELEASE_REF
			Disconnect(L"RegisterRecv Fail");
		}
	}


}

void Session::ProcessRecv(int numOfBytes)
{
	_recvEvent._owner = nullptr;

	if (numOfBytes == 0) {
		Disconnect(L"Recv 0");
		return;
	}

	if (_recvBuffer.OnWrite(numOfBytes) == false) {
		Disconnect(L"Recv Buffer Full");
		return;
	}

	int dataSize = _recvBuffer.DataSize();

	int processLen = ProcessData(_recvBuffer.ReadPos(), dataSize);
	if (processLen < 0) {
		Disconnect(L"Invalid Packet Size");
		return;
	}
	if (_recvBuffer.OnRead(processLen) == false)
	{
		Disconnect(L"Recv Buffer Overflow");
		return;
	}

	_recvBuffer.CleanCheck();

	RegisterRecv();
}

void Session::RegisterSend()
{
	if (IsConnected() == false) {
		return;
	}

	_sendEvent.Init();
	_sendEvent._owner = shared_from_this();

	shared_ptr<SendBuffer> buffer;
	while (_sendQueue.Try_pop(buffer)) {
		_sendEvent.sendBuffers.push_back(buffer);
	}
	const int popped = static_cast<int>(_sendEvent.sendBuffers.size());
	if (popped == 0)
	{
		// 큐를 본 뒤 플래그를 잡기 전에 다른 송신(ProcessSend의 재확인)이 먼저 다 가져갔다.
		// 버퍼 0개로 WSASend하면 0바이트 완료가 와서 ProcessSend가 "Send 0"으로 끊으므로 부르지 않는다.
		// 플래그를 내린 뒤 다시 확인하는 것은 ProcessSend와 같은 절차다.
		_sendEvent._owner = nullptr;
		_sendRegistered.store(false);
		TrySendNow();
		return;
	}
	_sendQueued.fetch_sub(popped);
	NetStats::SendQueued.fetch_sub(popped);

	vector<WSABUF> wsaBufs;
	wsaBufs.reserve(_sendEvent.sendBuffers.size());
	for (shared_ptr<SendBuffer> sendBuffer : _sendEvent.sendBuffers)
	{
		WSABUF wsaBuf;
		wsaBuf.buf = reinterpret_cast<char*>(sendBuffer->Buffer());
		wsaBuf.len = static_cast<LONG>(sendBuffer->WritePos());
		wsaBufs.push_back(wsaBuf);
	}


	NetStats::SendCalls.fetch_add(1);
	NetStats::SendBufs.fetch_add(static_cast<long long>(wsaBufs.size()));

	DWORD numOfBytes = 0;
	if (SOCKET_ERROR == ::WSASend(_socket, wsaBufs.data(), (DWORD)wsaBufs.size(), &numOfBytes, 0, &_sendEvent, nullptr))
	{
		int errorCode = ::WSAGetLastError();
		if (errorCode != WSA_IO_PENDING)
		{
			_sendEvent._owner = nullptr; // Release REF
			_sendEvent.sendBuffers.clear();
			_sendRegistered.store(false);
			Disconnect(L"RegisterSend Fail");
		}
	}
}

void Session::ProcessSend(int numOfBytes)
{
	_sendEvent._owner = nullptr;
	_sendEvent.sendBuffers.clear();
	

	if (numOfBytes == 0)
	{
		Disconnect(L"Send 0");
		return;
	}


	// 플래그를 내린 뒤 큐를 다시 확인한다. isEmpty 검사와 플래그 해제 사이에 Send된 패킷은
	// Send 쪽 CAS가 실패해서 등록되지 않으므로 여기서 이어서 보낸다.
	_sendRegistered.store(false);
	if (_sendQueue.isEmpty() == false)
	{
		// 큐에 남은 것이 잡 큐의 "보낼 세션 목록"에 올라 있으면(_flushPending) 그 큐가 송신 간격에 맞춰 보낸다 — 여기서 바로
		// 이어 보내면 간격을 건너뛰어 묶이지 않는다(P1b). 플래그가 켜져 있는 동안은 FlushSend가 반드시 한 번 더 불린다.
		// 플래그가 꺼져 있으면(FlushSend가 이미 지나갔거나 잡 큐 밖에서 보낸 것) 여기서 이어 보내야 남지 않는다.
		if (_flushPending.load())
			return;

		bool expected = false;
		if (_sendRegistered.compare_exchange_strong(expected, true))
		{
			NetStats::SendContinues.fetch_add(1, memory_order_relaxed);
			RegisterSend();
		}
	}
}


