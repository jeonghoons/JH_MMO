#include "pch.h"
#include "Game/Auction/AuctionService.h"
#include "Network/Session.h"
#include "Game/Room/Room.h"
#include "Game/Object/Player.h"
#include "DB/DatabaseWorker.h"
#include "Utils/Metrics.h"

namespace
{
	long long NowMs()
	{
		return chrono::duration_cast<chrono::milliseconds>(chrono::steady_clock::now().time_since_epoch()).count();
	}

	// 정렬된 여러 범위를 병합해 앞에서부터 최대 fetch개를 out에 담는다(k-way 병합 — 이름 검색은 맞는 아이템 종류마다 색인이 따로라서).
	// before(a, b) = 결과 순서에서 a가 b보다 앞. 다 담고도 남은 것이 있으면 true.
	template<typename It, typename Before>
	bool MergeTake(vector<pair<It, It>>& ranges, Before before, size_t fetch, vector<pair<int64_t, int64_t>>& out)
	{
		// 힙의 맨 위 = 다음에 나올 것. std 힙은 비교가 참인 쪽을 아래로 두므로 순서를 뒤집어 준다.
		auto heapLess = [&](const pair<It, It>& a, const pair<It, It>& b) { return before(*b.first, *a.first); };
		make_heap(ranges.begin(), ranges.end(), heapLess);
		while (!ranges.empty() && out.size() < fetch)
		{
			pop_heap(ranges.begin(), ranges.end(), heapLess);
			pair<It, It>& top = ranges.back();
			out.push_back(*top.first);
			++top.first;
			if (top.first == top.second) ranges.pop_back();
			else push_heap(ranges.begin(), ranges.end(), heapLess);
		}
		return !ranges.empty();
	}
}

AuctionService::AuctionService(HANDLE iocpHandle) : _jobQueue(make_shared<JobQueue>(iocpHandle))
{
}

AuctionService::~AuctionService() = default;

void AuctionService::Init(vector<DB_AuctionListing> rows)
{
	const long long nowMs = NowMs();
	int overdue = 0;
	for (const DB_AuctionListing& row : rows)
	{
		Listing listing;
		listing.id = row.listingId;
		listing.sellerUID = row.sellerUID;
		listing.itemId = row.itemId;
		listing.count = row.count;
		listing.unitPrice = row.unitPrice;
		listing.expireAtMs = nowMs + static_cast<long long>(row.remainSec) * 1000;
		if (row.remainSec <= 0) ++overdue;
		AddListing(listing);
	}
	LOG_INFO("Auction", "경매 목록 로드 완료 (판매 중 {}건, 그중 기간이 지난 것 {}건 — 첫 검사에서 만료 처리)", _listings.size(), overdue);

	// 첫 검사는 곧바로(꺼져 있던 동안 지난 만료), 그 뒤로는 CheckExpire가 스스로 다시 예약한다.
	ReserveJob(EXPIRE_RETRY_MS, &AuctionService::CheckExpire);
}

bool AuctionService::TryBeginDbWork()
{
	const int limit = GServerConfig.auctionDbMaxInFlight;
	const int now = Metrics::AuctionDbInFlight.fetch_add(1) + 1;
	if (limit > 0 && now > limit)
	{
		Metrics::AuctionDbInFlight.fetch_sub(1);
		Metrics::AuctionTryLater.fetch_add(1);
		return false;
	}
	return true;
}

void AuctionService::EndDbWork()
{
	Metrics::AuctionDbInFlight.fetch_sub(1);
}

AuctionService::Listing* AuctionService::Find(int64_t listingId)
{
	const auto it = _listings.find(listingId);
	return it == _listings.end() ? nullptr : &it->second;
}

int64_t AuctionService::SortValue(const Listing& listing, int sort)
{
	switch (sort)
	{
	case Protocol::AUCTION_SORT_TOTAL_PRICE: return listing.unitPrice * static_cast<int64_t>(listing.count);
	case Protocol::AUCTION_SORT_REMAIN: return listing.expireAtMs;
	default: return listing.unitPrice;
	}
}

