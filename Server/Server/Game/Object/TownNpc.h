#pragma once
#include "Game/Object/GameObject.h"
#include "Game/World/GameMap.h"
#include "Data/ServerData.h"

// 마을 NPC 하나 (L4, D-202, Docs/DESIGN_ITEMS_TRADE.md "상점 NPC").
// 레벨의 Npc 마커(npcTemplateId) + NpcTable 행으로 만든다. ObjectInfo.template_id = NpcTable ID, name = 표시 이름,
// player_type = 체형, equip_items = 장착 외형(매니퀸 + 기존 장비, D-170). 움직이지 않고 대상이 되지 않는다.
// Room::_npcs에만 들어간다 — Room::_objects(캐릭터 전용: 전투·시야 코드가 Character로 바로 캐스팅함)에는 넣지 않는다.
// 이름이 비슷한 Room::NpcEnterRoom·NPCMove는 몬스터용이다.
class TownNpc : public StaticObject
{
public:
	TownNpc(int id, const NpcData& data, const MapPosition& pos)
		: StaticObject(Protocol::ObjectType::OBJECT_TYPE_NPC), _shopId(data.shopId), _auction(data.auction == 1), _quest(data.hasQuest)
	{
		// 클라이언트가 F 키 안내(상점·경매장·퀘스트)를 가르는 값(D-234). 한 NPC는 하나만 한다(로더가 확인)
		_objectInfo.set_npc_kind(_auction ? Protocol::NPC_KIND_AUCTION : (_shopId != 0 ? Protocol::NPC_KIND_SHOP : (_quest ? Protocol::NPC_KIND_QUEST : Protocol::NPC_KIND_NONE)));
		_objectInfo.set_id(id);
		_objectInfo.set_template_id(data.templateId);
		_objectInfo.set_name(data.displayName);
		_objectInfo.set_player_type(static_cast<Protocol::PlayerType>(data.body));
		for (int i = 0; i < NPC_EQUIP_SLOTS; ++i)
			_objectInfo.add_equip_items(data.equip[i]);
		Protocol::PositionInfo* p = _objectInfo.mutable_position();
		p->set_x(pos.X);
		p->set_y(pos.Y);
		p->set_z(pos.Z);
		p->set_yaw(pos.Yaw);
	}

	int GetTemplateId() const { return _objectInfo.template_id(); }
	// 이 NPC가 여는 상점(ShopTable.ShopId). 0 = 상점 없음
	int GetShopId() const { return _shopId; }
	// 경매 관리인인가(A4.3 — 경매 등록·취소·수령은 이 NPC 근처에서만)
	bool IsAuction() const { return _auction; }
	// 퀘스트 NPC인가(Q1 — QuestTable.NpcId가 이 종류를 가리킨다. 수락·완료는 이 NPC 근처에서만)
	bool IsQuest() const { return _quest; }

private:
	int _shopId = 0;
	bool _auction = false;
	bool _quest = false;
};
