#include "pch.h"
#include "PacketSerializer.h"
#include "Player.h"

shared_ptr<SendBuffer> PacketSerializer::MAKE_SC_ADD_OBJECT(shared_ptr<GameObject> object)
{
	Protocol::SC_ADD_OBJECT_PACKET packet;
	packet.mutable_object_info()->CopyFrom(object->GetInfo());
	return MakeSendBuffer(packet, Protocol::SC_ADD_OBJECT);
}

shared_ptr<SendBuffer> PacketSerializer::MAKE_SC_REMOVE_OBJECT(int objectId)
{
	Protocol::SC_REMOVE_OBJECT_PACKET packet; 
	packet.set_object_id(objectId);
	return MakeSendBuffer(packet, Protocol::SC_REMOVE_OBJECT);
}

shared_ptr<SendBuffer> PacketSerializer::MAKE_SC_MOVE_OBJECT(shared_ptr<GameObject> object)
{
	Protocol::SC_MOVE_PACKET packet;
	packet.mutable_object_info()->CopyFrom(object->GetInfo());
	packet.set_move_time(object->GetLastMoveTime());

	return MakeSendBuffer(packet, Protocol::SC_MOVE_OBJECT);
}

shared_ptr<SendBuffer> PacketSerializer::MAKE_SC_ATTACK(int attackerId, int skillId, int targetId, float aimYaw, float aimX, float aimY, int cooldownMs)
{
	Protocol::SC_ATTACK_PACKET packet;
	packet.set_attacker_id(attackerId);
	packet.set_skill_id(skillId);
	packet.set_target_id(targetId);
	packet.set_aim_yaw(aimYaw);
	packet.set_aim_x(aimX);
	packet.set_aim_y(aimY);
	packet.set_cooldown_ms(cooldownMs);

	return MakeSendBuffer(packet, Protocol::SC_ATTACK);
}

shared_ptr<SendBuffer> PacketSerializer::MAKE_SC_DAMAGE(int attackerId, int targetId, int damage, int remainHp)
{
	Protocol::SC_DAMAGE_PACKET packet;
	packet.set_attacker_id(attackerId);
	packet.set_target_id(targetId);
	packet.set_damage(damage);
	packet.set_remain_hp(remainHp);

	return MakeSendBuffer(packet, Protocol::SC_DAMAGE);
}

shared_ptr<SendBuffer> PacketSerializer::MAKE_SC_PONG(long long clientTime)
{
	Protocol::SC_PONG_PACKET packet;
	packet.set_client_time(clientTime);

	return MakeSendBuffer(packet, Protocol::SC_PONG);
}

shared_ptr<SendBuffer> PacketSerializer::MAKE_SC_CHAT(int senderId, const wstring& senderName, const wstring& message)
{
	Protocol::SC_CHAT_PACKET packet;
	packet.set_sender_id(senderId);
	packet.set_sender_name(Utils::WStringToUTF8(senderName));
	packet.set_message(Utils::WStringToUTF8(message));

	return MakeSendBuffer(packet, Protocol::SC_CHAT);
}

shared_ptr<SendBuffer> PacketSerializer::MAKE_SC_DEAD(int objectId)
{
	Protocol::SC_DEAD_PACKET packet;
	packet.set_object_id(objectId);

	return MakeSendBuffer(packet, Protocol::SC_DEAD);
}