void AuctionService::IndexInsert(const Listing& listing)
{
	auto& byItem = _byItem[listing.itemId];
	for (int sort = 0; sort < SORT_COUNT; ++sort)
	{
		const SortKey key{ SortValue(listing, sort), listing.id };
		_all[sort].insert(key);
		byItem[sort].insert(key);
	}
}

void AuctionService::IndexErase(const Listing& listing)
{
	const auto item = _byItem.find(listing.itemId);
	for (int sort = 0; sort < SORT_COUNT; ++sort)
	{
		const SortKey key{ SortValue(listing, sort), listing.id };
		_all[sort].erase(key);
		if (item != _byItem.end()) item->second[sort].erase(key);
	}
	if (item != _byItem.end() && item->second[0].empty()) _byItem.erase(item);
}

void AuctionService::AddListing(const Listing& source)
{
	if (_listings.count(source.id) > 0) return;

	Listing listing = source;
	listing.reserved = 0;
	listing.inFlightBuys = 0;
	listing.closing = false;
	listing.needVerify = false;
	listing.verifying = false;
	_listings[listing.id] = listing;
	_bySeller[listing.sellerUID].push_back(listing.id);
	IndexInsert(listing);
	Metrics::AuctionListings.store(static_cast<int>(_listings.size()));
}

void AuctionService::RemoveListing(int64_t listingId, Protocol::AuctionResult closedReason)
{
	const auto it = _listings.find(listingId);
	if (it == _listings.end()) return;
	const Listing& listing = it->second;

	IndexErase(listing);

	const auto seller = _bySeller.find(listing.sellerUID);
	if (seller != _bySeller.end())
	{
		vector<int64_t>& ids = seller->second;
		ids.erase(remove(ids.begin(), ids.end(), listingId), ids.end());
		if (ids.empty()) _bySeller.erase(seller);
	}

	_listings.erase(it);
	Metrics::AuctionListings.store(static_cast<int>(_listings.size()));

	// 끝난 사유를 잠시 기억한다 — 보고 있던 목록에서 이 등록을 사려는 요청에 "이미 판매 완료"·"판매가 끝난 물품"을 가려 답한다(D-281).
	if (closedReason != Protocol::AUCTION_RESULT_NONE)
	{
		const long long nowMs = NowMs();
		_closed[listingId] = closedReason;
		_closedOrder.emplace_back(nowMs, listingId);
		PruneClosed(nowMs);
	}
}

void AuctionService::SetCount(Listing& listing, int32_t count)
{
	if (count <= 0)
	{
		RemoveListing(listing.id, Protocol::AUCTION_RESULT_SOLD);
		return;
	}
	if (count == listing.count) return;
	// 총액 색인의 값이 바뀌므로 빼고 다시 넣는다(빼는 것은 바꾸기 전 값으로).
	IndexErase(listing);
	listing.count = count;
	IndexInsert(listing);
}

Protocol::AuctionResult AuctionService::ClosedReason(int64_t listingId)
{
	PruneClosed(NowMs());
	const auto it = _closed.find(listingId);
	return it != _closed.end() ? it->second : Protocol::AUCTION_RESULT_NO_LISTING;
}

void AuctionService::PruneClosed(long long nowMs)
{
	while (!_closedOrder.empty() && nowMs - _closedOrder.front().first > CLOSED_KEEP_MS)
	{
		_closed.erase(_closedOrder.front().second);
		_closedOrder.pop_front();
	}
}

void AuctionService::FillInfo(Protocol::AuctionListingInfo* out, const Listing& listing, int64_t viewerUID, long long nowMs) const
{
	out->set_listing_id(listing.id);
	out->set_item_id(listing.itemId);
	out->set_count(listing.count);
	out->set_unit_price(listing.unitPrice);
	out->set_remain_sec(static_cast<int32_t>(max<long long>(0, (listing.expireAtMs - nowMs) / 1000)));
	out->set_mine(listing.sellerUID == viewerUID);
}

