#include "Game/JMInventoryManager.h"

namespace
{
	FJMInventoryItem ToItem(const Protocol::ItemInfo& Info)
	{
		FJMInventoryItem Item;
		Item.ItemDbId = Info.item_db_id();
		Item.ItemId = Info.item_id();
		Item.Count = Info.count();
		Item.EquipPart = static_cast<int32>(Info.equip_part());
		Item.Slot = Info.slot();
		return Item;
	}
}

void UJMInventoryManager::HandleInventory(const Protocol::SC_INVENTORY_PACKET& Pkt)
{
	Items.Reset();
	for (const Protocol::ItemInfo& Info : Pkt.items())
	{
		Items.Add(Info.item_db_id(), ToItem(Info));
	}
	Gold = Pkt.gold();
	MaxSlots = Pkt.max_slots();
	PickupRangeCm = Pkt.pickup_range_cm();
	TradeRangeCm = Pkt.trade_range_cm();
	ShopRangeCm = Pkt.shop_range_cm();
	QuickSlotItems.Reset();
	for (const int32 ItemId : Pkt.quick_slots()) QuickSlotItems.Add(ItemId);
	bReceived = true;

	UE_LOG(LogTemp, Log, TEXT("Inventory: items=%d gold=%lld slots=%d"), Items.Num(), Gold, MaxSlots);
	OnInventoryChanged.Broadcast();
}

void UJMInventoryManager::HandleInventoryUpdate(const Protocol::SC_INVENTORY_UPDATE_PACKET& Pkt)
{
	for (const Protocol::ItemInfo& Info : Pkt.changed())
	{
		Items.Add(Info.item_db_id(), ToItem(Info));
	}
	for (const int64 Removed : Pkt.removed())
	{
		Items.Remove(Removed);
	}
	Gold = Pkt.gold();
	// 액티브 칸 배치는 바뀌었을 때만 실려 온다(5칸 전체).
	if (Pkt.quick_slots_size() > 0)
	{
		QuickSlotItems.Reset();
		for (const int32 ItemId : Pkt.quick_slots()) QuickSlotItems.Add(ItemId);
	}

	UE_LOG(LogTemp, Log, TEXT("InventoryUpdate: changed=%d removed=%d gold=%lld (items=%d)"), Pkt.changed_size(), Pkt.removed_size(), Gold, Items.Num());
	OnInventoryChanged.Broadcast();
}

void UJMInventoryManager::Clear()
{
	Items.Reset();
	QuickSlotItems.Reset();
	Gold = 0;
	bReceived = false;
	OnInventoryChanged.Broadcast();
}

TArray<FJMInventoryItem> UJMInventoryManager::GetSortedItems(bool bEquipped) const
{
	TArray<FJMInventoryItem> Result;
	for (const TPair<int64, FJMInventoryItem>& Pair : Items)
	{
		if ((Pair.Value.EquipPart != 0) == bEquipped)
		{
			Result.Add(Pair.Value);
		}
	}
	Result.Sort([](const FJMInventoryItem& A, const FJMInventoryItem& B) { return A.ItemDbId < B.ItemDbId; });
	return Result;
}

int64 UJMInventoryManager::GetBagItemCount(int32 ItemId) const
{
	int64 Total = 0;
	for (const TPair<int64, FJMInventoryItem>& Pair : Items)
	{
		if (Pair.Value.ItemId == ItemId && Pair.Value.EquipPart == 0) Total += Pair.Value.Count;
	}
	return Total;
}

int64 UJMInventoryManager::FindBagItemDbId(int32 ItemId) const
{
	int64 Found = 0;
	for (const TPair<int64, FJMInventoryItem>& Pair : Items)
	{
		if (Pair.Value.ItemId == ItemId && Pair.Value.EquipPart == 0 && (Found == 0 || Pair.Key < Found))
		{
			Found = Pair.Key;
		}
	}
	return Found;
}
