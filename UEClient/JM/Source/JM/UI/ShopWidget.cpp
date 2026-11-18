#include "UI/ShopWidget.h"
#include "UI/ItemSlotWidget.h"
#include "Components/PanelWidget.h"
#include "Components/TextBlock.h"
#include "Components/Button.h"
#include "Components/EditableTextBox.h"
#include "Data/ItemTableRow.h"
#include "Game/DataManager.h"
#include "Game/JMInventoryManager.h"
#include "Game/JMShopManager.h"

UJMShopManager* UShopWidget::GetShopManager() const
{
	const UGameInstance* GameInstance = GetGameInstance();
	return GameInstance ? GameInstance->GetSubsystem<UJMShopManager>() : nullptr;
}

void UShopWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (BtnBuy) BtnBuy->OnClicked.AddUniqueDynamic(this, &UShopWidget::OnBuyClicked);
	if (BtnSell) BtnSell->OnClicked.AddUniqueDynamic(this, &UShopWidget::OnSellClicked);
	if (BtnClose) BtnClose->OnClicked.AddUniqueDynamic(this, &UShopWidget::OnCloseClicked);
	if (EditBuyCount) EditBuyCount->OnTextChanged.AddUniqueDynamic(this, &UShopWidget::OnCountChanged);
	if (EditSellCount) EditSellCount->OnTextChanged.AddUniqueDynamic(this, &UShopWidget::OnCountChanged);

	if (UJMShopManager* Shop = GetShopManager())
	{
		ShopChangedHandle = Shop->OnShopChanged.AddUObject(this, &UShopWidget::OnShopChanged);
	}
	if (UJMInventoryManager* Inventory = GetGameInstance() ? GetGameInstance()->GetSubsystem<UJMInventoryManager>() : nullptr)
	{
		InventoryChangedHandle = Inventory->OnInventoryChanged.AddUObject(this, &UShopWidget::Refresh);
	}
	OnShopChanged();
}

void UShopWidget::NativeDestruct()
{
	if (UJMShopManager* Shop = GetShopManager())
	{
		Shop->OnShopChanged.Remove(ShopChangedHandle);
	}
	if (UJMInventoryManager* Inventory = GetGameInstance() ? GetGameInstance()->GetSubsystem<UJMInventoryManager>() : nullptr)
	{
		Inventory->OnInventoryChanged.Remove(InventoryChangedHandle);
	}
	Super::NativeDestruct();
}

int32 UShopWidget::ReadCount(const UEditableTextBox* Box) const
{
	if (Box == nullptr) return 1;
	const FString Text = Box->GetText().ToString().TrimStartAndEnd();
	if (!Text.IsNumeric()) return 0;
	// 서버 수량 상한(MaxStack × 칸 수)보다 큰 값은 어차피 거부된다. 표시 계산이 넘치지 않게만 막는다.
	return static_cast<int32>(FMath::Clamp<int64>(FCString::Atoi64(*Text), 0, 100000));
}

void UShopWidget::OnShopChanged()
{
	const UJMShopManager* Shop = GetShopManager();
	if (Shop == nullptr) return;

	// 고른 품목이 이 상점에 없으면(다른 상점을 열었거나 닫힘) 선택을 푼다.
	const bool bStillSold = Shop->GetEntries().ContainsByPredicate([this](const FJMShopEntry& Entry) { return Entry.ItemId == SelectedItemId; });
	if (!bStillSold)
	{
		SelectedItemId = 0;
		if (EditBuyCount) EditBuyCount->SetText(FText::FromString(TEXT("1")));
	}
	Refresh();
}

