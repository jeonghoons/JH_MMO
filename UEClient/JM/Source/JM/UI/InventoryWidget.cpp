#include "UI/InventoryWidget.h"
#include "UI/ItemSlotWidget.h"
#include "UI/JMUIManager.h"
#include "UI/WidgetNames.h"
#include "Components/UniformGridPanel.h"
#include "Components/UniformGridSlot.h"
#include "Components/PanelWidget.h"
#include "Components/TextBlock.h"
#include "Components/Button.h"
#include "Data/ItemTableRow.h"
#include "Game/DataManager.h"
#include "Game/JMInventoryManager.h"
#include "Network/NetworkManager.h"

namespace
{
	constexpr int32 EquipPartCount = 7;   // Protocol::EquipPart 1~7

	// 빈 장착 칸에 보이는 슬롯 이름. 인덱스 = EquipPart - 1
	const TCHAR* const EquipPartLabels[EquipPartCount] = { TEXT("머리"), TEXT("상의"), TEXT("하의"), TEXT("장갑"), TEXT("신발"), TEXT("무기"), TEXT("보조") };
}

void UInventoryWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (BtnClose)
	{
		BtnClose->OnClicked.AddUniqueDynamic(this, &UInventoryWidget::OnCloseClicked);
	}
	if (BtnDrop)
	{
		BtnDrop->OnClicked.AddUniqueDynamic(this, &UInventoryWidget::OnDropClicked);
	}
	BuildEquipSlots();

	if (UGameInstance* GameInstance = GetGameInstance())
	{
		if (UJMInventoryManager* Inventory = GameInstance->GetSubsystem<UJMInventoryManager>())
		{
			InventoryChangedHandle = Inventory->OnInventoryChanged.AddUObject(this, &UInventoryWidget::Refresh);
		}
	}
	// 창이 인벤토리 패킷보다 늦게 만들어져도 현재 값으로 시작한다.
	Refresh();
}

void UInventoryWidget::NativeDestruct()
{
	if (UGameInstance* GameInstance = GetGameInstance())
	{
		if (UJMInventoryManager* Inventory = GameInstance->GetSubsystem<UJMInventoryManager>())
		{
			Inventory->OnInventoryChanged.Remove(InventoryChangedHandle);
		}
	}
	Super::NativeDestruct();
}

UItemSlotWidget* UInventoryWidget::CreateSlot()
{
	if (!ItemSlotClass) return nullptr;

	UItemSlotWidget* ItemSlot = CreateWidget<UItemSlotWidget>(this, ItemSlotClass);
	if (ItemSlot == nullptr) return nullptr;

	ItemSlot->OnClicked.BindUObject(this, &UInventoryWidget::OnSlotClicked);
	ItemSlot->OnRightClicked.BindUObject(this, &UInventoryWidget::OnSlotRightClicked);
	ItemSlot->OnDropped.BindUObject(this, &UInventoryWidget::OnSlotDropped);
	return ItemSlot;
}

void UInventoryWidget::BuildEquipSlots()
{
	if (EquipPanel == nullptr || EquipSlots.Num() > 0) return;

	for (int32 Part = 1; Part <= EquipPartCount; ++Part)
	{
		UItemSlotWidget* ItemSlot = CreateSlot();
		if (ItemSlot == nullptr) return;
		EquipPanel->AddChild(ItemSlot);
		ItemSlot->InitEquipSlot(Part, FText::FromString(EquipPartLabels[Part - 1]));
		EquipSlots.Add(ItemSlot);
	}
}

void UInventoryWidget::EnsureBagSlots(int32 SlotCount)
{
	if (BagGrid == nullptr) return;

	// 칸은 늘리기만 한다(칸 수는 서버 값이라 접속 중에는 바뀌지 않는다. 로그인 보정으로 넘친 칸이 있으면 그만큼 더 그린다).
	while (BagSlots.Num() < SlotCount)
	{
		UItemSlotWidget* ItemSlot = CreateSlot();
		if (ItemSlot == nullptr) return;
		const int32 Index = BagSlots.Num();
		BagGrid->AddChildToUniformGrid(ItemSlot, Index / Columns, Index % Columns);
		ItemSlot->InitBagSlot(Index);
		BagSlots.Add(ItemSlot);
	}
}

