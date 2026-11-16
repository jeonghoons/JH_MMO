#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Protocol/Protocol.pb.h"
#include "JMInventoryManager.generated.h"

// 내 아이템 한 개(스택이면 한 묶음) = 서버 DB Player_Item 한 행 (A3.3)
USTRUCT(BlueprintType)
struct FJMInventoryItem
{
	GENERATED_BODY()

	// 서버가 발급한 아이템 인스턴스 ID. 장착·버리기·거래 요청은 이 값으로 한다
	UPROPERTY(BlueprintReadOnly, Category = "Inventory")
	int64 ItemDbId = 0;

	// ItemTable ID
	UPROPERTY(BlueprintReadOnly, Category = "Inventory")
	int32 ItemId = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Inventory")
	int64 Count = 0;

	// 장착 중인 슬롯(Protocol::EquipPart 1~7). 0 = 인벤토리
	UPROPERTY(BlueprintReadOnly, Category = "Inventory")
	int32 EquipPart = 0;

	// 인벤토리 칸 번호(0부터, 서버가 저장). 장착 중이면 -1 (A6.1)
	UPROPERTY(BlueprintReadOnly, Category = "Inventory")
	int32 Slot = -1;
};

DECLARE_MULTICAST_DELEGATE(FOnJMInventoryChanged);

// 내 인벤토리·장착·골드의 클라이언트 사본 (A3.3).
// 서버가 보낸 값(SC_INVENTORY = 전체, SC_INVENTORY_UPDATE = 바뀐 행)을 그대로 담을 뿐 클라이언트가 계산하지 않는다.
// 인벤토리·장비창(UInventoryWidget)은 OnInventoryChanged에 연결해 다시 그린다.
UCLASS()
class JM_API UJMInventoryManager : public UGameInstanceSubsystem
{
	GENERATED_BODY()
public:
	void HandleInventory(const Protocol::SC_INVENTORY_PACKET& Pkt);
	void HandleInventoryUpdate(const Protocol::SC_INVENTORY_UPDATE_PACKET& Pkt);
	// 로그인 화면으로 돌아갈 때 비운다
	void Clear();

	const TMap<int64, FJMInventoryItem>& GetItems() const { return Items; }
	// ItemDbId 순(= 서버가 보여 주는 순서). bEquipped = true면 장착한 것만, false면 인벤토리 칸의 것만
	TArray<FJMInventoryItem> GetSortedItems(bool bEquipped) const;
	// 인벤토리 칸에 있는(장착하지 않은) 그 종류의 아이템 중 ItemDbId가 가장 작은 것. 없으면 0
	int64 FindBagItemDbId(int32 ItemId) const;
	// 인벤토리 칸에 있는 그 종류의 수량 합계(액티브 칸의 수량 표시, A3.6)
	int64 GetBagItemCount(int32 ItemId) const;
	// 액티브 칸 Index(0부터)에 올린 아이템 종류. 없으면 0. 서버가 저장하는 배치다(SC_INVENTORY·SC_INVENTORY_UPDATE의 quick_slots)
	int32 GetQuickSlotItem(int32 Index) const { return QuickSlotItems.IsValidIndex(Index) ? QuickSlotItems[Index] : 0; }

	int64 GetGold() const { return Gold; }
	int32 GetMaxSlots() const { return MaxSlots; }
	int32 GetPickupRangeCm() const { return PickupRangeCm; }
	int32 GetTradeRangeCm() const { return TradeRangeCm; }
	bool HasInventory() const { return bReceived; }

	// 아이템·골드가 바뀔 때마다(전체 수신 포함)
	FOnJMInventoryChanged OnInventoryChanged;

private:
	UPROPERTY()
	TMap<int64, FJMInventoryItem> Items;

	TArray<int32> QuickSlotItems;

	int64 Gold = 0;
	int32 MaxSlots = 0;
	int32 PickupRangeCm = 0;
	int32 TradeRangeCm = 0;
	bool bReceived = false;
};
