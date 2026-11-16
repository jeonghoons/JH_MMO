#include "pch.h"
#include "Game/Item/Inventory.h"

int64_t Inventory::GetEquippedDbId(int equipPart) const
{
	if (equipPart == 0) return 0;
	for (const auto& [dbId, item] : _items)
	{
		if (item.equipPart == equipPart) return dbId;
	}
	return 0;
}

const ItemInstance* Inventory::FindBySlot(int slot) const
{
	if (slot < 0) return nullptr;
	for (const auto& [dbId, item] : _items)
	{
		if (item.equipPart == 0 && item.slot == slot) return &item;
	}
	return nullptr;
}

std::set<int> Inventory::UsedSlots() const
{
	std::set<int> used = _reservedSlots;
	for (const auto& [dbId, item] : _items)
	{
		if (item.equipPart == 0 && item.slot >= 0) used.insert(item.slot);
	}
	return used;
}

std::vector<int> Inventory::FreeSlots(int maxSlots, int want) const
{
	std::vector<int> out;
	if (want <= 0) return out;
	const std::set<int> used = UsedSlots();
	for (int slot = 0; slot < maxSlots && static_cast<int>(out.size()) < want; ++slot)
	{
		if (used.count(slot) == 0) out.push_back(slot);
	}
	return out;
}

int Inventory::FirstFreeSlotUnbounded() const
{
	const std::set<int> used = UsedSlots();
	int slot = 0;
	while (used.count(slot) > 0) ++slot;
	return slot;
}

bool Inventory::PlanAdd(int32_t itemId, int64_t count, int32_t maxStack, int32_t maxSlots,
	std::vector<DB_ItemOp::Fill>& outFills, std::vector<int32_t>& outNewCounts, std::vector<int32_t>& outNewSlots) const
{
	outFills.clear();
	outNewCounts.clear();
	outNewSlots.clear();
	if (count <= 0 || maxStack <= 0 || maxSlots <= 0) return false;
	// 인벤토리 전체보다 많으면 어차피 들어가지 않는다(아래 계산이 큰 값에서 넘치지 않게 먼저 거른다).
	if (count > static_cast<int64_t>(maxStack) * maxSlots) return false;

	int64_t remain = count;

	// 1) 덜 찬 스택(인벤토리 위치만)에 ItemDbId 순으로 채운다.
	if (maxStack > 1)
	{
		for (const auto& [dbId, item] : _items)
		{
			if (remain <= 0) break;
			if (item.equipPart != 0 || item.itemId != itemId || item.count >= maxStack) continue;
			const int32_t add = static_cast<int32_t>(std::min<int64_t>(remain, maxStack - item.count));
			outFills.push_back({ dbId, item.count, add });
			remain -= add;
		}
	}

	// 2) 남은 만큼 새 행을 앞의 빈 칸부터. 빈 칸이 모자라면 거부한다(위에서 수량을 걸렀으므로 needRows ≤ maxSlots).
	if (remain > 0)
	{
		const int needRows = static_cast<int>(remain / maxStack + (remain % maxStack != 0 ? 1 : 0));
		const std::vector<int> freeSlots = FreeSlots(maxSlots, needRows);
		if (static_cast<int>(freeSlots.size()) < needRows)
		{
			outFills.clear();
			return false;
		}
		for (int i = 0; i < needRows; ++i)
		{
			const int32_t rowCount = static_cast<int32_t>(std::min<int64_t>(remain, maxStack));
			outNewCounts.push_back(rowCount);
			outNewSlots.push_back(freeSlots[i]);
			remain -= rowCount;
		}
	}
	return true;
}