void UInventoryWidget::Refresh()
{
	UGameInstance* GameInstance = GetGameInstance();
	UJMInventoryManager* Inventory = GameInstance ? GameInstance->GetSubsystem<UJMInventoryManager>() : nullptr;
	UDataManager* DataManager = GameInstance ? GameInstance->GetSubsystem<UDataManager>() : nullptr;
	if (Inventory == nullptr) return;

	// 칸 번호별·장착 슬롯별로 나눈다
	TMap<int32, const FJMInventoryItem*> BagBySlot;
	const FJMInventoryItem* Equipped[EquipPartCount] = {};
	int32 MaxUsedSlot = -1;
	const FJMInventoryItem* SelectedItem = nullptr;
	for (const TPair<int64, FJMInventoryItem>& Pair : Inventory->GetItems())
	{
		const FJMInventoryItem& Item = Pair.Value;
		if (Item.EquipPart >= 1 && Item.EquipPart <= EquipPartCount)
		{
			Equipped[Item.EquipPart - 1] = &Item;
		}
		else if (Item.EquipPart == 0 && Item.Slot >= 0)
		{
			BagBySlot.Add(Item.Slot, &Item);
			MaxUsedSlot = FMath::Max(MaxUsedSlot, Item.Slot);
		}
		if (Item.ItemDbId == SelectedDbId) SelectedItem = &Item;
	}
	if (SelectedItem == nullptr) SelectedDbId = 0;   // 선택한 아이템이 사라졌다

	EnsureBagSlots(FMath::Max(Inventory->GetMaxSlots(), MaxUsedSlot + 1));

	auto FillSlot = [&](UItemSlotWidget* ItemSlot, const FJMInventoryItem* Item)
	{
		if (ItemSlot == nullptr) return;
		ItemSlot->SetItem(Item, (Item && DataManager) ? DataManager->FindItemById(Item->ItemId) : nullptr);
		ItemSlot->SetSelected(Item != nullptr && Item->ItemDbId == SelectedDbId);
	};
	for (int32 Index = 0; Index < BagSlots.Num(); ++Index)
	{
		const FJMInventoryItem* const* Found = BagBySlot.Find(Index);
		FillSlot(BagSlots[Index], Found ? *Found : nullptr);
	}
	for (int32 Index = 0; Index < EquipSlots.Num(); ++Index)
	{
		FillSlot(EquipSlots[Index], Equipped[Index]);
	}

	// 버리기는 인벤토리 칸의 아이템을 골랐을 때만 누를 수 있다.
	if (BtnDrop) BtnDrop->SetIsEnabled(SelectedItem != nullptr && SelectedItem->EquipPart == 0);

	if (TxtGold) TxtGold->SetText(FText::AsNumber(Inventory->GetGold()));
	if (TxtSlots) TxtSlots->SetText(FText::FromString(FString::Printf(TEXT("%d / %d"), BagBySlot.Num(), Inventory->GetMaxSlots())));
	if (TxtSelected)
	{
		FText Text;
		if (SelectedItem)
		{
			const FItemTableRow* Row = DataManager ? DataManager->FindItemById(SelectedItem->ItemId) : nullptr;
			const FString Name = Row ? Row->DisplayName.ToString() : FString::FromInt(SelectedItem->ItemId);
			Text = FText::FromString(SelectedItem->Count >= 2 ? FString::Printf(TEXT("%s x%lld"), *Name, SelectedItem->Count) : Name);
		}
		TxtSelected->SetText(Text);
	}
}

void UInventoryWidget::OnSlotClicked(UItemSlotWidget* ItemSlot)
{
	if (ItemSlot == nullptr) return;
	// 빈 칸을 누르면 선택을 푼다
	SelectedDbId = ItemSlot->GetItemDbId();
	Refresh();
}

