#include "pch.h"
#include "Packet/PacketSerializer.h"
#include "Game/Item/Inventory.h"
#include "Data/ServerData.h"
#include "Game/Object/Player.h"
#include "Game/Room/Room.h"

shared_ptr<SendBuffer> PacketSerializer::MAKE_SC_ADD_OBJECT(shared_ptr<GameObject> object)
{
	Protocol::SC_ADD_OBJECT_PACKET packet;
	packet.mutable_object_info()->CopyFrom(object->GetInfo());
	return MakeSendBuffer(packet, Protocol::SC_ADD_OBJECT);
}

shared_ptr<SendBuffer> PacketSerializer::MAKE_SC_ADD_OBJECT_SELF(shared_ptr<Player> player)
{
	Protocol::SC_ADD_OBJECT_PACKET packet;
	packet.mutable_object_info()->CopyFrom(player->GetInfo());
	packet.mutable_object_info()->set_exp(player->GetExp());   // 다른 사람에게 가는 ObjectInfo에는 싣지 않는다
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

void PacketSerializer::AppendMoveEntry(const shared_ptr<GameObject>& object, string& out)
{
	const Protocol::ObjectInfo& info = object->GetInfo();
	Protocol::MoveEntry entry;
	entry.set_id(info.id());
	entry.mutable_position()->CopyFrom(info.position());
	entry.set_target_id(info.target_id());
	entry.set_move_time(object->GetLastMoveTime());

	// SC_MOVE_BATCH_PACKET.moves = 필드 1, LEN 형식: 태그(0x0A) + 길이(varint) + 본문
	uint32_t len = static_cast<uint32_t>(entry.ByteSizeLong());
	out.push_back(static_cast<char>(0x0A));
	while (len >= 0x80) { out.push_back(static_cast<char>((len & 0x7F) | 0x80)); len >>= 7; }
	out.push_back(static_cast<char>(len));
	entry.AppendToString(&out);
}

shared_ptr<SendBuffer> PacketSerializer::MAKE_SC_MOVE_BATCH_RAW(const string& body)
{
	const uint16_t packetSize = static_cast<uint16_t>(body.size() + sizeof(PacketHeader));
	shared_ptr<SendBuffer> sendBuffer = make_shared<SendBuffer>(packetSize);
	PacketHeader* header = reinterpret_cast<PacketHeader*>(sendBuffer->Buffer());
	header->size = packetSize;
	header->id = Protocol::SC_MOVE_BATCH;
	memcpy(&header[1], body.data(), body.size());
	sendBuffer->Commit(packetSize);
	return sendBuffer;
}

shared_ptr<SendBuffer> PacketSerializer::MAKE_SC_ATTACK(int attackerId, int skillId, int targetId, float aimYaw, float aimX, float aimY, int cooldownMs, uint32_t seq)
{
	Protocol::SC_ATTACK_PACKET packet;
	packet.set_attacker_id(attackerId);
	packet.set_skill_id(skillId);
	packet.set_target_id(targetId);
	packet.set_aim_yaw(aimYaw);
	packet.set_aim_x(aimX);
	packet.set_aim_y(aimY);
	packet.set_cooldown_ms(cooldownMs);
	packet.set_seq(seq);

	return MakeSendBuffer(packet, Protocol::SC_ATTACK);
}

shared_ptr<SendBuffer> PacketSerializer::MAKE_SC_ATTACK_RESULT(Protocol::AttackResult result, uint32_t seq, int skillSlot, int remainMs)
{
	Protocol::SC_ATTACK_RESULT_PACKET packet;
	packet.set_result(result);
	packet.set_seq(seq);
	packet.set_skill_slot(skillSlot);
	packet.set_remain_ms(remainMs);
	return MakeSendBuffer(packet, Protocol::SC_ATTACK_RESULT);
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
	packet.set_shop_range_cm(economy.shopRangeCm);
	for (const int32_t itemId : quickSlots)
		packet.add_quick_slots(itemId);
	return MakeSendBuffer(packet, Protocol::SC_INVENTORY);
}

shared_ptr<SendBuffer> PacketSerializer::MAKE_SC_SHOP_OPEN(int npcId, const vector<ShopData>& items)
{
	Protocol::SC_SHOP_OPEN_PACKET packet;
	packet.set_npc_id(npcId);
	for (const ShopData& row : items)
	{
		Protocol::ShopItem* out = packet.add_items();
		out->set_item_id(row.itemId);
		out->set_buy_price(row.buyPrice);
	}
	return MakeSendBuffer(packet, Protocol::SC_SHOP_OPEN);
}

shared_ptr<SendBuffer> PacketSerializer::MAKE_SC_TRADE_REQUEST(int fromId, const wstring& fromName, int timeoutSec)
{
	Protocol::SC_TRADE_REQUEST_PACKET packet;
	packet.set_from_id(fromId);
	packet.set_from_name(Utils::WStringToUTF8(fromName));
	packet.set_timeout_sec(timeoutSec);
	return MakeSendBuffer(packet, Protocol::SC_TRADE_REQUEST);
}

shared_ptr<SendBuffer> PacketSerializer::MAKE_SC_TRADE_STATE(const TradeSession& trade)
{
	Protocol::SC_TRADE_STATE_PACKET packet;
	packet.set_phase(trade.phase);
	for (const TradeSide& side : trade.sides)
	{
		Protocol::TradeOffer* offer = packet.add_offers();
		offer->set_object_id(side.objectId);
		for (const TradeItem& item : side.items)
		{
			Protocol::TradeOfferItem* out = offer->add_items();
			out->set_item_db_id(item.itemDbId);
			out->set_item_id(item.itemId);
			out->set_count(item.count);
		}
		offer->set_gold(side.gold);
		offer->set_locked(side.locked);
		offer->set_confirmed(side.confirmed);
	}
	return MakeSendBuffer(packet, Protocol::SC_TRADE_STATE);
}

shared_ptr<SendBuffer> PacketSerializer::MAKE_SC_TRADE_END(Protocol::TradeResult result)
{
	Protocol::SC_TRADE_END_PACKET packet;
	packet.set_result(result);
	return MakeSendBuffer(packet, Protocol::SC_TRADE_END);
}

shared_ptr<SendBuffer> PacketSerializer::MAKE_SC_TRADE_RESULT(Protocol::PacketId request, Protocol::TradeResult result)
{
	Protocol::SC_TRADE_RESULT_PACKET packet;
	packet.set_request(request);
	packet.set_result(result);
	return MakeSendBuffer(packet, Protocol::SC_TRADE_RESULT);
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

shared_ptr<SendBuffer> PacketSerializer::MAKE_SC_CHANGE_HP(int objectId, int hp)
{
	Protocol::SC_CHANGE_HP_PACKET packet;
	packet.set_object_id(objectId);
	packet.set_hp(hp);
	return MakeSendBuffer(packet, Protocol::SC_CHANGE_HP);
}

shared_ptr<SendBuffer> PacketSerializer::MAKE_SC_GAIN_EXP(int64_t exp, int64_t gained)
{
	Protocol::SC_GAIN_EXP_PACKET packet;
	packet.set_exp(exp);
	packet.set_gained(gained);
	return MakeSendBuffer(packet, Protocol::SC_GAIN_EXP);
}

shared_ptr<SendBuffer> PacketSerializer::MAKE_SC_LEVEL_UP(int objectId, int level, const Protocol::StatInfo& stat)
{
	Protocol::SC_LEVEL_UP_PACKET packet;
	packet.set_object_id(objectId);
	packet.set_level(level);
	packet.mutable_stat()->CopyFrom(stat);
	return MakeSendBuffer(packet, Protocol::SC_LEVEL_UP);
}

shared_ptr<SendBuffer> PacketSerializer::MAKE_SC_CHANGE_MP(int mp)
{
	Protocol::SC_CHANGE_MP_PACKET packet;
	packet.set_mp(mp);
	return MakeSendBuffer(packet, Protocol::SC_CHANGE_MP);
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

shared_ptr<SendBuffer> PacketSerializer::MAKE_SC_QUEST_LIST(const shared_ptr<Player>& player)
{
	Protocol::SC_QUEST_LIST_PACKET packet;
	for (const auto& [questId, entry] : player->quests)
	{
		Protocol::QuestInfo* info = packet.add_quests();
		info->set_quest_id(questId);
		info->set_state(entry.state);
		info->set_progress(entry.progress);
	}
	return MakeSendBuffer(packet, Protocol::SC_QUEST_LIST);
}

shared_ptr<SendBuffer> PacketSerializer::MAKE_SC_QUEST_UPDATE(int questId, Protocol::QuestState state, int progress)
{
	Protocol::SC_QUEST_UPDATE_PACKET packet;
	Protocol::QuestInfo* info = packet.mutable_quest();
	info->set_quest_id(questId);
	info->set_state(state);
	info->set_progress(progress);
	return MakeSendBuffer(packet, Protocol::SC_QUEST_UPDATE);
}

shared_ptr<SendBuffer> PacketSerializer::MAKE_SC_QUEST_RESULT(Protocol::PacketId request, int questId, Protocol::QuestResult result)
{
	Protocol::SC_QUEST_RESULT_PACKET packet;
	packet.set_request(request);
	packet.set_quest_id(questId);
	packet.set_result(result);
	return MakeSendBuffer(packet, Protocol::SC_QUEST_RESULT);
}

shared_ptr<SendBuffer> PacketSerializer::MAKE_SC_MAIL_NOTICE(Protocol::MailKind kind, int itemId, int64_t count, int64_t gold, int64_t listingId)
{
	Protocol::SC_MAIL_NOTICE_PACKET packet;
	packet.set_kind(kind);
	packet.set_item_id(itemId);
	packet.set_count(count);
	packet.set_gold(gold);
	packet.set_listing_id(listingId);
	return MakeSendBuffer(packet, Protocol::SC_MAIL_NOTICE);
}

shared_ptr<SendBuffer> PacketSerializer::MAKE_SC_AUCTION_RESULT(Protocol::PacketId request, Protocol::AuctionResult result, int64_t id, int64_t remain)
{
	Protocol::SC_AUCTION_RESULT_PACKET packet;
	packet.set_request(request);
	packet.set_result(result);
	packet.set_id(id);
	packet.set_remain(remain);
	return MakeSendBuffer(packet, Protocol::SC_AUCTION_RESULT);
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

shared_ptr<SendBuffer> PacketSerializer::MAKE_SC_TELEPORT(shared_ptr<GameObject> object, uint32_t correctionSeq)
{
	Protocol::SC_TELEPORT_PACKET packet;
	packet.set_object_id(object->GetId());
	packet.mutable_pos_info()->CopyFrom(object->GetInfo().position());
	packet.set_correction_seq(correctionSeq);

	return MakeSendBuffer(packet, Protocol::SC_TELEPORT);
}

shared_ptr<SendBuffer> PacketSerializer::MAKE_SC_ROOM_CHANGE(shared_ptr<GameObject> object, const string& mapName, Protocol::RoomKind kind, int channel, uint32_t correctionSeq)
{
	Protocol::SC_ROOM_CHANGE_PACKET packet;
	packet.set_map_name(mapName);
	packet.set_kind(kind);
	packet.set_channel(channel);
	packet.mutable_pos_info()->CopyFrom(object->GetInfo().position());
	packet.set_correction_seq(correctionSeq);

	return MakeSendBuffer(packet, Protocol::SC_ROOM_CHANGE);
}

shared_ptr<SendBuffer> PacketSerializer::MAKE_SC_MOVE_RESULT(Protocol::MoveResult result)
{
	Protocol::SC_MOVE_RESULT_PACKET packet;
	packet.set_result(result);

	return MakeSendBuffer(packet, Protocol::SC_MOVE_RESULT);
}

shared_ptr<SendBuffer> PacketSerializer::MAKE_SC_DUNGEON_STATE(Protocol::DungeonState state, int remainSec)
{
	Protocol::SC_DUNGEON_STATE_PACKET packet;
	packet.set_state(state);
	packet.set_remain_sec(remainSec);

	return MakeSendBuffer(packet, Protocol::SC_DUNGEON_STATE);
}

shared_ptr<SendBuffer> PacketSerializer::MAKE_SC_PORTAL_LIST(const vector<PortalTriggerPoint>& portals, const vector<Protocol::PortalKind>& kinds)
{
	Protocol::SC_PORTAL_LIST_PACKET packet;
	for (size_t i = 0; i < portals.size(); ++i)
	{
		const PortalTriggerPoint& portal = portals[i];
		Protocol::PortalInfo* info = packet.add_portals();
		info->set_portal_id(portal.PointId);
		info->set_x(portal.Pos.X);
		info->set_y(portal.Pos.Y);
		info->set_z(portal.Pos.Z);
		info->set_radius_cm(portal.TriggerRadius);
		info->set_kind(i < kinds.size() ? kinds[i] : Protocol::PORTAL_KIND_NORMAL);
	}

	return MakeSendBuffer(packet, Protocol::SC_PORTAL_LIST);
}


shared_ptr<SendBuffer> PacketSerializer::MAKE_SC_PARTY_INVITE(int fromId, const wstring& fromName, int timeoutSec)
{
	Protocol::SC_PARTY_INVITE_PACKET packet;
	packet.set_from_id(fromId);
	packet.set_from_name(Utils::WStringToUTF8(fromName));
	packet.set_timeout_sec(timeoutSec);
	return MakeSendBuffer(packet, Protocol::SC_PARTY_INVITE);
}

shared_ptr<SendBuffer> PacketSerializer::MAKE_SC_PARTY_STATE(const Protocol::SC_PARTY_STATE_PACKET& packet)
{
	return MakeSendBuffer(packet, Protocol::SC_PARTY_STATE);
}

shared_ptr<SendBuffer> PacketSerializer::MAKE_SC_PARTY_RESULT(Protocol::PacketId request, Protocol::PartyResult result)
{
	Protocol::SC_PARTY_RESULT_PACKET packet;
	packet.set_request(request);
	packet.set_result(result);
	return MakeSendBuffer(packet, Protocol::SC_PARTY_RESULT);
}
