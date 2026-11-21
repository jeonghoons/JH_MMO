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
		listing.price = row.price;
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

int AuctionService::FilterOf(int itemId)
{
	const ItemData* item = DataManager::GetItemData(itemId);
	const int slot = item != nullptr ? static_cast<int>(item->slot) : 0;
	return (slot >= 1 && slot < FILTER_OTHER) ? slot : FILTER_OTHER;
}

void AuctionService::AddListing(const Listing& source)
{
	if (_listings.count(source.id) > 0) return;

	Listing listing = source;
	listing.filter = FilterOf(listing.itemId);
	listing.busy = false;
	_listings[listing.id] = listing;
	_bySeller[listing.sellerUID].push_back(listing.id);

	const pair<int64_t, int64_t> key{ listing.price, listing.id };
	for (const int filter : { FILTER_ALL, listing.filter })
	{
		auto& index = _index[filter];
		index.insert(lower_bound(index.begin(), index.end(), key), key);
	}
	Metrics::AuctionListings.store(static_cast<int>(_listings.size()));
}

void AuctionService::RemoveListing(int64_t listingId)
{
	const auto it = _listings.find(listingId);
	if (it == _listings.end()) return;
	const Listing& listing = it->second;

	const pair<int64_t, int64_t> key{ listing.price, listing.id };
	for (const int filter : { FILTER_ALL, listing.filter })
	{
		auto& index = _index[filter];
		const auto found = lower_bound(index.begin(), index.end(), key);
		if (found != index.end() && *found == key) index.erase(found);
	}

	const auto seller = _bySeller.find(listing.sellerUID);
	if (seller != _bySeller.end())
	{
		vector<int64_t>& ids = seller->second;
		ids.erase(remove(ids.begin(), ids.end(), listingId), ids.end());
		if (ids.empty()) _bySeller.erase(seller);
	}

	_listings.erase(it);
	Metrics::AuctionListings.store(static_cast<int>(_listings.size()));
}

void AuctionService::FillInfo(Protocol::AuctionListingInfo* out, const Listing& listing, int64_t viewerUID, long long nowMs) const
{
	out->set_listing_id(listing.id);
	out->set_item_id(listing.itemId);
	out->set_count(listing.count);
	out->set_price(listing.price);
	out->set_remain_sec(static_cast<int32_t>(max<long long>(0, (listing.expireAtMs - nowMs) / 1000)));
	out->set_mine(listing.sellerUID == viewerUID);
}

void AuctionService::Search(shared_ptr<GameSession> session, int64_t playerUID, int slot, int page)
{
	Metrics::AuctionSearches.fetch_add(1);
	if (session == nullptr || session->IsConnected() == false) return;

	if (slot < 0 || slot >= FILTER_COUNT) slot = FILTER_ALL;
	const vector<pair<int64_t, int64_t>>& index = _index[slot];
	const int pageSize = DataManager::GetEconomy().auctionPageSize;
	const int total = static_cast<int>(index.size());
	const int lastPage = total > 0 ? (total - 1) / pageSize : 0;
	page = clamp(page, 0, lastPage);

	// 처리 중이거나 기간이 지난(아직 만료 처리 전인) 등록도 그대로 보인다 — 구매를 누르면 그때 SOLD·NO_LISTING으로 답한다.
	const long long nowMs = NowMs();
	Protocol::SC_AUCTION_LIST_PACKET packet;
	packet.set_slot(slot);
	packet.set_page(page);
	packet.set_total_count(total);
	packet.set_page_size(pageSize);
	const int begin = page * pageSize;
	const int end = min(total, begin + pageSize);
	for (int i = begin; i < end; ++i)
	{
		const auto it = _listings.find(index[i].second);
		if (it == _listings.end()) continue;
		FillInfo(packet.add_listings(), it->second, playerUID, nowMs);
	}
	session->Send(PacketSerializer::MakeSendBuffer(packet, Protocol::SC_AUCTION_LIST));
}

