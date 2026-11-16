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


	// 아이템 작업 하나를 한 트랜잭션으로 실행한다(A3.3, D-153). 끝나면 room 잡(Room::OnItemOpDone)으로 작업과 결과를 돌려준다.
	// 플레이어당 동시에 하나만 보내므로(Player::itemOp) 같은 플레이어의 작업 순서가 뒤바뀌지 않는다.
	void ExecItemOp(shared_ptr<class Room> room, shared_ptr<class Player> player, DB_ItemOp op);

	// 진행 상황(레벨·경험치·HP)을 저장한다(A2.5). 끝나면 room 잡(Room::OnProgressSaved)으로 저장한 값과 함께 돌아간다.
	// 플레이어당 동시에 하나만 보낸다(Room::RequestProgressSave).
	void SavePlayerProgress(shared_ptr<class Room> room, shared_ptr<class Player> player, int64_t playerUID, DB_PlayerProgress progress, long long requestUs);

private:
	// DB의 Schema_Version이 DbQueries::DB_SCHEMA_VERSION과 같은지 확인한다. 다르면 이유를 로그에 남기고 false.
	bool CheckSchemaVersion();

private:
	ConcurrentQ<shared_ptr<Job>> _dbJobQueue;
	vector<thread> _threads;
	unique_ptr<DBConnectionPool> _dbConnectionPool;

	HANDLE						_iocpHandle;
};

