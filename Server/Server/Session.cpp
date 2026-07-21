#include "pch.h"
#include "Session.h"
#include "ServerService.h"
#include "PacketHandler.h"
#include "Room.h"
#include "Player.h"

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

void Session::OnConnected()
{
	_connected.store(true);
	// cout << "Client[" << _cid << "] Connected" << endl;

	GetService()->AddSession(static_pointer_cast<Session>(shared_from_this()));
	
	RegisterRecv();
}

void Session::Disconnect(const WCHAR* cause)
{
	if (_connected.exchange(false) == false)
		return;
	
	if (shared_ptr<Room> room = _currPlayer->GetCurrentRoom()) {
		room->PushJob(&Room::PlayerLeaveRoom, _currPlayer);
	}
	GetService()->ReleaseSession(static_pointer_cast<Session>(shared_from_this()));
	
	wcout << "DisConnect :" << cause << endl;
}


void Session::Send(shared_ptr<SendBuffer> sendBuffer, bool pushOnly)
{
	if (false == IsConnected())
		return;
	
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
		if (dataSize < header.size)
			break;

		// 패킷 조립 성공
		ProcessPacket(&buffer[processLen], header.size);

		processLen += header.size;

	}
	return processLen;
}

void Session::ProcessPacket(BYTE* buffer, int len)
{
	shared_ptr<Session> session = static_pointer_cast<Session>(shared_from_this());
	
	PacketHandler::ProcessPacket(session, buffer, len);
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
		return;
	}

	int dataSize = _recvBuffer.DataSize();

	int processLen = ProcessData(_recvBuffer.ReadPos(), dataSize);
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


	if (_sendQueue.isEmpty())
		_sendRegistered.store(false);
	else 
		RegisterSend();
}

void Session::ProcessDBEvent()
{
	// GRoomManager->EnterPlayer(static_pointer_cast<Session>(shared_from_this()));
}


