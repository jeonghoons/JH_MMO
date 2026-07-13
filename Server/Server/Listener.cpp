#include "pch.h"
#include "Listener.h"
#include "ServerService.h"
#include "IocpEvent.h"
#include "Session.h"
#include "NetAddress.h"

Listener::~Listener()
{
	::closesocket(_listenSocket);
	delete _acceptOver;
}

HANDLE Listener::GetHandle()
{
	return reinterpret_cast<HANDLE>(_listenSocket);
}

void Listener::Dispatch(class IocpEvent* iocpEvent, int numBytes)
{
	if (iocpEvent->_type != EventType::Accept)
		exit(-1);

	AcceptEvent* accpetEvent = static_cast<AcceptEvent*>(iocpEvent);
	ProcessAccept(accpetEvent);
}

bool Listener::StartAccept(shared_ptr<ServerService> service)
{
	_service = service;
	if (_service == nullptr)
		return false;

	_listenSocket = WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);
	if (_listenSocket == INVALID_SOCKET)
		return false;

	if (_service->GetIocpInstance()->Register(shared_from_this()) == false)
		return false;
	
	if (SOCKET_ERROR == ::bind(_listenSocket, reinterpret_cast<SOCKADDR*>(&_service->GetNetAddress().GetSockAddr()), sizeof(SOCKADDR)))
		return false;

	if (SOCKET_ERROR == ::listen(_listenSocket, SOMAXCONN))
		return false;

	_acceptOver = new AcceptEvent();
	_acceptOver->_owner = shared_from_this();
	RegisterAccept(_acceptOver);

	return true;
}

void Listener::RegisterAccept(AcceptEvent* acceptEvent)
{
	shared_ptr<Session> session = _service->CreateSession();
	_acceptOver->Init();
	_acceptOver->_session = session;

	DWORD num_bytes{};
	if (false == AcceptEx(_listenSocket, session->GetSocket(), session->_recvBuffer.WritePos(), 0,
		sizeof(SOCKADDR_IN) + 16, sizeof(SOCKADDR_IN) + 16, &num_bytes, static_cast<LPOVERLAPPED>(acceptEvent)))
	{
		const int errorCode = ::WSAGetLastError();
		if (errorCode != WSA_IO_PENDING)
		{
			RegisterAccept(_acceptOver);
		}
	}

}

void Listener::ProcessAccept(AcceptEvent* acceptEvent)
{
	shared_ptr<Session> session = acceptEvent->_session;

	::setsockopt(session->GetSocket(), SOL_SOCKET, SO_UPDATE_ACCEPT_CONTEXT,
		(char*)&_listenSocket, sizeof(_listenSocket));

	SOCKADDR_IN sockAddress;
	int sizeOfSockAddr = sizeof(sockAddress);
	if (SOCKET_ERROR == ::getpeername(session->GetSocket(), reinterpret_cast<SOCKADDR*>(&sockAddress), &sizeOfSockAddr))
	{
		RegisterAccept(acceptEvent);
		return;
	}

	session->SetNetAddress(NetAddress(sockAddress));
	int newId = IdGenerate();
	session->SetId(newId);
	session->OnConnected();

	

	RegisterAccept(acceptEvent);
}
