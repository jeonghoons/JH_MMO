#pragma once


class PacketSerializer
{

public:
	static shared_ptr<SendBuffer> MAKE_SC_ADD_OBJECT(shared_ptr<GameObject> object);

	static shared_ptr<SendBuffer> MAKE_SC_REMOVE_OBJECT(int objectId);

	// correctionSeq: 내 캐릭터 보정일 때 서버의 보정 순번(그 외에는 0)
	static shared_ptr<SendBuffer> MAKE_SC_MOVE_OBJECT(shared_ptr<GameObject> object, uint32_t correctionSeq = 0);

	static shared_ptr<SendBuffer> MAKE_SC_ATTACK(int attackerId, int skillId, int targetId, float aimYaw, float aimX, float aimY, int cooldownMs);
	
	static shared_ptr<SendBuffer> MAKE_SC_DAMAGE(int attackerId, int targetId, int damage, int remainHp);

	// reviveDelayMs: 플레이어만(이 시간이 지나야 부활 요청을 받는다), 몬스터는 0
	static shared_ptr<SendBuffer> MAKE_SC_DEAD(int objectId, int reviveDelayMs = 0);
	// 부활한 플레이어의 현재 위치·HP를 싣는다. correctionSeq는 본인이 이후 CS_MOVE에 실을 순번
	static shared_ptr<SendBuffer> MAKE_SC_REVIVE(shared_ptr<GameObject> object, uint32_t correctionSeq);

	static shared_ptr<SendBuffer> MAKE_SC_PONG(long long clientTime);
	static shared_ptr<SendBuffer> MAKE_SC_CHANGE_EQUIP(int objectId, Protocol::EquipPart part, int itemId, const Protocol::StatInfo& stat);
	static shared_ptr<SendBuffer> MAKE_SC_CHANGE_EQUIP_RESULT(Protocol::EquipResult result, Protocol::EquipPart part, int itemId, int64_t itemDbId);
	// 인벤토리(A3.3). 전체는 방 입장 직후 본인에게 한 번, 그 뒤로는 바뀐 행만.
	static shared_ptr<SendBuffer> MAKE_SC_INVENTORY(const class Inventory& inventory, int64_t gold, const QuickSlotArray& quickSlots);
	// quickSlots: 액티브 칸 배치가 바뀌었을 때만 넘긴다(nullptr = 바뀌지 않음)
	static shared_ptr<SendBuffer> MAKE_SC_INVENTORY_UPDATE(const vector<struct ItemInstance>& changed, const vector<int64_t>& removed, int64_t gold, const QuickSlotArray* quickSlots = nullptr);
	// 소비 아이템 사용 결과(A3.6): 쓴 종류, 사용 뒤 HP, 재사용 시간
	static shared_ptr<SendBuffer> MAKE_SC_USE_ITEM(int objectId, int itemId, int hp, int cooldownMs);
	static shared_ptr<SendBuffer> MAKE_SC_ITEM_RESULT(Protocol::PacketId request, Protocol::ItemResult result);
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