void AuctionService::QueryMy(shared_ptr<GameSession> session, int64_t playerUID)
{
	GDBWorker->PushDBJob(&DatabaseWorker::ReadMail, session, playerUID, static_cast<int>(DataManager::GetEconomy().auctionPageSize));
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
	auto reject = [&](Protocol::AuctionResult reason) {
		DB_ItemOpResult result;
		result.auctionResult = reason;
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
		Listing* listing = Find(op.listingId);
		if (listing == nullptr) { reject(Protocol::AUCTION_RESULT_NO_LISTING); return; }
		if (listing->busy) { reject(Protocol::AUCTION_RESULT_SOLD); return; }
		if (listing->sellerUID == op.playerUID) { reject(Protocol::AUCTION_RESULT_OWN_ITEM); return; }
		// 기간이 지났는데 아직 만료 검사 전인 등록은 살 수 없다(다음 검사에서 반환된다).
		if (nowMs >= listing->expireAtMs) { reject(Protocol::AUCTION_RESULT_NO_LISTING); return; }
		if (op.auctionGold < listing->price) { reject(Protocol::AUCTION_RESULT_NOT_ENOUGH_GOLD); return; }
		listing->busy = true;
		op.auctionPrice = listing->price;
		op.auctionSellerUID = listing->sellerUID;
		// 판매자에게 보낼 수령함 알림용(D-262). op.itemId는 DB 바인딩(등록)에 쓰는 칸이라 건드리지 않는다.
		op.auctionItemId = listing->itemId;
		op.auctionItemCount = listing->count;
		break;
	}
	case ItemOpKind::AuctionCancel:
	{
		Listing* listing = Find(op.listingId);
		if (listing == nullptr || listing->sellerUID != op.playerUID) { reject(Protocol::AUCTION_RESULT_NO_LISTING); return; }
		if (listing->busy) { reject(Protocol::AUCTION_RESULT_SOLD); return; }
		listing->busy = true;
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
			listing.price = op.auctionPrice;
			listing.expireAtMs = NowMs() + static_cast<long long>(op.auctionDurationSec) * 1000;
			AddListing(listing);
			Metrics::AuctionRegistered.fetch_add(1);
			LOG_DEBUG("Auction", "등록 listing={} seller={} item={} count={} price={}", listing.id, listing.sellerUID, listing.itemId, listing.count, listing.price);
		}
		else if (result.unknown)
		{
			// DB에 들어갔을 수도 있다 — 들어갔는데 목록에 없으면 검색에 보이지 않고 만료도 되지 않으므로 그 판매자의 등록을 다시 읽는다.
			ResyncSeller(op.playerUID);
		}
		break;
	case ItemOpKind::AuctionBuy:
	case ItemOpKind::AuctionCancel:
		if (result.success)
		{
			// 구매가 성사된 지점이다: 판매자가 접속 중이면 수령함 알림을 보낸다(D-262). 명부는 WorldService의 것이라 그 잡으로 넘긴다
			// (접속하지 않았으면 아무것도 하지 않는다 — 다음 로그인 때 SelectLogin의 HasMail로 알린다).
			if (op.kind == ItemOpKind::AuctionBuy)
			{
				GWorld->PushJob(&WorldService::NotifySeller, op.auctionSellerUID,
					PacketSerializer::MAKE_SC_MAIL_NOTICE(Protocol::MAIL_KIND_SALE_GOLD, op.auctionItemId, op.auctionItemCount, op.auctionPrice, op.listingId));
			}
			RemoveListing(op.listingId);
			(op.kind == ItemOpKind::AuctionBuy ? Metrics::AuctionBought : Metrics::AuctionCancelled).fetch_add(1);
			LOG_DEBUG("Auction", "{} listing={} player={} price={}", op.kind == ItemOpKind::AuctionBuy ? "구매" : "취소", op.listingId, op.playerUID, op.auctionPrice);
		}
		else
		{
			// 롤백이든 결과 불명이든 DB의 상태를 다시 읽어 맞춘다. 그동안 처리 중 표시는 그대로 둔다.
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
	listing->busy = true;
	GDBWorker->PushDBJob(&DatabaseWorker::ReadListingState, listingId);
}

void AuctionService::OnListingVerified(int64_t listingId, int state)
{
	Listing* listing = Find(listingId);
	if (listing == nullptr) return;

	if (state < 0)
	{
		LOG_WARN("Auction", "등록 {} 의 DB 상태를 읽지 못했습니다 — {}ms 뒤 다시 읽습니다(그동안 이 등록은 처리 중으로 남습니다)", listingId, VERIFY_RETRY_MS);
		ReserveJob(VERIFY_RETRY_MS, &AuctionService::VerifyListing, listingId);
		return;
	}
	if (state == 1)
	{
		listing->busy = false;   // 아직 판매 중(트랜잭션이 롤백됐다)
		return;
	}
	LOG_INFO("Auction", "등록 {} 은 DB에서 판매 중이 아닙니다(State {}) — 목록에서 뺍니다", listingId, state);
	RemoveListing(listingId);
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
		listing.price = row.price;
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
		// 목록 전체를 본다(등록이 아주 많아지면 만료 시각순 색인을 검토 — A5.4).
		const long long nowMs = NowMs();
		vector<int64_t> ids;
		bool more = false;
		for (const auto& [id, listing] : _listings)
		{
			if (listing.busy || nowMs < listing.expireAtMs) continue;
			if (ids.size() >= EXPIRE_BATCH) { more = true; break; }
			ids.push_back(id);
		}

		if (!ids.empty())
		{
			// 만료도 혼잡 기준(D-235)에 든다: 꽉 차 있으면 이번에는 보내지 않고 곧 다시 본다.
			if (TryBeginDbWork())
			{
				for (const int64_t id : ids)
					_listings[id].busy = true;
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
		}
		else
		{
			Metrics::AuctionExpired.fetch_add(1);
			// 만료 반환도 판매자에게 알린다(D-262 — 구매와 같은 경로). 목록에서 빼기 전에 종류·수량을 읽는다.
			if (const Listing* listing = Find(id))
			{
				GWorld->PushJob(&WorldService::NotifySeller, listing->sellerUID,
					PacketSerializer::MAKE_SC_MAIL_NOTICE(Protocol::MAIL_KIND_EXPIRED, listing->itemId, listing->count, 0, id));
			}
		}
		RemoveListing(id);
	}
	LOG_DEBUG("Auction", "만료 처리 {}건(요청 {}건)", expired.size(), requested.size());
}
