#pragma once


class PacketSerializer
{

public:
	static shared_ptr<SendBuffer> MAKE_SC_ADD_OBJECT(shared_ptr<GameObject> object);
	// 내 캐릭터 입장(본인에게만): ObjectInfo에 본인만 받는 값(경험치 — A2.3b, D-254)을 더한다
	static shared_ptr<SendBuffer> MAKE_SC_ADD_OBJECT_SELF(shared_ptr<class Player> player);

	static shared_ptr<SendBuffer> MAKE_SC_REMOVE_OBJECT(int objectId);

	// correctionSeq: 내 캐릭터 보정일 때 서버의 보정 순번(그 외에는 0)
	static shared_ptr<SendBuffer> MAKE_SC_MOVE_OBJECT(shared_ptr<GameObject> object, uint32_t correctionSeq = 0);

	// seq: 시전자가 보낸 요청 순번(몬스터는 0). 한 버퍼로 AOI 전원에게 가고 시전자 본인만 쓴다 (CP1)
	static shared_ptr<SendBuffer> MAKE_SC_ATTACK(int attackerId, int skillId, int targetId, float aimYaw, float aimX, float aimY, int cooldownMs, uint32_t seq);
	// 스킬·공격 거부(본인에게만). remainMs는 ACTING·COOLDOWN일 때 남은 시간
	static shared_ptr<SendBuffer> MAKE_SC_ATTACK_RESULT(Protocol::AttackResult result, uint32_t seq, int skillSlot, int remainMs);
	
	static shared_ptr<SendBuffer> MAKE_SC_DAMAGE(int attackerId, int targetId, int damage, int remainHp);

	// reviveDelayMs: 플레이어만(이 시간이 지나야 부활 요청을 받는다), 몬스터는 0
	static shared_ptr<SendBuffer> MAKE_SC_DEAD(int objectId, int reviveDelayMs = 0);
	// 부활한 플레이어의 현재 위치·HP를 싣는다. correctionSeq는 본인이 이후 CS_MOVE에 실을 순번
	static shared_ptr<SendBuffer> MAKE_SC_REVIVE(shared_ptr<GameObject> object, uint32_t correctionSeq);
	// 순간이동(L5): object의 지금 위치(이미 옮긴 뒤)와 본인용 보정 순번
	static shared_ptr<SendBuffer> MAKE_SC_TELEPORT(shared_ptr<GameObject> object, uint32_t correctionSeq);
	// 쓸 수 있는 포털 목록(L5). 방 입장 직후 본인에게
	// kinds[i] = portals[i]의 종류(안내 문구 구분, DG3c). 짧으면 나머지는 NORMAL
	static shared_ptr<SendBuffer> MAKE_SC_PORTAL_LIST(const vector<struct PortalTriggerPoint>& portals, const vector<Protocol::PortalKind>& kinds);
	static shared_ptr<SendBuffer> MAKE_SC_DUNGEON_STATE(Protocol::DungeonState state, int remainSec);
	// Room 이동(DG3b): object의 지금 위치(도착 자리로 옮긴 뒤)와 보정 순번, 도착 Room의 레벨·종류·채널
	static shared_ptr<SendBuffer> MAKE_SC_ROOM_CHANGE(shared_ptr<GameObject> object, const string& mapName, Protocol::RoomKind kind, int channel, uint32_t correctionSeq);
	static shared_ptr<SendBuffer> MAKE_SC_MOVE_RESULT(Protocol::MoveResult result);

