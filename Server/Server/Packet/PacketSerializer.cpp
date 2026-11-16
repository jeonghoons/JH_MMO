#include "pch.h"
#include "Packet/PacketSerializer.h"
#include "Game/Item/Inventory.h"
#include "Data/ServerData.h"
#include "Game/Object/Player.h"

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

shared_ptr<SendBuffer> PacketSerializer::MAKE_SC_MOVE_OBJECT(shared_ptr<GameObject> object, uint32_t correctionSeq)
{
	Protocol::SC_MOVE_PACKET packet;
	packet.mutable_object_info()->CopyFrom(object->GetInfo());
	packet.set_move_time(object->GetLastMoveTime());
	packet.set_correction_seq(correctionSeq);

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

shared_ptr<SendBuffer> PacketSerializer::MAKE_SC_CHANGE_EQUIP(int objectId, Protocol::EquipPart part, int itemId, const Protocol::StatInfo& stat)
{
	Protocol::SC_CHANGE_EQUIP_PACKET packet;
	packet.set_object_id(objectId);
	packet.set_part(part);
	packet.set_item_id(itemId);
	packet.mutable_stat()->CopyFrom(stat);
	return MakeSendBuffer(packet, Protocol::SC_CHANGE_EQUIP);
}

shared_ptr<SendBuffer> PacketSerializer::MAKE_SC_CHANGE_EQUIP_RESULT(Protocol::EquipResult result, Protocol::EquipPart part, int itemId, int64_t itemDbId)
{
	Protocol::SC_CHANGE_EQUIP_RESULT_PACKET packet;
	packet.set_result(result);
	packet.set_part(part);
	packet.set_item_id(itemId);
	packet.set_item_db_id(itemDbId);
	return MakeSendBuffer(packet, Protocol::SC_CHANGE_EQUIP_RESULT);
}

static void FillItemInfo(Protocol::ItemInfo* out, const ItemInstance& item)
{
	out->set_item_db_id(item.dbId);
	out->set_item_id(item.itemId);
	out->set_count(item.count);
	out->set_equip_part(static_cast<Protocol::EquipPart>(item.equipPart));
	out->set_slot(item.equipPart == 0 ? item.slot : -1);
}

shared_ptr<SendBuffer> PacketSerializer::MAKE_SC_INVENTORY(const Inventory& inventory, int64_t gold, const QuickSlotArray& quickSlots)
{
	const EconomyData& economy = DataManager::GetEconomy();
	Protocol::SC_INVENTORY_PACKET packet;
	for (const auto& [dbId, item] : inventory.Items())
		FillItemInfo(packet.add_items(), item);
	packet.set_gold(gold);
	packet.set_max_slots(economy.inventorySlots);
	packet.set_pickup_range_cm(economy.pickupRangeCm);
	packet.set_trade_range_cm(economy.tradeRangeCm);
	for (const int32_t itemId : quickSlots)
		packet.add_quick_slots(itemId);
	return MakeSendBuffer(packet, Protocol::SC_INVENTORY);
}

shared_ptr<SendBuffer> PacketSerializer::MAKE_SC_USE_ITEM(int objectId, int itemId, int hp, int cooldownMs)
{
	Protocol::SC_USE_ITEM_PACKET packet;
	packet.set_object_id(objectId);
	packet.set_item_id(itemId);
	packet.set_hp(hp);
	packet.set_cooldown_ms(cooldownMs);
	return MakeSendBuffer(packet, Protocol::SC_USE_ITEM);
}

shared_ptr<SendBuffer> PacketSerializer::MAKE_SC_INVENTORY_UPDATE(const vector<ItemInstance>& changed, const vector<int64_t>& removed, int64_t gold, const QuickSlotArray* quickSlots)
{
	Protocol::SC_INVENTORY_UPDATE_PACKET packet;
	for (const ItemInstance& item : changed)
		FillItemInfo(packet.add_changed(), item);
	for (const int64_t dbId : removed)
		packet.add_removed(dbId);
	packet.set_gold(gold);
	if (quickSlots != nullptr)
	{
		for (const int32_t itemId : *quickSlots)
			packet.add_quick_slots(itemId);
	}
	return MakeSendBuffer(packet, Protocol::SC_INVENTORY_UPDATE);
}

shared_ptr<SendBuffer> PacketSerializer::MAKE_SC_ITEM_RESULT(Protocol::PacketId request, Protocol::ItemResult result)
{
	Protocol::SC_ITEM_RESULT_PACKET packet;
	packet.set_request(request);
	packet.set_result(result);
	return MakeSendBuffer(packet, Protocol::SC_ITEM_RESULT);
}

shared_ptr<SendBuffer> PacketSerializer::MAKE_SC_SKILL_AIM(int objectId, int skillId, bool active, uint32_t seq)
{
	Protocol::SC_SKILL_AIM_PACKET packet;
	packet.set_object_id(objectId);
	packet.set_skill_id(skillId);
	packet.set_active(active);
	packet.set_seq(seq);
	return MakeSendBuffer(packet, Protocol::SC_SKILL_AIM);
}

shared_ptr<SendBuffer> PacketSerializer::MAKE_SC_CHAT(int senderId, const wstring& senderName, const wstring& message)
{
	Protocol::SC_CHAT_PACKET packet;
	packet.set_sender_id(senderId);
	packet.set_sender_name(Utils::WStringToUTF8(senderName));
	packet.set_message(Utils::WStringToUTF8(message));

	return MakeSendBuffer(packet, Protocol::SC_CHAT);
}

shared_ptr<SendBuffer> PacketSerializer::MAKE_SC_DEAD(int objectId, int reviveDelayMs)
{
	Protocol::SC_DEAD_PACKET packet;
	packet.set_object_id(objectId);
	packet.set_revive_delay_ms(reviveDelayMs);

	return MakeSendBuffer(packet, Protocol::SC_DEAD);
}

shared_ptr<SendBuffer> PacketSerializer::MAKE_SC_REVIVE(shared_ptr<GameObject> object, uint32_t correctionSeq)
{
	Protocol::SC_REVIVE_PACKET packet;
	packet.set_object_id(object->GetId());
	packet.mutable_pos_info()->CopyFrom(object->GetInfo().position());
	packet.set_hp(object->GetInfo().stat().hp());
	packet.set_correction_seq(correctionSeq);

	return MakeSendBuffer(packet, Protocol::SC_REVIVE);
}

