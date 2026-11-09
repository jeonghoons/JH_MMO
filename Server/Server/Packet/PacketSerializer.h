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

	static shared_ptr<SendBuffer> MAKE_SC_DEAD(int objectId);

	static shared_ptr<SendBuffer> MAKE_SC_PONG(long long clientTime);
	static shared_ptr<SendBuffer> MAKE_SC_CHANGE_EQUIP(int objectId, Protocol::EquipPart part, int itemId, const Protocol::StatInfo& stat);
	static shared_ptr<SendBuffer> MAKE_SC_CHANGE_EQUIP_RESULT(Protocol::EquipResult result, Protocol::EquipPart part, int itemId);
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

