#include "CorePch.h"
#include "Network/Session.h"
#include "Network/ServerService.h"
#include "Network/NetStats.h"

Session::Session() : _recvBuffer(65536)
{
	_socket = ::WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0, WSA_FLAG_OVERLAPPED);
}

Session::~Session()
{
	if (_socket != INVALID_SOCKET)
		::closesocket(_socket);

	cout << "~Session[" << _cid << "]" << endl;

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
		cout << "Undefined Event" << endl;
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
	
	wcout << "DisConnect :" << cause << endl;
}


void Session::Send(shared_ptr<SendBuffer> sendBuffer, bool pushOnly)
{
	if (false == IsConnected())
		return;

	NetStats::MessagesOut.fetch_add(1);
	_sendQueue.Push(sendBuffer);

	if (pushOnly) return;

	bool expected = false;
	if (_sendRegistered.compare_exchange_strong(expected, true)) {
		RegisterSend();
	}

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

	vector<WSABUF> wsaBufs;
	wsaBufs.reserve(_sendEvent.sendBuffers.size());
	for (shared_ptr<SendBuffer> sendBuffer : _sendEvent.sendBuffers)
	{
		WSABUF wsaBuf;
		wsaBuf.buf = reinterpret_cast<char*>(sendBuffer->Buffer());
		wsaBuf.len = static_cast<LONG>(sendBuffer->WritePos());
		wsaBufs.push_back(wsaBuf);
	}


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
		bool expected = false;
		if (_sendRegistered.compare_exchange_strong(expected, true))
			RegisterSend();
	}
}