void AuctionService::Search(shared_ptr<GameSession> session, int64_t playerUID, SearchRequest request)
{
	Metrics::AuctionSearches.fetch_add(1);
	if (session == nullptr || session->IsConnected() == false) return;

	const EconomyData& economy = DataManager::GetEconomy();
	const int sort = Protocol::AuctionSort_IsValid(request.sort) ? static_cast<int>(request.sort) : 0;

	// 찾을 색인: 검색어가 없으면 전체 색인 하나, 있으면 이름이 맞는 아이템 종류마다의 색인.
	vector<const SortIndex*> sources;
	if (request.all)
	{
		sources.push_back(&_all[sort]);
	}
	else
	{
		for (const int itemId : request.itemIds)
		{
			const auto it = _byItem.find(itemId);
			if (it != _byItem.end() && !it->second[sort].empty()) sources.push_back(&it->second[sort]);
		}
	}
	int total = 0;
	for (const SortIndex* index : sources) total += static_cast<int>(index->size());

	// 커서(MORE): 받은 마지막 줄의 (정렬 값, ListingId) 다음부터. 그 사이에 팔리거나 새로 올라와도 줄이 겹치거나 빠지지 않는다.
	// 처리 중이거나 기간이 지난(아직 만료 처리 전인) 등록도 그대로 보인다 — 구매를 누르면 그때 사유로 답한다.
	const bool fromCursor = (request.query == Protocol::AUCTION_QUERY_MORE && request.cursorId != 0);
	const SortKey cursor{ request.cursorKey, request.cursorId };
	const size_t fetch = static_cast<size_t>(economy.auctionFetchRows);
	vector<SortKey> rows;
	bool hasMore = false;
	if (!request.desc)
	{
		using It = SortIndex::const_iterator;
		vector<pair<It, It>> ranges;
		for (const SortIndex* index : sources)
		{
			It begin = fromCursor ? index->upper_bound(cursor) : index->begin();
			if (begin != index->end()) ranges.emplace_back(begin, index->end());
		}
		hasMore = MergeTake(ranges, [](const SortKey& a, const SortKey& b) { return a < b; }, fetch, rows);
	}
	else
	{
		using It = SortIndex::const_reverse_iterator;
		vector<pair<It, It>> ranges;
		for (const SortIndex* index : sources)
		{
			// reverse_iterator(lower_bound(cursor)) = 커서보다 작은 것 중 가장 큰 것부터
			It begin = fromCursor ? It(index->lower_bound(cursor)) : index->rbegin();
			if (begin != index->rend()) ranges.emplace_back(begin, index->rend());
		}
		hasMore = MergeTake(ranges, [](const SortKey& a, const SortKey& b) { return b < a; }, fetch, rows);
	}

	const long long nowMs = NowMs();
	Protocol::SC_AUCTION_LIST_PACKET packet;
	packet.set_slot(0);
	packet.set_query(request.query);
	packet.set_sort(static_cast<Protocol::AuctionSort>(sort));
	packet.set_desc(request.desc);
	packet.set_total_count(total);
	packet.set_has_more(hasMore);
	packet.set_keyword(request.keyword);
	packet.set_page_rows(economy.auctionPageRows);
	packet.set_search_interval_ms(economy.auctionSearchIntervalMs);
	packet.set_action_interval_ms(economy.auctionActionIntervalMs);
	for (const SortKey& key : rows)
	{
		const auto it = _listings.find(key.second);
		if (it == _listings.end()) continue;
		FillInfo(packet.add_listings(), it->second, playerUID, nowMs);
		packet.add_cursor_keys(key.first);
	}
	session->Send(PacketSerializer::MakeSendBuffer(packet, Protocol::SC_AUCTION_LIST));
}

void AuctionService::QueryMy(shared_ptr<GameSession> session, int64_t playerUID)
{
	GDBWorker->PushDBJob(&DatabaseWorker::ReadMail, session, playerUID, static_cast<int>(DataManager::GetEconomy().auctionMailPageSize));
}