	static shared_ptr<SendBuffer> MAKE_SC_PONG(long long clientTime);
	static shared_ptr<SendBuffer> MAKE_SC_CHANGE_EQUIP(int objectId, Protocol::EquipPart part, int itemId, const Protocol::StatInfo& stat);
	static shared_ptr<SendBuffer> MAKE_SC_CHANGE_EQUIP_RESULT(Protocol::EquipResult result, Protocol::EquipPart part, int itemId, int64_t itemDbId);
	// 인벤토리(A3.3). 전체는 방 입장 직후 본인에게 한 번, 그 뒤로는 바뀐 행만.
	static shared_ptr<SendBuffer> MAKE_SC_INVENTORY(const class Inventory& inventory, int64_t gold, const QuickSlotArray& quickSlots);
	// quickSlots: 액티브 칸 배치가 바뀌었을 때만 넘긴다(nullptr = 바뀌지 않음)
	static shared_ptr<SendBuffer> MAKE_SC_INVENTORY_UPDATE(const vector<struct ItemInstance>& changed, const vector<int64_t>& removed, int64_t gold, const QuickSlotArray* quickSlots = nullptr);
	// 상점 품목(L4): ShopTable의 그 상점 행들
	static shared_ptr<SendBuffer> MAKE_SC_SHOP_OPEN(int npcId, const vector<struct ShopData>& items);
	// 1:1 거래(A4.2)
	static shared_ptr<SendBuffer> MAKE_SC_TRADE_REQUEST(int fromId, const wstring& fromName, int timeoutSec);
	static shared_ptr<SendBuffer> MAKE_SC_TRADE_STATE(const struct TradeSession& trade);
	static shared_ptr<SendBuffer> MAKE_SC_TRADE_END(Protocol::TradeResult result);
	static shared_ptr<SendBuffer> MAKE_SC_TRADE_RESULT(Protocol::PacketId request, Protocol::TradeResult result);
	// 파티(DG2). 상태 패킷은 WorldService가 채워서 넘긴다(빈 패킷 = 파티 없음)
	static shared_ptr<SendBuffer> MAKE_SC_PARTY_INVITE(int fromId, const wstring& fromName, int timeoutSec);
	static shared_ptr<SendBuffer> MAKE_SC_PARTY_STATE(const Protocol::SC_PARTY_STATE_PACKET& packet);
	static shared_ptr<SendBuffer> MAKE_SC_PARTY_RESULT(Protocol::PacketId request, Protocol::PartyResult result);
	// 소비 아이템 사용 결과(A3.6): 쓴 종류, 사용 뒤 HP, 재사용 시간
	static shared_ptr<SendBuffer> MAKE_SC_USE_ITEM(int objectId, int itemId, int hp, int cooldownMs);
	static shared_ptr<SendBuffer> MAKE_SC_CHANGE_HP(int objectId, int hp);
	// 성장·MP(A2.3b, D-254): 경험치(본인), 레벨업(AOI, 새 스탯 — HP·MP 가득), MP 변화(본인)
	static shared_ptr<SendBuffer> MAKE_SC_GAIN_EXP(int64_t exp, int64_t gained);
	static shared_ptr<SendBuffer> MAKE_SC_LEVEL_UP(int objectId, int level, const Protocol::StatInfo& stat);
	static shared_ptr<SendBuffer> MAKE_SC_CHANGE_MP(int mp);
	static shared_ptr<SendBuffer> MAKE_SC_ITEM_RESULT(Protocol::PacketId request, Protocol::ItemResult result);
	// 경매 요청의 결과(A4.3, 성공·거부 모두). id = 대상 listing_id(등록 성공이면 새 등록, 수령이면 mail_id).
	// 검색 결과·내 목록(SC_AUCTION_LIST·SC_AUCTION_MY)은 AuctionService가 직접 채워 MakeSendBuffer로 만든다.
	static shared_ptr<SendBuffer> MAKE_SC_AUCTION_RESULT(Protocol::PacketId request, Protocol::AuctionResult result, int64_t id);
	// 퀘스트(Q1). 목록 = 그 플레이어가 받은 퀘스트 전부(Player::quests — 처음 입장 때 한 번), 변화 = 퀘스트 하나, 거부 = 요청 패킷 id + 사유
	static shared_ptr<SendBuffer> MAKE_SC_QUEST_LIST(const shared_ptr<class Player>& player);
	static shared_ptr<SendBuffer> MAKE_SC_QUEST_UPDATE(int questId, Protocol::QuestState state, int progress);
	static shared_ptr<SendBuffer> MAKE_SC_QUEST_RESULT(Protocol::PacketId request, int questId, Protocol::QuestResult result);
	// 수령함 알림(D-262): SALE_GOLD(팔린 물건·대금) · EXPIRED(돌아온 물건) · NONE(로그인 때 수령함이 비어 있지 않음 — 나머지 0)
	static shared_ptr<SendBuffer> MAKE_SC_MAIL_NOTICE(Protocol::MailKind kind, int itemId, int64_t count, int64_t gold, int64_t listingId);
	// seq는 본인에게 보내는 거부·시간 초과에만 채운다 (그 밖은 0)
	static shared_ptr<SendBuffer> MAKE_SC_SKILL_AIM(int objectId, int skillId, bool active, uint32_t seq);

	static shared_ptr<SendBuffer> MAKE_SC_CHAT(int senderId, const wstring& senderName, const wstring& message);

public:
	template<typename T>
	static shared_ptr<SendBuffer> MakeSendBuffer(const T& pkt, uint16_t packetId)
	{
		const uint16_t dataSize = static_cast<uint16_t>(pkt.ByteSizeLong());
		const uint16_t packetSize = dataSize + sizeof(PacketHeader);

		shared_ptr<SendBuffer> sendBuffer = make_shared<SendBuffer>(packetSize);

		PacketHeader* header = reinterpret_cast<PacketHeader*>(sendBuffer->Buffer());
		header->size = packetSize;
		header->id = packetId; 

		pkt.SerializeToArray(&header[1], dataSize);
		sendBuffer->Commit(packetSize);

		return sendBuffer;
	}

};

