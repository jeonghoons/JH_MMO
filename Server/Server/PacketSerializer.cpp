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

shared_ptr<SendBuffer> PacketSerializer::MAKE_SC_ATTACK(int attackerId, int skillId, int targetId)
{
	Protocol::SC_ATTACK_PACKET packet;
	packet.set_attacker_id(attackerId);
	packet.set_skill_id(skillId);
	packet.set_target_id(targetId);

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

shared_ptr<SendBuffer> PacketSerializer::MAKE_SC_DEAD(int objectId)
{
	Protocol::SC_DEAD_PACKET packet;
	packet.set_object_id(objectId);

	return MakeSendBuffer(packet, Protocol::SC_DEAD);
}