void AuctionService::OnMailRead(shared_ptr<GameSession> session, int64_t playerUID, vector<DB_MailRow> mails, bool more, bool success)
{
	EndDbWork();   // Room::PlayerAuctionMy의 TryBeginDbWork와 짝
	if (session == nullptr || session->IsConnected() == false) return;

	if (!success)
	{
		session->Send(PacketSerializer::MAKE_SC_AUCTION_RESULT(Protocol::CS_AUCTION_MY, Protocol::AUCTION_RESULT_DB_ERROR, 0));
		return;
	}

	const EconomyData& economy = DataManager::GetEconomy();
	const long long nowMs = NowMs();
	Protocol::SC_AUCTION_MY_PACKET packet;
	const auto seller = _bySeller.find(playerUID);
	if (seller != _bySeller.end())
	{
		for (const int64_t listingId : seller->second)
		{
			const auto it = _listings.find(listingId);
			if (it != _listings.end()) FillInfo(packet.add_listings(), it->second, playerUID, nowMs);
		}
	}
	for (const DB_MailRow& row : mails)
	{
		Protocol::MailInfo* out = packet.add_mails();
		out->set_mail_id(row.mailId);
		out->set_kind(static_cast<Protocol::MailKind>(row.kind));
		out->set_gold(row.gold);
		out->set_item_id(row.itemId);
		out->set_count(row.count);
		out->set_listing_id(row.listingId);
	}
	packet.set_mail_more(more);
	packet.set_max_listings(economy.auctionMaxListings);
	packet.set_min_price(economy.auctionMinPrice);
	packet.set_max_price(economy.auctionMaxPrice);
	session->Send(PacketSerializer::MakeSendBuffer(packet, Protocol::SC_AUCTION_MY));
}

void AuctionService::BeginOp(shared_ptr<Room> room, shared_ptr<Player> player, DB_ItemOp op)
{
	// 거부해도 Room으로 돌려준다 — Room::OnItemOpDone이 itemOp를 끄고 본인에게 사유를 알린다(이 서비스는 Player를 만지지 않는다).
	auto reject = [&](Protocol::AuctionResult reason, int64_t remain = 0) {
		if (reason == Protocol::AUCTION_RESULT_NOT_ENOUGH_COUNT || reason == Protocol::AUCTION_RESULT_LISTING_BUSY)
			Metrics::AuctionConflicts.fetch_add(1);
		DB_ItemOpResult result;
		result.auctionResult = reason;
		result.auctionRemain = remain;
		room->PushJob(&Room::OnItemOpDone, player, op, result);
	};

	const long long nowMs = NowMs();
	switch (op.kind)
	{
	case ItemOpKind::AuctionRegister:
	{
		// 플레이어당 아이템 작업은 동시에 1건이라, 같은 판매자의 등록이 여기서 겹치지 않는다.
		const auto seller = _bySeller.find(op.playerUID);
		const int have = seller == _bySeller.end() ? 0 : static_cast<int>(seller->second.size());
		if (have >= DataManager::GetEconomy().auctionMaxListings) { reject(Protocol::AUCTION_RESULT_LISTING_LIMIT); return; }
		break;
	}
	case ItemOpKind::AuctionBuy:
	{
		// 부분 구매 + 수량 예약(D-281)
		Listing* listing = Find(op.listingId);
		if (listing == nullptr) { reject(ClosedReason(op.listingId)); return; }
		if (listing->sellerUID == op.playerUID) { reject(Protocol::AUCTION_RESULT_OWN_ITEM); return; }
		// 취소·만료가 DB에 가 있거나, 기간이 지났는데 아직 만료 검사 전인 등록은 살 수 없다(다음 검사에서 반환된다).
		if (listing->closing || nowMs >= listing->expireAtMs) { reject(Protocol::AUCTION_RESULT_ALREADY_CLOSED); return; }
		// 결과를 알 수 없는 실패 뒤 DB 상태를 다시 읽는 중이다 — 메모리 수량을 믿을 수 없다.
		if (listing->needVerify) { reject(Protocol::AUCTION_RESULT_LISTING_BUSY); return; }
		const int32_t available = listing->count - listing->reserved;
		if (available <= 0) { reject(Protocol::AUCTION_RESULT_SOLD); return; }
		if (op.auctionBuyCount > available) { reject(Protocol::AUCTION_RESULT_NOT_ENOUGH_COUNT, available); return; }
		// 수량 ≤ 남은 수량(≤ MaxStack)이고 단가 ≤ 20억이라 곱은 int64 안이다.
		const int64_t cost = listing->unitPrice * static_cast<int64_t>(op.auctionBuyCount);
		if (op.auctionGold < cost) { reject(Protocol::AUCTION_RESULT_NOT_ENOUGH_GOLD); return; }
		listing->reserved += op.auctionBuyCount;
		listing->inFlightBuys += 1;
		op.auctionUnitPrice = listing->unitPrice;
		op.auctionSellerUID = listing->sellerUID;
		// 판매자에게 보낼 수령함 알림용(D-262). op.itemId는 DB 바인딩(등록)에 쓰는 칸이라 건드리지 않는다.
		op.auctionItemId = listing->itemId;
		break;
	}
	case ItemOpKind::AuctionCancel:
	{
		Listing* listing = Find(op.listingId);
		if (listing == nullptr || listing->sellerUID != op.playerUID) { reject(Protocol::AUCTION_RESULT_NO_LISTING); return; }
		if (listing->closing) { reject(Protocol::AUCTION_RESULT_ALREADY_CLOSED); return; }
		// 구매가 DB에 가 있거나 DB 상태를 다시 읽는 중이면 취소하지 않는다 — 끝난 뒤 다시(D-281).
		if (listing->inFlightBuys > 0 || listing->needVerify) { reject(Protocol::AUCTION_RESULT_LISTING_BUSY); return; }
		listing->closing = true;
		break;
	}
	default:
		reject(Protocol::AUCTION_RESULT_NOT_ALLOWED);
		return;
	}

	GDBWorker->PushDBJob(&DatabaseWorker::ExecAuctionOp, room, player, op);
}

