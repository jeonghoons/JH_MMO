#pragma once
#include <map>
#include <set>
#include <vector>
#include "Data/ServerData.h"

// 메모리의 아이템 한 개(스택이면 한 묶음) = DB Player_Item 한 행.
struct ItemInstance
{
	int64_t dbId = 0;      // Player_Item.ItemDbId
	int32_t itemId = 0;    // ItemTable ID
	int32_t count = 0;
	int32_t equipPart = 0; // 장착 중이면 Protocol::EquipPart(1~7), 인벤토리에 있으면 0
	int32_t slot = -1;     // 인벤토리 칸 번호(0부터, Player_Item.BagSlot). 장착 중이면 -1
};

// 플레이어가 가진 아이템(인벤토리 + 장착)의 메모리 사본 (A3.3, D-153·D-155).
// 권위는 DB다: 소유가 바뀌는 동작은 Room 잡이 여기서 검증·계획하고, DB 트랜잭션이 성공한 뒤에만 여기에 반영한다(Room::OnItemOpDone).
// Room 잡 안에서만 읽고 쓴다(로그인 때는 로비 잡이 채운 뒤 방으로 넘긴다).
//
// 칸 위치(A6.1, D-166): 인벤토리 행은 칸 번호를 갖고 DB에 저장한다(유니크 인덱스 UX_Player_Item_BagSlot).
// 새로 들어오는 행(지급·장착 해제)은 0 ~ 칸 수 - 1에서 가장 앞의 빈 칸에 넣는다. 빈 칸이 없으면 가득 찬 것이다.
// 로그인 보정으로 내려온 행만 칸 수를 넘는 번호를 받을 수 있다(버리지 않기 위해서 — 클라이언트는 넘친 칸도 그린다).
class Inventory
{
public:
	// 넣거나(없던 dbId) 값을 바꾼다(있던 dbId).
	void Set(const ItemInstance& item) { _items[item.dbId] = item; }
	bool Remove(int64_t dbId) { return _items.erase(dbId) > 0; }

	const ItemInstance* Find(int64_t dbId) const
	{
		const auto it = _items.find(dbId);
		return it == _items.end() ? nullptr : &it->second;
	}

	// ItemDbId 순
	const std::map<int64_t, ItemInstance>& Items() const { return _items; }

	// 그 슬롯에 장착한 행의 dbId (없으면 0)
	int64_t GetEquippedDbId(int equipPart) const;

	// 그 인벤토리 칸에 있는 행 (없으면 nullptr)
	const ItemInstance* FindBySlot(int slot) const;
	// 메모리에 올리지 않은 DB 행(ItemTable에 없는 종류)이 차지한 칸. 다른 행을 넣지 않는다.
	void ReserveSlot(int slot) { if (slot >= 0) _reservedSlots.insert(slot); }
	bool IsSlotReserved(int slot) const { return _reservedSlots.count(slot) > 0; }

	// 0 ~ maxSlots - 1에서 빈 칸을 앞에서부터 최대 want개 (부족하면 있는 만큼만)
	std::vector<int> FreeSlots(int maxSlots, int want) const;
	// 상한 없이 가장 앞의 빈 칸(로그인 보정 전용 — 가득 차 있으면 칸 수를 넘는 번호가 나온다)
	int FirstFreeSlotUnbounded() const;

	// 지급·줍기로 들어올 때의 배치 계획: 같은 종류의 덜 찬 스택에 먼저 채우고 남으면 빈 칸에 새 행을 만든다.
	// outNewSlots는 outNewCounts와 같은 순서의 칸 번호. 전부 들어갈 자리가 없으면 false(통째로 거부 — INVENTORY_FULL). 메모리는 바꾸지 않는다.
	bool PlanAdd(int32_t itemId, int64_t count, int32_t maxStack, int32_t maxSlots,
		std::vector<DB_ItemOp::Fill>& outFills, std::vector<int32_t>& outNewCounts, std::vector<int32_t>& outNewSlots) const;

private:
	std::set<int> UsedSlots() const;

	std::map<int64_t, ItemInstance> _items;
	std::set<int> _reservedSlots;
};
