#include "CorePch.h"
#include "Network/ServerService.h"
#include "Network/NetStats.h"

ServerService::ServerService(NetAddress address, shared_ptr<IocpCore> iocpCore, SessionFactory factory)
	: _netAddress(address), _iocpCore(iocpCore), _sessionFactory(std::move(factory))
{
	WSADATA wsaData;
	if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
		LOG_ERROR("Network", "WSAStartup failed with error: {}", WSAGetLastError());
		exit(-1);
	}

}

ServerService::~ServerService()
{
}

bool ServerService::Start()
{
	_listener = make_shared<Listener>();
	if (_listener == nullptr)
		return false;

	shared_ptr<ServerService> service = static_pointer_cast<ServerService>(shared_from_this());
	if (_listener->StartAccept(service) == false)
		return false;

	return true;
}

shared_ptr<Session> ServerService::CreateSession()
{
	
	shared_ptr<Session> session = _sessionFactory();
	if (session == nullptr)
		return nullptr;
	session->SetService(shared_from_this());

	if (_iocpCore->Register(session) == false)
		return nullptr;

	return session;
}

void ServerService::AddSession(shared_ptr<Session> session)
{
	RWLock::WriteGuard lock(_lock);
	auto [it, success] = _sessions.emplace(session->GetId(), session);
	if (!success) {
		LOG_ERROR("Network", "AddSession: 이미 있는 세션 id {}", session->GetId());
	}
}

void ServerService::ReleaseSession(shared_ptr<Session> session)
{
	RWLock::WriteGuard lock(_lock);
	
	int sessionId = session->GetId();

	auto it = _sessions.find(sessionId);
	if (it == _sessions.end()) {
		LOG_ERROR("Network", "ReleaseSession: 없는 세션 id {}", sessionId);
	}
	else {
		_sessions.erase(it);
	}
}

int ServerService::CloseIdleSessions(long long timeoutMs)
{
	if (timeoutMs <= 0)
		return 0;

	// Disconnect가 ReleaseSession으로 쓰기 락을 잡으므로, 대상을 락 안에서 모으고 락 밖에서 끊는다.
	const long long nowMs = static_cast<long long>(::GetTickCount64());
	vector<shared_ptr<Session>> idle;
	{
		RWLock::ReadGuard lock(_lock);
		for (const auto& [id, session] : _sessions)
		{
			if (nowMs - session->LastRecvMs() >= timeoutMs)
				idle.push_back(session);
		}
	}

	int closed = 0;
	for (const shared_ptr<Session>& session : idle)
	{
		// 목록을 만든 뒤 다른 경로로 이미 끊긴 세션은 세지 않는다.
		if (session->IsConnected() == false)
			continue;
		++closed;
		NetStats::IdleKicks.fetch_add(1);
		LOG_DEBUG("Network", "유휴 타임아웃: 세션 {} ({}ms 동안 수신 없음)", session->GetId(), nowMs - session->LastRecvMs());
		session->Disconnect(L"Idle Timeout");
	}
	return closed;
}