void AuctionService::OnOpDone(shared_ptr<Room> room, shared_ptr<Player> player, DB_ItemOp op, DB_ItemOpResult result)
{
	switch (op.kind)
	{
	case ItemOpKind::AuctionRegister:
		if (result.success && result.listingId != 0)
		{
			Listing listing;
			listing.id = result.listingId;
			listing.sellerUID = op.playerUID;
			listing.itemId = op.itemId;
			listing.count = op.auctionCount;
			listing.unitPrice = op.auctionUnitPrice;
			listing.expireAtMs = NowMs() + static_cast<long long>(op.auctionDurationSec) * 1000;
			AddListing(listing);
			Metrics::AuctionRegistered.fetch_add(1);
			LOG_DEBUG("Auction", "등록 listing={} seller={} item={} count={} unit={}", listing.id, listing.sellerUID, listing.itemId, listing.count, listing.unitPrice);
		}
		else if (result.unknown)
		{
			// DB에 들어갔을 수도 있다 — 들어갔는데 목록에 없으면 검색에 보이지 않고 만료도 되지 않으므로 그 판매자의 등록을 다시 읽는다.
			ResyncSeller(op.playerUID);
		}
		break;
	case ItemOpKind::AuctionBuy:
	{
		// 예약을 돌려준다(성공이면 아래에서 남은 수량을 줄인다).
		Listing* listing = Find(op.listingId);
		if (listing != nullptr)
		{
			listing->reserved = max(0, listing->reserved - op.auctionBuyCount);
			listing->inFlightBuys = max(0, listing->inFlightBuys - 1);
		}

		if (result.success)
		{
			// 판매가 성사된 지점이다: 판매자가 접속 중이면 수령함 알림(팔린 수량·대금)을 보낸다(D-262). 명부는 WorldService의 것이라 그 잡으로 넘긴다.
			const int64_t gold = op.auctionUnitPrice * static_cast<int64_t>(op.auctionBuyCount);
			GWorld->PushJob(&WorldService::NotifySeller, op.auctionSellerUID,
				PacketSerializer::MAKE_SC_MAIL_NOTICE(Protocol::MAIL_KIND_SALE_GOLD, op.auctionItemId, op.auctionBuyCount, gold, op.listingId));
			Metrics::AuctionBought.fetch_add(1);
			if (result.auctionRemain > 0) Metrics::AuctionPartialBuys.fetch_add(1);
			LOG_DEBUG("Auction", "구매 listing={} player={} count={} unit={} remain={}", op.listingId, op.playerUID, op.auctionBuyCount, op.auctionUnitPrice, result.auctionRemain);

			if (listing != nullptr)
			{
				// 남은 수량은 줄어들기만 한다. 결과가 커밋 순서와 다르게 도착해도 맞도록 메모리 값과 DB가 알려 준 값 중 작은 것을 쓴다.
				const int32_t next = min(listing->count - op.auctionBuyCount, static_cast<int32_t>(result.auctionRemain));
				const bool verifyAfter = listing->needVerify && listing->inFlightBuys == 0;
				SetCount(*listing, next);   // 0이면 목록에서 빠진다(listing이 무효가 됨)
				if (verifyAfter) VerifyListing(op.listingId);
			}
		}
		else if (listing != nullptr)
		{
			// 롤백이든 결과 불명이든 메모리가 DB와 어긋났을 수 있다 — 진행 중인 구매가 모두 끝나면 DB의 State·Count를 다시 읽어 맞춘다.
			listing->needVerify = true;
			if (listing->inFlightBuys == 0) VerifyListing(op.listingId);
		}
		break;
	}
	case ItemOpKind::AuctionCancel:
		if (result.success)
		{
			RemoveListing(op.listingId, Protocol::AUCTION_RESULT_ALREADY_CLOSED);
			Metrics::AuctionCancelled.fetch_add(1);
			LOG_DEBUG("Auction", "취소 listing={} player={}", op.listingId, op.playerUID);
		}
		else
		{
			// 롤백이든 결과 불명이든 DB의 상태를 다시 읽어 맞춘다. 그동안 끝내는 중 표시는 그대로 둔다.
			VerifyListing(op.listingId);
		}
		break;
	default:
		break;
	}

	room->PushJob(&Room::OnItemOpDone, player, op, result);
}

