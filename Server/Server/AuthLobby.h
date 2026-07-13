#pragma once
#include "ServerData.h"
#include "JobQueue.h"

class AuthLobby : public enable_shared_from_this<AuthLobby>
{
public:
	AuthLobby(HANDLE iocpHandle);
	~AuthLobby();

	template<typename... Arguments>
	void PushJob(void(AuthLobby::* memFunc)(Arguments...), Arguments... args)
	{
		_jobQueue->Push(shared_from_this(), memFunc, std::forward<Arguments>(args)...);
	}

public:
	void OnLoginSuccess(shared_ptr<Session> session, DB_PlayerInfo info, DB_PlayerData data);
	void OnLoginFailed(shared_ptr<Session> session, string errorMsg);

private:
	shared_ptr<JobQueue> _jobQueue;
};