void UInventoryWidget::OnSlotRightClicked(UItemSlotWidget* ItemSlot)
{
	if (ItemSlot == nullptr || !ItemSlot->HasItem()) return;
	UNetworkManager* NetManager = UNetworkManager::Get(this);
	if (NetManager == nullptr) return;

	if (ItemSlot->IsEquipSlot())
	{
		NetManager->SendChangeEquip(ItemSlot->GetEquipPart(), 0);   // 해제
		return;
	}

	// 인벤토리의 장비: 그 아이템의 슬롯에 장착. 소비 아이템: 그 행에서 하나 사용(A3.6).
	UDataManager* DataManager = GetGameInstance() ? GetGameInstance()->GetSubsystem<UDataManager>() : nullptr;
	const FItemTableRow* Row = DataManager ? DataManager->FindItemById(ItemSlot->GetItemId()) : nullptr;
	if (Row == nullptr) return;
	if (Row->Slot == EEquipSlot::None)
	{
		if (Row->UseEffect != EItemUseEffect::None) NetManager->SendUseItem(ItemSlot->GetItemDbId(), 0);
		return;
	}
	NetManager->SendChangeEquip(static_cast<int32>(Row->Slot), ItemSlot->GetItemDbId());
}

void UInventoryWidget::OnSlotDropped(UItemSlotWidget* From, UItemSlotWidget* To)
{
	if (From == nullptr || To == nullptr || !From->HasItem()) return;
	UNetworkManager* NetManager = UNetworkManager::Get(this);
	if (NetManager == nullptr) return;

	// 검증은 모두 서버가 한다(슬롯이 맞는지, 빈 칸이 있는지 등). 여기서는 어떤 요청인지만 고른다.
	if (!From->IsEquipSlot() && !To->IsEquipSlot())
	{
		// 인벤토리 → 인벤토리: 칸 옮기기(대상에 아이템이 있으면 서버가 맞바꾼다)
		NetManager->SendMoveItem(From->GetItemDbId(), To->GetSlotIndex());
	}
	else if (!From->IsEquipSlot() && To->IsEquipSlot())
	{
		// 인벤토리 → 장착 칸: 그 슬롯에 장착
		NetManager->SendChangeEquip(To->GetEquipPart(), From->GetItemDbId());
	}
	else if (From->IsEquipSlot() && !To->IsEquipSlot())
	{
		// 장착 칸 → 인벤토리: 대상 칸에 같은 슬롯의 장비가 있으면 그것으로 교체(옛 장비가 그 칸으로 내려온다), 아니면 해제(앞의 빈 칸으로).
		UDataManager* DataManager = GetGameInstance() ? GetGameInstance()->GetSubsystem<UDataManager>() : nullptr;
		const FItemTableRow* TargetRow = (To->HasItem() && DataManager) ? DataManager->FindItemById(To->GetItemId()) : nullptr;
		const bool bSwap = TargetRow != nullptr && static_cast<int32>(TargetRow->Slot) == From->GetEquipPart();
		NetManager->SendChangeEquip(From->GetEquipPart(), bSwap ? To->GetItemDbId() : 0);
	}
}

void UInventoryWidget::OnDropClicked()
{
	if (SelectedDbId == 0) return;
	// 요청만 보낸다. 서버가 행을 지운 뒤 SC_INVENTORY_UPDATE(칸에서 사라짐)와 SC_ADD_OBJECT(발밑 드랍)를 보낸다. 거부는 시스템 메시지.
	if (UNetworkManager* NetManager = UNetworkManager::Get(this))
	{
		NetManager->SendDropItem(SelectedDbId);
	}
}

void UInventoryWidget::OnCloseClicked()
{
	if (UGameInstance* GameInstance = GetGameInstance())
	{
		if (UJMUIManager* UIManager = GameInstance->GetSubsystem<UJMUIManager>())
		{
			UIManager->SetWidgetVisible(WidgetNames::Inventory, false);
		}
	}
}