void AuctionService::VerifyListing(int64_t listingId)
{
	Listing* listing = Find(listingId);
	if (listing == nullptr) return;
	listing->needVerify = true;
	// 구매가 DB에 가 있으면 그 결과가 끝난 뒤에 읽는다(OnOpDone이 다시 부른다). 이미 읽는 중이면 그 결과를 기다린다.
	if (listing->inFlightBuys > 0 || listing->verifying) return;
	listing->verifying = true;
	GDBWorker->PushDBJob(&DatabaseWorker::ReadListingState, listingId);
}

void AuctionService::OnListingVerified(int64_t listingId, int state, int count)
{
	Listing* listing = Find(listingId);
	if (listing == nullptr) return;
	listing->verifying = false;

	if (state < 0)
	{
		LOG_WARN("Auction", "등록 {} 의 DB 상태를 읽지 못했습니다 — {}ms 뒤 다시 읽습니다(그동안 이 등록은 처리 중으로 남습니다)", listingId, VERIFY_RETRY_MS);
		ReserveJob(VERIFY_RETRY_MS, &AuctionService::VerifyListing, listingId);
		return;
	}
	if (state == 1 && count > 0)
	{
		// 아직 판매 중이다(트랜잭션이 롤백됐거나 일부만 반영됨) — DB의 남은 수량으로 맞추고 표시를 푼다.
		listing->needVerify = false;
		listing->closing = false;
		SetCount(*listing, count);
		return;
	}
	LOG_INFO("Auction", "등록 {} 은 DB에서 판매 중이 아닙니다(State {}) — 목록에서 뺍니다", listingId, state);
	const Protocol::AuctionResult reason = state == 2 ? Protocol::AUCTION_RESULT_SOLD
		: (state == 3 || state == 4) ? Protocol::AUCTION_RESULT_ALREADY_CLOSED : Protocol::AUCTION_RESULT_NONE;
	RemoveListing(listingId, reason);
}

void AuctionService::ResyncSeller(int64_t sellerUID)
{
	GDBWorker->PushDBJob(&DatabaseWorker::ReadSellerListings, sellerUID);
}

void AuctionService::OnSellerRead(int64_t sellerUID, vector<DB_AuctionListing> rows, bool success)
{
	if (!success)
	{
		LOG_WARN("Auction", "판매자 {} 의 등록을 다시 읽지 못했습니다 — {}ms 뒤 다시 읽습니다", sellerUID, VERIFY_RETRY_MS);
		ReserveJob(VERIFY_RETRY_MS, &AuctionService::ResyncSeller, sellerUID);
		return;
	}

	// 목록에 없는 것만 넣는다. 그 사이 팔려서 뺀 등록이 다시 들어가는 드문 경우는, 다음 구매가 롤백된 뒤 VerifyListing이 걸러 낸다.
	const long long nowMs = NowMs();
	for (const DB_AuctionListing& row : rows)
	{
		if (_listings.count(row.listingId) > 0) continue;
		Listing listing;
		listing.id = row.listingId;
		listing.sellerUID = row.sellerUID;
		listing.itemId = row.itemId;
		listing.count = row.count;
		listing.unitPrice = row.unitPrice;
		listing.expireAtMs = nowMs + static_cast<long long>(row.remainSec) * 1000;
		AddListing(listing);
		LOG_INFO("Auction", "판매자 {} 의 등록 {} 을 DB에서 읽어 목록에 넣었습니다", sellerUID, row.listingId);
	}
}

