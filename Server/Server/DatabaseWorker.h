#pragma once
#include "ServerData.h"
#include "DBConnectionPool.h"
#include <concurrent_queue.h>
#include "ConcurrentQueue.h"
#include "Job.h"

struct UserAccount
{
	unsigned int uniqueId;
	WCHAR accountPassword[20];
	WCHAR accountId[20];
};

class DatabaseWorker : public enable_shared_from_this<DatabaseWorker>
{
public:
	DatabaseWorker(HANDLE iocpHandle, int num_connections);
	~DatabaseWorker();
	
public:
	void Run();

	template<typename... Arguments>
	void PushDBJob(void(DatabaseWorker::* memFunc)(Arguments...), Arguments... args)
	{
		shared_ptr<Job> job = make_shared<Job>(shared_from_this(), memFunc, std::forward<Arguments>(args)...);
		_dbJobQueue.Push(job);
	}

	void TryLogin(shared_ptr<Session> session, string recvId, string recvPw);
	void TrySignUP(shared_ptr<Session> session, string recvId, string recvPw, int playerType);

	void SavePlayerData(DB_PlayerData data);

private:
	ConcurrentQ<shared_ptr<Job>> _dbJobQueue;
	vector<thread> _threads;
	unique_ptr<DBConnectionPool> _dbConnectionPool;

	HANDLE						_iocpHandle;
};

