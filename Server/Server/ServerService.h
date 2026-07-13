#pragma once
#include "Listener.h"
#include "IocpCore.h"
#include "Session.h"
#include "NetAddress.h"
#include "Room.h"

class ServerService : public enable_shared_from_this<ServerService>
{
public:
	ServerService(NetAddress address, shared_ptr<IocpCore> iocpCore);
	~ServerService();

	bool Start();

	shared_ptr<IocpCore>& GetIocpInstance() { return _iocpCore; }
	NetAddress	GetNetAddress() { return _netAddress; }

	shared_ptr<Session>	CreateSession();
	void			AddSession(shared_ptr<Session> session);
	void			ReleaseSession(shared_ptr<Session> session);

private:
	RWLock					_lock;
	

	shared_ptr<Listener>		_listener;
	NetAddress				_netAddress;
	shared_ptr<IocpCore>		_iocpCore;
	
	// vector<shared_ptr<Session>> _sessions;
	unordered_map<int, shared_ptr<Session>> _sessions;

};

