#pragma once
#include "Network/Listener.h"
#include "Network/IocpCore.h"
#include "Network/Session.h"
#include "Network/NetAddress.h"
#include <functional>

// 접속마다 세션 객체를 만드는 함수. 콘텐츠 쪽이 자기 Session 파생 클래스를 넘긴다.
using SessionFactory = std::function<shared_ptr<Session>()>;

class ServerService : public enable_shared_from_this<ServerService>
{
public:
	ServerService(NetAddress address, shared_ptr<IocpCore> iocpCore, SessionFactory factory);
	~ServerService();

	bool Start();

	shared_ptr<IocpCore>& GetIocpInstance() { return _iocpCore; }
	NetAddress	GetNetAddress() { return _netAddress; }

	shared_ptr<Session>	CreateSession();
	void			AddSession(shared_ptr<Session> session);
	void			ReleaseSession(shared_ptr<Session> session);

	// 마지막 수신 뒤 timeoutMs가 지난 세션을 끊는다(유휴 타임아웃, D-220). 끊은 수를 돌려준다. 어느 스레드에서 불러도 된다.
	int			CloseIdleSessions(long long timeoutMs);

private:
	RWLock					_lock;
	

	shared_ptr<Listener>		_listener;
	NetAddress				_netAddress;
	shared_ptr<IocpCore>		_iocpCore;
	SessionFactory			_sessionFactory;
	
	// vector<shared_ptr<Session>> _sessions;
	unordered_map<int, shared_ptr<Session>> _sessions;

};

