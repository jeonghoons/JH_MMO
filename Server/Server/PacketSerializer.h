#pragma once


class PacketSerializer
{

public:
	static shared_ptr<SendBuffer> MAKE_SC_ADD_OBJECT(shared_ptr<GameObject> object);

	static shared_ptr<SendBuffer> MAKE_SC_REMOVE_OBJECT(int objectId);

	static shared_ptr<SendBuffer> MAKE_SC_MOVE_OBJECT(shared_ptr<GameObject> object);

	static shared_ptr<SendBuffer> MAKE_SC_ATTACK(int attackerId, int skillId, int targetId);
	
	static shared_ptr<SendBuffer> MAKE_SC_DAMAGE(int attackerId, int targetId, int damage, int remainHp);

	static shared_ptr<SendBuffer> MAKE_SC_DEAD(int objectId);

public:
	template<typename T>
	static shared_ptr<SendBuffer> MakeSendBuffer(const T& pkt, uint16_t packetId)
	{
		const uint16_t dataSize = static_cast<uint16_t>(pkt.ByteSizeLong());
		const uint16_t packetSize = dataSize + sizeof(PacketHeader);

		shared_ptr<SendBuffer> sendBuffer = make_shared<SendBuffer>(packetSize);

		PacketHeader* header = reinterpret_cast<PacketHeader*>(sendBuffer->Buffer());
		header->size = packetSize;
		header->id = packetId; // (만약 멤버명이 type이라면 header->type 으로 변경하세요)

		// 임시 버퍼 없이 SendBuffer에 직접 직렬화
		pkt.SerializeToArray(&header[1], dataSize);
		sendBuffer->Commit(packetSize);

		return sendBuffer;
	}

};

