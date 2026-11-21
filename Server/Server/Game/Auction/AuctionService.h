#pragma once
#include "Data/ServerData.h"
#include "Job/JobQueue.h"
#include "Job/Timer.h"

extern shared_ptr<Timer> GTimer;

class Room;
class Player;
class GameSession;

// 경매장 전역 서비스 (A4.3, D-159·D-234·D-235 — Docs/DESIGN_ITEMS_TRADE.md "경매장"). 전역 GAuction, 수명 = 프로세스.
// AuthLobby·WorldService와 같은 꼴: 자체 잡 큐를 갖고, 상태(판매 중 목록)는 서비스 잡 안에서만 읽고 쓴다. 밖에서는 GAuction->PushJob.
//
// 목록의 권위는 DB(Auction_Listing)이고, 메모리 목록은 검색과 "같은 등록에 두 요청이 겹치지 않게" 하는 데 쓴다.
//   - 등록·구매·취소: Room 잡(검증, itemOp 켬 — Room::StartAuctionOp) → BeginOp(목록 검증) → DatabaseWorker::ExecAuctionOp(한 트랜잭션)
//     → OnOpDone(목록 갱신) → Room::OnItemOpDone(메모리·패킷 반영, itemOp 끔).
//     **BeginOp가 거부해 DB에 가지 않을 때도 반드시 Room::OnItemOpDone으로 돌려준다** — 빠지면 그 플레이어의 itemOp.inFlight가 켜진 채 남아
//     다음 로그인이 LoginCloseForceSec만큼 막히고 Room 이동이 TIMEOUT 되고 던전 인스턴스가 닫히지 않는다.
//   - 검색·내 목록: Room 잡은 검증만 하고(itemOp를 쓰지 않는다 — 읽기 전용이라 Room 이동·퇴장 정리와 무관), 서비스가 세션에 직접 답한다.
//   - 처리 중 표시(Listing::busy): 구매·취소·만료는 그 등록에 표시를 한 뒤 DB로 보낸다(DB 작업 스레드가 여러 개라 같은 등록의 두 트랜잭션이
//     동시에 돌 수 있다). 표시된 등록에 온 다른 요청은 거부하고 만료 검사는 건너뛴다. 성공하면 목록에서 빼고, 실패하면 DB의 State를 다시 읽어
//     판매 중이면 표시를 풀고 아니면 목록에서 뺀다(VerifyListing — 롤백이든 결과 불명이든 목록이 DB와 다시 맞는다).
//   - 서비스는 Player의 필드를 만지지 않는다(필드는 소유 Room 잡에서만 — Server/CLAUDE.md "구역별 Room"). 필요한 값은 DB_ItemOp에 실려 온다.
class AuctionService : public enable_shared_from_this<AuctionService>
{
public:
	// 검색 필터(CS_AUCTION_SEARCH.slot): 0 = 전체, 1~7 = 장비 슬롯(ItemSlot), 8 = 슬롯이 없는 아이템(소비 등)
	static constexpr int FILTER_ALL = 0;
	static constexpr int FILTER_OTHER = 8;
	static constexpr int FILTER_COUNT = 9;

	AuctionService(HANDLE iocpHandle);
	~AuctionService();

	template<typename... Arguments>
	void PushJob(void(AuctionService::* memFunc)(Arguments...), Arguments... args)
	{
		_jobQueue->Push(shared_from_this(), memFunc, std::forward<Arguments>(args)...);
	}

	template<typename... Arguments>
	void ReserveJob(DWORD milliseconds, void(AuctionService::* memFunc)(Arguments...), Arguments... args)
	{
		GTimer->Reserve(milliseconds, _jobQueue, shared_from_this(), memFunc, std::forward<Arguments>(args)...);
	}

	// 서버 시작 때 한 번(워커 스레드를 만들기 전, 메인 스레드에서 직접): DB에서 읽어 둔 판매 중인 등록으로 목록을 만들고 만료 검사를 시작한다.
	// 서버가 꺼져 있던 동안 기간이 지난 등록은 첫 검사에서 만료 처리된다.
	void Init(vector<DB_AuctionListing> rows);

	// ---- 혼잡 기준(D-235) ----
	// 경매 종류의 DB 작업(등록·구매·취소·수령·수령함 읽기·만료 묶음)을 시작해도 되는가. 되면 진행 수를 1 늘리고 true —
	// 그 작업의 결과를 받은 곳에서 반드시 EndDbWork를 부른다. ServerConfig AuctionDbMaxInFlight를 넘으면 false(TRY_LATER로 거부).
	// 어느 스레드에서 불러도 된다(원자 값).
	static bool TryBeginDbWork();
	static void EndDbWork();

