#pragma once
#include "Data/ServerData.h"
#include "Job/JobQueue.h"
#include "Job/Timer.h"
#include <array>
#include <deque>
#include <set>

extern shared_ptr<Timer> GTimer;

class Room;
class Player;
class GameSession;

// 경매장 전역 서비스 (A4.3, D-159·D-234·D-235 → AU2 재기획 D-281 — Docs/DESIGN_ITEMS_TRADE.md "경매장"). 전역 GAuction, 수명 = 프로세스.
// AuthLobby·WorldService와 같은 꼴: 자체 잡 큐를 갖고, 상태(판매 중 목록)는 서비스 잡 안에서만 읽고 쓴다. 밖에서는 GAuction->PushJob.
//
// 목록의 권위는 DB(Auction_Listing)이고, 메모리 목록은 검색과 "같은 등록에 온 요청끼리 판정이 어긋나지 않게" 하는 데 쓴다.
//   - 등록·구매·취소: Room 잡(검증, itemOp 켬 — Room::StartItemOp) → BeginOp(목록 검증·예약) → DatabaseWorker::ExecAuctionOp(한 트랜잭션)
//     → OnOpDone(목록 갱신) → Room::OnItemOpDone(메모리·패킷 반영, itemOp 끔).
//     **BeginOp가 거부해 DB에 가지 않을 때도 반드시 Room::OnItemOpDone으로 돌려준다** — 빠지면 그 플레이어의 itemOp.inFlight가 켜진 채 남아
//     다음 로그인이 LoginCloseForceSec만큼 막히고 Room 이동이 TIMEOUT 되고 던전 인스턴스가 닫히지 않는다.
//   - 검색·내 목록: Room 잡은 검증만 하고(itemOp를 쓰지 않는다 — 읽기 전용이라 Room 이동·퇴장 정리와 무관), 서비스가 세션에 직접 답한다.
//   - 구매 = 수량 예약(D-281): "남은 수량 − 예약"으로 판정해 예약을 잡고 DB로 보낸다. 같은 등록의 구매 여러 건이 함께 DB에 갈 수 있고
//     (DB 작업 스레드가 여러 개), DB는 조건부 갱신(Count >= N)으로 이중 판매를 막는다. 결과가 오면 예약을 돌려주고 성공이면 남은 수량을 줄인다.
//   - 취소·만료는 그 등록에 "끝내는 중"(closing) 표시를 한 뒤 DB로 보낸다. 진행 중인 구매가 있는 등록은 취소를 거부(LISTING_BUSY)하고 만료는 미룬다.
//   - 결과를 알 수 없는 실패(롤백 여부 불명) 뒤에는 그 등록의 진행 중인 구매가 모두 끝난 뒤 DB의 State·Count를 다시 읽어 맞춘다(needVerify → VerifyListing).
//     그동안 그 등록에 온 구매·취소는 LISTING_BUSY.
//   - 서비스는 Player의 필드를 만지지 않는다(필드는 소유 Room 잡에서만 — Server/CLAUDE.md "구역별 Room"). 필요한 값은 DB_ItemOp·SearchRequest에 실려 온다.
class AuctionService : public enable_shared_from_this<AuctionService>
{
public:
	// Room이 정리해 넣는 검색 요청(D-281). 검색어 판정(이름 → 아이템 id)은 Room이 하고, 서비스는 id로만 찾는다.
	struct SearchRequest
	{
		Protocol::AuctionQuery query = Protocol::AUCTION_QUERY_SEARCH;
		Protocol::AuctionSort sort = Protocol::AUCTION_SORT_UNIT_PRICE;
		bool desc = false;
		bool all = true;              // 검색어가 비었다 = 전체
		std::vector<int> itemIds;     // all이 아닐 때 이름이 맞는 아이템 id들(비어 있으면 0건)
		std::string keyword;          // 응답에 돌려줄 검색어(UTF-8, 표시용)
		int64_t cursorKey = 0;        // MORE: 받은 마지막 줄의 정렬 값
		int64_t cursorId = 0;         // MORE: 받은 마지막 줄의 ListingId (0 = 처음부터)
	};

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
	// 검색(D-281): 정렬 색인에서 커서 다음부터 EconomyTable.AuctionFetchRows줄을 SC_AUCTION_LIST로 답한다.
	void Search(shared_ptr<GameSession> session, int64_t playerUID, SearchRequest request);
	// 내 등록 + 수령함: 수령함은 메모리에 두지 않고 DB에서 읽는다(오래된 것부터 AuctionMailPageSize개) → OnMailRead가 SC_AUCTION_MY로 답한다.
	// 부르는 쪽(Room::PlayerAuctionMy)이 TryBeginDbWork를 통과한 뒤 넣는다.
	void QueryMy(shared_ptr<GameSession> session, int64_t playerUID);
	// 등록·구매·취소: 목록으로 검증(구매는 예약)하고 DB로 보낸다. 거부면 결과에 사유(DB_ItemOpResult::auctionResult)를 적어 Room::OnItemOpDone으로 돌려준다.
	void BeginOp(shared_ptr<Room> room, shared_ptr<Player> player, DB_ItemOp op);

