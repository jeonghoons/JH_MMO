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

	// ---- 경매장(A4.3) ----
	// 등록·구매·취소 한 건을 한 트랜잭션으로 실행한다. ExecItemOp와 같되 결과를 경매 서비스(AuctionService::OnOpDone)로 돌려준다
	// — 서비스가 목록을 갱신한 뒤 Room::OnItemOpDone으로 넘긴다.
	void ExecAuctionOp(shared_ptr<class Room> room, shared_ptr<class Player> player, DB_ItemOp op);
	// 기간이 지난 등록 묶음을 한 트랜잭션으로 만료 처리한다(아이템은 판매자 수령함으로). 결과는 AuctionService::OnExpireDone.
	void ExecAuctionExpire(vector<int64_t> listingIds);
	// 수령함을 오래된 것부터 limit건 읽는다(더 있는지 알려고 한 건 더 읽는다). 결과는 AuctionService::OnMailRead.
	void ReadMail(shared_ptr<GameSession> session, int64_t playerUID, int limit);
	// 등록 하나의 State를 읽는다(실패한 구매·취소·만료 뒤 목록을 DB에 맞출 때). 결과는 AuctionService::OnListingVerified.
	void ReadListingState(int64_t listingId);
	// 한 판매자의 판매 중인 등록을 읽는다(결과를 알 수 없는 등록 뒤). 결과는 AuctionService::OnSellerRead.
	void ReadSellerListings(int64_t sellerUID);
	// 서버 시작 때(생성자, DB 스레드를 만들기 전) 읽어 둔 판매 중인 등록 전체. 한 번만 가져간다(AuctionService::Init).
	vector<DB_AuctionListing> TakeInitialAuctionListings() { return std::move(_initialAuctionListings); }

	// 진행 상황(레벨·경험치·HP)을 저장한다(A2.5). 끝나면 room 잡(Room::OnProgressSaved)으로 저장한 값과 함께 돌아간다.
	// 플레이어당 동시에 하나만 보낸다(Room::RequestProgressSave).
	void SavePlayerProgress(shared_ptr<class Room> room, shared_ptr<class Player> player, int64_t playerUID, DB_PlayerProgress progress, long long requestUs);
	// 액티브 칸 배치 5칸을 저장한다(A3.6). 끝나면 room 잡(Room::OnQuickSlotsSaved)으로 저장한 값과 함께 돌아간다.
	// 플레이어당 동시에 하나만 보낸다(Room::RequestQuickSlotSave).
	void SaveQuickSlots(shared_ptr<class Room> room, shared_ptr<class Player> player, int64_t playerUID, QuickSlotArray slots);

private:
	// DB의 Schema_Version이 DbQueries::DB_SCHEMA_VERSION과 같은지 확인한다. 다르면 이유를 로그에 남기고 false.
	bool CheckSchemaVersion();
	// 아이템 작업 하나를 실행하고 결과를 돌려준다(바인딩 → 실행 → 결과 행 읽기). ExecItemOp·ExecAuctionOp가 쓴다.
	DB_ItemOpResult RunItemOp(DB_ItemOp& op);
	// 아이템 트랜잭션 틀(DbQueries::ItemOpHead/ItemOpTail)의 쿼리를 실행하고 결과 행(Ok, Kind, V)을 읽는다.
	// 파라미터는 부르는 쪽이 미리 바인딩한다. 연결은 돌려놓지 않는다(부르는 쪽이 Push).
	DB_ItemOpResult RunItemQuery(DBConnection* dbConn, const wchar_t* query, const char* kindName, int64_t playerUID, bool forceFail);
	// 판매 중인 등록을 읽는다. sellerUID가 0이면 전부, 아니면 그 판매자 것만.
	bool ReadListings(DBConnection* dbConn, int64_t sellerUID, vector<DB_AuctionListing>& out);

private:
	ConcurrentQ<shared_ptr<Job>> _dbJobQueue;
	vector<thread> _threads;
	unique_ptr<DBConnectionPool> _dbConnectionPool;
	vector<DB_AuctionListing> _initialAuctionListings;   // 생성자가 읽어 둔 판매 중인 경매 등록(A4.3)

	HANDLE						_iocpHandle;
};