	// ---- Room 잡이 넣는 요청 ----
	// 검색: 필터의 가격 오름차순 목록에서 한 쪽을 SC_AUCTION_LIST로 답한다. 범위를 넘는 쪽은 마지막 쪽으로 맞춘다.
	void Search(shared_ptr<GameSession> session, int64_t playerUID, int slot, int page);
	// 내 등록 + 수령함: 수령함은 메모리에 두지 않고 DB에서 읽는다(오래된 것부터 AuctionPageSize개) → OnMailRead가 SC_AUCTION_MY로 답한다.
	// 부르는 쪽(Room::PlayerAuctionMy)이 TryBeginDbWork를 통과한 뒤 넣는다.
	void QueryMy(shared_ptr<GameSession> session, int64_t playerUID);
	// 등록·구매·취소: 목록으로 검증하고 DB로 보낸다. 거부면 결과에 사유(DB_ItemOpResult::auctionResult)를 적어 Room::OnItemOpDone으로 돌려준다.
	void BeginOp(shared_ptr<Room> room, shared_ptr<Player> player, DB_ItemOp op);

	// ---- DatabaseWorker가 넣는 결과 ----
	void OnOpDone(shared_ptr<Room> room, shared_ptr<Player> player, DB_ItemOp op, DB_ItemOpResult result);
	void OnMailRead(shared_ptr<GameSession> session, int64_t playerUID, vector<DB_MailRow> mails, bool more, bool success);
	void OnExpireDone(vector<int64_t> requested, DB_ItemOpResult result);
	// state: Auction_Listing.State(1 = 판매 중), 0 = 없는 등록, -1 = 읽지 못함(잠시 뒤 다시 읽는다)
	void OnListingVerified(int64_t listingId, int state);
	void OnSellerRead(int64_t sellerUID, vector<DB_AuctionListing> rows, bool success);

private:
	struct Listing
	{
		int64_t id = 0;
		int64_t sellerUID = 0;
		int32_t itemId = 0;
		int32_t count = 0;
		int64_t price = 0;          // 묶음 전체의 값
		long long expireAtMs = 0;   // 만료 시각(서버의 steady 시계)
		int filter = FILTER_OTHER;  // 이 등록이 들어간 슬롯 필터(1~8). 전체(0)에는 항상 들어간다
		bool busy = false;          // 처리 중(구매·취소·만료가 DB에 가 있거나 상태를 다시 읽는 중)
	};

	Listing* Find(int64_t listingId);
	void AddListing(const Listing& listing);
	void RemoveListing(int64_t listingId);
	static int FilterOf(int itemId);
	void FillInfo(Protocol::AuctionListingInfo* out, const Listing& listing, int64_t viewerUID, long long nowMs) const;

	// 예약 잡: 기간이 지난 등록을 묶어 만료 처리하고 자신을 다시 예약한다(EconomyTable.AuctionExpireCheckSec마다, 밀린 것이 있으면 1초 뒤).
	void CheckExpire();
	// 그 등록의 DB 상태를 다시 읽어 목록을 맞춘다(실패한 구매·취소·만료 뒤). 읽는 동안 처리 중 표시를 유지한다.
	void VerifyListing(int64_t listingId);
	// 그 판매자의 판매 중인 등록을 DB에서 다시 읽어 목록에 없는 것을 넣는다(결과를 알 수 없는 등록 뒤 — DB에 들어갔을 수 있다).
	void ResyncSeller(int64_t sellerUID);

private:
	static constexpr size_t EXPIRE_BATCH = 50;         // 만료 한 묶음의 등록 수(한 트랜잭션)
	static constexpr DWORD EXPIRE_RETRY_MS = 1000;     // 밀린 만료가 남았거나 혼잡할 때 다음 검사까지
	static constexpr DWORD VERIFY_RETRY_MS = 5000;     // DB 상태를 읽지 못했을 때 다시 읽기까지

	shared_ptr<JobQueue> _jobQueue;
	unordered_map<int64_t, Listing> _listings;                 // 판매 중인 등록(ListingId → 등록)
	unordered_map<int64_t, vector<int64_t>> _bySeller;         // 판매자(PlayerId) → 그 사람의 판매 중인 ListingId (1인 등록 수·내 등록)
	vector<pair<int64_t, int64_t>> _index[FILTER_COUNT];       // 필터별 (가격, ListingId) 오름차순 — 검색이 요청마다 정렬하지 않게 한다
	bool _expireInFlight = false;                              // 만료 묶음이 DB에 가 있다(한 번에 한 묶음)
};