	// ---- DatabaseWorker가 넣는 결과 ----
	void OnOpDone(shared_ptr<Room> room, shared_ptr<Player> player, DB_ItemOp op, DB_ItemOpResult result);
	void OnMailRead(shared_ptr<GameSession> session, int64_t playerUID, vector<DB_MailRow> mails, bool more, bool success);
	void OnExpireDone(vector<int64_t> requested, DB_ItemOpResult result);
	// state: Auction_Listing.State(1 = 판매 중), 0 = 없는 등록, -1 = 읽지 못함(잠시 뒤 다시 읽는다). count = DB의 남은 수량
	void OnListingVerified(int64_t listingId, int state, int count);
	void OnSellerRead(int64_t sellerUID, vector<DB_AuctionListing> rows, bool success);

private:
	// 정렬 종류(Protocol::AuctionSort와 같은 번호)
	static constexpr int SORT_COUNT = 3;
	using SortKey = pair<int64_t, int64_t>;   // (정렬 값, ListingId)
	using SortIndex = std::set<SortKey>;

	struct Listing
	{
		int64_t id = 0;
		int64_t sellerUID = 0;
		int32_t itemId = 0;
		int32_t count = 0;          // 남은 수량(DB에 확정된 것)
		int64_t unitPrice = 0;      // 1개당 가격
		long long expireAtMs = 0;   // 만료 시각(서버의 steady 시계)
		int32_t reserved = 0;       // 진행 중인 구매가 예약한 수량 합
		int32_t inFlightBuys = 0;   // DB에 가 있는 구매 수
		bool closing = false;       // 취소·만료가 DB에 가 있다
		bool needVerify = false;    // 결과를 알 수 없는 실패가 있었다 — 진행 중인 구매가 끝나면 DB를 다시 읽는다
		bool verifying = false;     // DB 상태를 읽는 중
	};

	Listing* Find(int64_t listingId);
	void AddListing(const Listing& listing);
	// 목록에서 뺀다. closedReason이 0이 아니면 "최근에 끝난 등록"으로 기억한다(구매 응답 문구 — SOLD·ALREADY_CLOSED)
	void RemoveListing(int64_t listingId, Protocol::AuctionResult closedReason);
	static int64_t SortValue(const Listing& listing, int sort);
	void IndexInsert(const Listing& listing);
	void IndexErase(const Listing& listing);
	// 남은 수량을 바꾼다(총액 색인을 다시 넣는다). 0 이하면 목록에서 빼고 SOLD로 기억한다
	void SetCount(Listing& listing, int32_t count);
	// 없는 등록에 온 구매의 사유: 최근에 끝났으면 그 사유, 아니면 NO_LISTING
	Protocol::AuctionResult ClosedReason(int64_t listingId);
	void PruneClosed(long long nowMs);
	void FillInfo(Protocol::AuctionListingInfo* out, const Listing& listing, int64_t viewerUID, long long nowMs) const;

	// 예약 잡: 기간이 지난 등록을 묶어 만료 처리하고 자신을 다시 예약한다(EconomyTable.AuctionExpireCheckSec마다, 밀린 것이 있으면 1초 뒤).
	void CheckExpire();
	// 그 등록의 DB 상태를 다시 읽어 목록을 맞춘다(실패한 구매·취소·만료 뒤). 진행 중인 구매가 있으면 끝날 때까지 미룬다.
	void VerifyListing(int64_t listingId);
	// 그 판매자의 판매 중인 등록을 DB에서 다시 읽어 목록에 없는 것을 넣는다(결과를 알 수 없는 등록 뒤 — DB에 들어갔을 수 있다).
	void ResyncSeller(int64_t sellerUID);

private:
	static constexpr size_t EXPIRE_BATCH = 50;         // 만료 한 묶음의 등록 수(한 트랜잭션)
	static constexpr DWORD EXPIRE_RETRY_MS = 1000;     // 밀린 만료가 남았거나 혼잡할 때 다음 검사까지
	static constexpr DWORD VERIFY_RETRY_MS = 5000;     // DB 상태를 읽지 못했을 때 다시 읽기까지
	static constexpr long long CLOSED_KEEP_MS = 3600LL * 1000;   // 끝난 등록의 사유를 기억하는 시간(D-281)

	shared_ptr<JobQueue> _jobQueue;
	unordered_map<int64_t, Listing> _listings;                       // 판매 중인 등록(ListingId → 등록)
	unordered_map<int64_t, vector<int64_t>> _bySeller;               // 판매자(PlayerId) → 그 사람의 판매 중인 ListingId (1인 등록 수·내 등록)
	std::array<SortIndex, SORT_COUNT> _all;                          // 정렬마다 전체 색인
	unordered_map<int, std::array<SortIndex, SORT_COUNT>> _byItem;   // 아이템 종류마다 정렬 색인(이름 검색 — 맞는 종류들을 병합)
	unordered_map<int64_t, Protocol::AuctionResult> _closed;         // 최근에 끝난 등록 → 사유
	std::deque<pair<long long, int64_t>> _closedOrder;               // (끝난 시각, ListingId) — 오래된 것부터 지운다
	bool _expireInFlight = false;                                    // 만료 묶음이 DB에 가 있다(한 번에 한 묶음)
};