void UShopWidget::Refresh()
{
	const UJMShopManager* Shop = GetShopManager();
	const UGameInstance* GameInstance = GetGameInstance();
	if (Shop == nullptr || GameInstance == nullptr) return;
	const UDataManager* DataManager = GameInstance->GetSubsystem<UDataManager>();
	const UJMInventoryManager* Inventory = GameInstance->GetSubsystem<UJMInventoryManager>();

	if (TxtTitle) TxtTitle->SetText(FText::FromString(Shop->GetNpcName().IsEmpty() ? FString(TEXT("상점")) : Shop->GetNpcName()));
	if (TxtGold) TxtGold->SetText(FText::FromString(FString::Printf(TEXT("%lld G"), Inventory ? Inventory->GetGold() : 0)));

	// ---- 파는 품목: 칸의 숫자 = 1개 값 ----
	const TArray<FJMShopEntry>& Entries = Shop->GetEntries();
	if (BuyItems && ItemSlotClass)
	{
		while (BuySlots.Num() < Entries.Num())
		{
			UItemSlotWidget* ItemSlot = CreateWidget<UItemSlotWidget>(this, ItemSlotClass);
			if (ItemSlot == nullptr) break;
			BuyItems->AddChild(ItemSlot);
			ItemSlot->InitBagSlot(BuySlots.Num());
			ItemSlot->SetDragEnabled(false);   // 상점 칸은 끌 수 없다(인벤토리 창이 다른 요청으로 읽지 않게)
			ItemSlot->OnClicked.BindUObject(this, &UShopWidget::OnBuySlotClicked);
			BuySlots.Add(ItemSlot);
		}
		for (int32 Index = 0; Index < BuySlots.Num(); ++Index)
		{
			UItemSlotWidget* ItemSlot = BuySlots[Index];
			if (ItemSlot == nullptr) continue;
			if (!Entries.IsValidIndex(Index))
			{
				ItemSlot->SetItem(nullptr, nullptr);
				ItemSlot->SetSelected(false);
				ItemSlot->SetVisibility(ESlateVisibility::Collapsed);
				continue;
			}
			// 칸 위젯은 내 아이템용이라 ItemDbId가 있어야 "아이템 있음"으로 그린다. 상점 칸은 품목 ID를 대신 넣는다(요청에는 쓰지 않음).
			FJMInventoryItem Shown;
			Shown.ItemDbId = Entries[Index].ItemId;
			Shown.ItemId = Entries[Index].ItemId;
			Shown.Count = Entries[Index].BuyPrice;
			ItemSlot->SetItem(&Shown, DataManager ? DataManager->FindItemById(Entries[Index].ItemId) : nullptr);
			ItemSlot->SetSelected(SelectedItemId == Entries[Index].ItemId);
			ItemSlot->SetVisibility(ESlateVisibility::Visible);
		}
	}

	// ---- 고른 품목 ----
	const FJMShopEntry* Selected = Entries.FindByPredicate([this](const FJMShopEntry& Entry) { return Entry.ItemId == SelectedItemId; });
	const int32 BuyCount = ReadCount(EditBuyCount);
	if (TxtBuyInfo)
	{
		if (Selected == nullptr)
		{
			TxtBuyInfo->SetText(FText::FromString(TEXT("살 물건을 고르세요 (칸의 숫자 = 1개 값)")));
		}
		else
		{
			const FItemTableRow* Row = DataManager ? DataManager->FindItemById(Selected->ItemId) : nullptr;
			const FString Name = Row ? Row->DisplayName.ToString() : FString::FromInt(Selected->ItemId);
			TxtBuyInfo->SetText(FText::FromString(FString::Printf(TEXT("구매: %s  %lld G × %d = %lld G"), *Name, Selected->BuyPrice, BuyCount, Selected->BuyPrice * BuyCount)));
		}
	}
	if (BtnBuy) BtnBuy->SetIsEnabled(Selected != nullptr && BuyCount >= 1);

	// ---- 판매 후보(인벤토리에서 우클릭한 행) ----
	const FJMInventoryItem* SellItem = nullptr;
	if (Inventory && Shop->GetSellCandidate() != 0)
	{
		SellItem = Inventory->GetItems().Find(Shop->GetSellCandidate());
		if (SellItem && SellItem->EquipPart != 0) SellItem = nullptr;   // 그 사이 장착했다
	}
	const FItemTableRow* SellRow = (SellItem && DataManager) ? DataManager->FindItemById(SellItem->ItemId) : nullptr;
	// 후보가 바뀌면 수량 칸을 그 행의 전체 수량으로 맞춘다.
	const int64 CandidateId = SellItem ? SellItem->ItemDbId : 0;
	if (CandidateId != SellCountFor)
	{
		SellCountFor = CandidateId;
		if (EditSellCount) EditSellCount->SetText(FText::FromString(SellItem ? FString::Printf(TEXT("%lld"), SellItem->Count) : FString(TEXT("1"))));
	}
	const int32 SellCount = ReadCount(EditSellCount);
	const int32 SellPrice = SellRow ? SellRow->SellPrice : 0;
	if (TxtSellInfo)
	{
		if (SellItem == nullptr)
		{
			TxtSellInfo->SetText(FText::FromString(TEXT("팔 아이템을 인벤토리에서 우클릭하세요")));
		}
		else
		{
			const FString Name = SellRow ? SellRow->DisplayName.ToString() : FString::FromInt(SellItem->ItemId);
			if (SellPrice <= 0)
				TxtSellInfo->SetText(FText::FromString(FString::Printf(TEXT("판매: %s — 팔 수 없는 아이템입니다"), *Name)));
			else
				TxtSellInfo->SetText(FText::FromString(FString::Printf(TEXT("판매: %s  %d G × %d / %lld개 = %lld G"), *Name, SellPrice, SellCount, SellItem->Count, static_cast<int64>(SellPrice) * SellCount)));
		}
	}
	if (BtnSell) BtnSell->SetIsEnabled(SellItem != nullptr && SellPrice > 0 && SellCount >= 1 && SellCount <= SellItem->Count);
}

void UShopWidget::OnBuySlotClicked(UItemSlotWidget* ItemSlot)
{
	if (ItemSlot == nullptr) return;
	SelectedItemId = ItemSlot->HasItem() ? ItemSlot->GetItemId() : 0;
	Refresh();
}

void UShopWidget::OnBuyClicked()
{
	// 골드·빈 칸·거리 검증은 서버가 하고 거부는 시스템 메시지로 온다.
	if (UJMShopManager* Shop = GetShopManager()) Shop->Buy(SelectedItemId, ReadCount(EditBuyCount));
}

void UShopWidget::OnSellClicked()
{
	if (UJMShopManager* Shop = GetShopManager()) Shop->SellSelected(ReadCount(EditSellCount));
}

void UShopWidget::OnCloseClicked()
{
	if (UJMShopManager* Shop = GetShopManager()) Shop->Close();
}

void UShopWidget::OnCountChanged(const FText& Text)
{
	Refresh();
}
