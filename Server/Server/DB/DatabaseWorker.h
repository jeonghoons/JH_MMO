#pragma once
#include "Data/ServerData.h"
#include "DB/DBConnectionPool.h"
#include <concurrent_queue.h>
#include "Utils/ConcurrentQueue.h"
#include "Job/Job.h"

struct UserAccount
{
	unsigned int uniqueId;
	WCHAR accountPassword[20];
	WCHAR accountId[20];
};

class DatabaseWorker : public enable_shared_from_this<DatabaseWorker>
{
public:
	DatabaseWorker(HANDLE iocpHandle, int num_connections, const wstring& connectionString);
	~DatabaseWorker();
	
public:
	void Run();

	template<typename... Arguments>
	void PushDBJob(void(DatabaseWorker::* memFunc)(Arguments...), Arguments... args)
	{
		shared_ptr<Job> job = make_shared<Job>(shared_from_this(), memFunc, std::forward<Arguments>(args)...);
		_dbJobQueue.Push(job);
	}

	void TryLogin(shared_ptr<GameSession> session, string recvId, string recvPw);
	void TrySignUP(shared_ptr<GameSession> session, string recvId, string recvPw, int playerType);


	// 장비 6칸 스냅숏을 한 트랜잭션으로 저장한다(DELETE 후 빈 칸을 뺀 INSERT). 끝나면 room 잡(Room::OnEquipSaved)으로 돌아간다.
	// 플레이어당 동시에 하나만 보내므로(Room::RequestEquipSave) 저장 순서가 뒤바뀌지 않는다.
	void SaveEquipSnapshot(shared_ptr<class Room> room, shared_ptr<class Player> player, int64_t playerUID, vector<int32_t> items, long long requestUs);

private:
	ConcurrentQ<shared_ptr<Job>> _dbJobQueue;
	vector<thread> _threads;
	unique_ptr<DBConnectionPool> _dbConnectionPool;

	HANDLE						_iocpHandle;
};