void AuctionService::CheckExpire()
{
	const DWORD normalMs = static_cast<DWORD>(DataManager::GetEconomy().auctionExpireCheckSec) * 1000;
	DWORD nextMs = normalMs;

	if (_expireInFlight)
	{
		nextMs = EXPIRE_RETRY_MS;   // 앞 묶음의 결과를 기다린다
	}
	else
	{
		// 목록 전체를 본다(등록이 아주 많아지면 만료 시각 색인 _all[REMAIN]을 앞에서부터 보는 방식으로 — A5.4).
		// 구매가 DB에 가 있거나 DB 상태를 다시 읽는 중인 등록은 이번에는 건너뛴다(D-281).
		const long long nowMs = NowMs();
		vector<int64_t> ids;
		bool more = false;
		for (const auto& [id, listing] : _listings)
		{
			if (listing.closing || listing.needVerify || listing.verifying || listing.inFlightBuys > 0 || nowMs < listing.expireAtMs) continue;
			if (ids.size() >= EXPIRE_BATCH) { more = true; break; }
			ids.push_back(id);
		}

		if (!ids.empty())
		{
			// 만료도 혼잡 기준(D-235)에 든다: 꽉 차 있으면 이번에는 보내지 않고 곧 다시 본다.
			if (TryBeginDbWork())
			{
				for (const int64_t id : ids)
					_listings[id].closing = true;
				_expireInFlight = true;
				GDBWorker->PushDBJob(&DatabaseWorker::ExecAuctionExpire, ids);
				if (more) nextMs = EXPIRE_RETRY_MS;
			}
			else
			{
				nextMs = EXPIRE_RETRY_MS;
			}
		}
	}

	ReserveJob(nextMs, &AuctionService::CheckExpire);
}

void AuctionService::OnExpireDone(vector<int64_t> requested, DB_ItemOpResult result)
{
	EndDbWork();
	_expireInFlight = false;

	if (!result.success)
	{
		// 롤백이면 바뀐 것이 없고, 결과 불명이면 반영됐을 수 있다 — 어느 쪽이든 DB 상태를 다시 읽어 맞춘다.
		LOG_WARN("Auction", "만료 묶음 {}건이 실패했습니다(결과 불명 {}) — 등록마다 DB 상태를 다시 읽습니다", requested.size(), result.unknown);
		for (const int64_t id : requested)
			VerifyListing(id);
		return;
	}

	const unordered_set<int64_t> expired(result.expiredIds.begin(), result.expiredIds.end());
	for (const int64_t id : requested)
	{
		if (expired.count(id) == 0)
		{
			LOG_WARN("Auction", "만료하려던 등록 {} 은 DB에서 이미 판매 중이 아니었습니다 — 목록에서 뺍니다", id);
			RemoveListing(id, Protocol::AUCTION_RESULT_NONE);
			continue;
		}
		Metrics::AuctionExpired.fetch_add(1);
		// 만료 반환도 판매자에게 알린다(D-262 — 구매와 같은 경로). 남은 수량이 돌아간다. 목록에서 빼기 전에 종류·수량을 읽는다.
		if (const Listing* listing = Find(id))
		{
			GWorld->PushJob(&WorldService::NotifySeller, listing->sellerUID,
				PacketSerializer::MAKE_SC_MAIL_NOTICE(Protocol::MAIL_KIND_EXPIRED, listing->itemId, listing->count, 0, id));
		}
		RemoveListing(id, Protocol::AUCTION_RESULT_ALREADY_CLOSED);
	}
	LOG_DEBUG("Auction", "만료 처리 {}건(요청 {}건)", expired.size(), requested.size());
}
