#pragma once
#include "Game/Object/GameObject.h"

// 바닥에 떨어진 아이템·골드 하나 (A3.4, D-157, Docs/DESIGN_ITEMS_TRADE.md "드랍·줍기·버리기").
// ObjectInfo.template_id = ItemTable ID(0 = 골드), item_count = 수량(골드는 금액). 움직이지 않고 DB에 없다(서버를 재시작하면 사라짐).
// Room::_drops에만 들어간다 — Room::_objects(캐릭터 전용: 전투·시야 코드가 Character로 바로 캐스팅함)에는 넣지 않는다.
// Room 잡 안에서만 읽고 쓴다.
class DropItem : public StaticObject
{
public:
	DropItem(int id, int itemId, int64_t count, const Protocol::PositionInfo& pos)
		: StaticObject(Protocol::ObjectType::OBJECT_TYPE_ITEM)
	{
		_objectInfo.set_id(id);
		_objectInfo.set_template_id(itemId);
		_objectInfo.set_item_count(count);
		Protocol::PositionInfo* p = _objectInfo.mutable_position();
		p->set_x(pos.x());
		p->set_y(pos.y());
		p->set_z(pos.z());
	}

	int GetItemId() const { return _objectInfo.template_id(); }
	int64_t GetCount() const { return _objectInfo.item_count(); }
	bool IsGold() const { return GetItemId() == 0; }

	// 소유권: 막타 플레이어의 PlayerId(Player::GetPlayerUID)와 만료 시각(steady_clock ms). 0 = 소유권 없음(누구나 주움).
	// 만료는 타이머가 아니라 줍기 요청 때 시각 비교로 판정한다.
	// 파티 공유(DG2, D-217): 막타 플레이어가 파티 중이었으면 그 파티 번호도 적는다. 소유 시간 동안 주울 수 있는 사람 =
	// 막타 본인 또는 "줍는 순간" 같은 파티 번호인 사람(드랍 뒤에 파티에 든 사람은 되고, 떠난 사람은 안 됨).
	int64_t ownerUID = 0;
	int ownerPartyId = 0;
	long long ownerUntilMs = 0;
	bool CanPickup(int64_t playerUID, int partyId, long long nowMs) const
	{
		return ownerUID == 0 || ownerUID == playerUID || (ownerPartyId != 0 && ownerPartyId == partyId) || nowMs >= ownerUntilMs;
	}

	// 사라지는 시각(steady_clock ms). 소멸 잡(Room::ExpireDrop)이 이 값으로 다시 확인한다(id가 다시 쓰였을 때 새 드랍을 지우지 않게).
	long long expireAtMs = 0;

	// 줍기 DB 작업이 진행 중이다(A3.5). 이 동안에는 소멸·상한 제거를 건너뛴다.
	bool picking = false;
};
