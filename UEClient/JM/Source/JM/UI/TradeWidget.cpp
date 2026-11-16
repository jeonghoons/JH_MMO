#include "UI/TradeWidget.h"
#include "UI/ItemSlotWidget.h"
#include "Blueprint/DragDropOperation.h"
#include "Components/PanelWidget.h"
#include "Components/TextBlock.h"
#include "Components/Button.h"
#include "Components/EditableTextBox.h"
#include "Data/ItemTableRow.h"
#include "Game/DataManager.h"
#include "Game/JMInventoryManager.h"
#include "Game/JMTradeManager.h"

namespace
{
	FText OfferStatusText(const FJMTradeOffer& Offer, bool bConfirming)
	{
		if (bConfirming) return FText::FromString(TEXT("교환 처리 중"));
		if (Offer.bConfirmed) return FText::FromString(TEXT("확정"));
		if (Offer.bLocked) return FText::FromString(TEXT("잠금"));
		return FText::FromString(TEXT("제시 중"));
	}

	void SetShown(UWidget* Widget, bool bShown)
	{
		if (Widget) Widget->SetVisibility(bShown ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}
}

UJMTradeManager* UTradeWidget::GetTradeManager() const
{
	const UGameInstance* GameInstance = GetGameInstance();
	return GameInstance ? GameInstance->GetSubsystem<UJMTradeManager>() : nullptr;
}

void UTradeWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (BtnAccept) BtnAccept->OnClicked.AddUniqueDynamic(this, &UTradeWidget::OnAcceptClicked);
	if (BtnDecline) BtnDecline->OnClicked.AddUniqueDynamic(this, &UTradeWidget::OnDeclineClicked);
	if (BtnLock) BtnLock->OnClicked.AddUniqueDynamic(this, &UTradeWidget::OnLockClicked);
	if (BtnConfirm) BtnConfirm->OnClicked.AddUniqueDynamic(this, &UTradeWidget::OnConfirmClicked);
	if (BtnCancel) BtnCancel->OnClicked.AddUniqueDynamic(this, &UTradeWidget::OnCancelClicked);
	if (EditGold) EditGold->OnTextCommitted.AddUniqueDynamic(this, &UTradeWidget::OnGoldCommitted);
	if (EditCount) EditCount->OnTextCommitted.AddUniqueDynamic(this, &UTradeWidget::OnCountCommitted);

	if (UJMTradeManager* Trade = GetTradeManager())
	{
		TradeChangedHandle = Trade->OnTradeChanged.AddUObject(this, &UTradeWidget::Refresh);
	}
	Refresh();
}

void UTradeWidget::NativeDestruct()
{
	if (UJMTradeManager* Trade = GetTradeManager())
	{
		Trade->OnTradeChanged.Remove(TradeChangedHandle);
	}
	Super::NativeDestruct();
}

void UTradeWidget::FillOffer(UPanelWidget* Panel, TArray<TObjectPtr<UItemSlotWidget>>& Slots, const FJMTradeOffer& Offer, bool bMine)
{
	if (Panel == nullptr || !ItemSlotClass) return;
	const UDataManager* DataManager = GetGameInstance() ? GetGameInstance()->GetSubsystem<UDataManager>() : nullptr;

	// 칸은 올린 개수만큼만 보인다. 모자라면 만들고 남으면 숨긴다.
	while (Slots.Num() < Offer.Items.Num())
	{
		UItemSlotWidget* ItemSlot = CreateWidget<UItemSlotWidget>(this, ItemSlotClass);
		if (ItemSlot == nullptr) return;
		Panel->AddChild(ItemSlot);
		ItemSlot->InitBagSlot(Slots.Num());
		ItemSlot->SetDragEnabled(false);   // 거래 창의 칸은 끌 수 없다(인벤토리 창이 다른 요청으로 읽지 않게)
		ItemSlot->OnClicked.BindUObject(this, &UTradeWidget::OnSlotClicked);
		if (bMine)
		{
			ItemSlot->OnRightClicked.BindUObject(this, &UTradeWidget::OnMySlotRightClicked);
			ItemSlot->OnDropped.BindUObject(this, &UTradeWidget::OnSlotDropped);
		}
		Slots.Add(ItemSlot);
	}

	for (int32 Index = 0; Index < Slots.Num(); ++Index)
	{
		UItemSlotWidget* ItemSlot = Slots[Index];
		if (ItemSlot == nullptr) continue;
		if (!Offer.Items.IsValidIndex(Index))
		{
			ItemSlot->SetItem(nullptr, nullptr);
			ItemSlot->SetSelected(false);
			ItemSlot->SetVisibility(ESlateVisibility::Collapsed);
			continue;
		}

		const FJMTradeOfferItem& Offered = Offer.Items[Index];
		FJMInventoryItem Item;
		Item.ItemDbId = Offered.ItemDbId;
		Item.ItemId = Offered.ItemId;
		Item.Count = Offered.Count;
		ItemSlot->SetItem(&Item, DataManager ? DataManager->FindItemById(Offered.ItemId) : nullptr);
		ItemSlot->SetSelected(SelectedDbId == Offered.ItemDbId && bSelectedMine == bMine);
		ItemSlot->SetVisibility(ESlateVisibility::Visible);
	}
}

void UTradeWidget::Refresh()
{
	const UJMTradeManager* Trade = GetTradeManager();
	if (Trade == nullptr) return;

	const EJMTradeState State = Trade->GetState();
	const bool bRequest = State == EJMTradeState::Incoming || State == EJMTradeState::Waiting;
	const bool bTrade = State == EJMTradeState::Offering || State == EJMTradeState::Confirming;
	SetShown(PanelRequest, bRequest);
	SetShown(PanelTrade, bTrade);

	const FString Partner = Trade->GetPartnerName().IsEmpty() ? FString(TEXT("상대")) : Trade->GetPartnerName();
	if (bRequest)
	{
		const bool bIncoming = State == EJMTradeState::Incoming;
		if (TxtRequest)
		{
			TxtRequest->SetText(FText::FromString(bIncoming
				? FString::Printf(TEXT("%s 님이 거래를 요청했습니다."), *Partner)
				: FString::Printf(TEXT("%s 님의 응답을 기다리는 중입니다."), *Partner)));
		}
		SetShown(BtnAccept, bIncoming);
		if (TxtDecline) TxtDecline->SetText(FText::FromString(bIncoming ? TEXT("거절") : TEXT("취소")));
	}
	if (!bTrade)
	{
		SelectedDbId = 0;
		return;
	}

	const FJMTradeOffer& Mine = Trade->GetMyOffer();
	const FJMTradeOffer& Theirs = Trade->GetTheirOffer();
	const bool bConfirming = State == EJMTradeState::Confirming;

	// 고른 칸이 제시에서 사라졌으면 선택을 푼다
	const FJMTradeOffer& SelectedSide = bSelectedMine ? Mine : Theirs;
	const FJMTradeOfferItem* Selected = SelectedSide.Items.FindByPredicate([this](const FJMTradeOfferItem& Item) { return Item.ItemDbId == SelectedDbId; });
	if (Selected == nullptr) SelectedDbId = 0;

	FillOffer(MyItems, MySlots, Mine, true);
	FillOffer(TheirItems, TheirSlots, Theirs, false);

	if (TxtPartner) TxtPartner->SetText(FText::FromString(Partner));
	if (TxtMyStatus) TxtMyStatus->SetText(OfferStatusText(Mine, bConfirming));
	if (TxtTheirStatus) TxtTheirStatus->SetText(OfferStatusText(Theirs, bConfirming));
	if (TxtTheirGold) TxtTheirGold->SetText(FText::AsNumber(Theirs.Gold));

	const bool bCanEdit = State == EJMTradeState::Offering && !Mine.bLocked;
	if (EditGold)
	{
		// 적는 중에는 건드리지 않는다(상대의 변경으로 화면이 다시 그려져도 입력이 지워지지 않게)
		if (!EditGold->HasKeyboardFocus()) EditGold->SetText(FText::FromString(FString::Printf(TEXT("%lld"), Mine.Gold)));
		EditGold->SetIsEnabled(bCanEdit);
	}
	if (EditCount)
	{
		const bool bCountEditable = bCanEdit && Selected != nullptr && bSelectedMine;
		if (!EditCount->HasKeyboardFocus() || !bCountEditable)
		{
			EditCount->SetText(Selected ? FText::FromString(FString::Printf(TEXT("%lld"), Selected->Count)) : FText::GetEmpty());
		}
		EditCount->SetIsEnabled(bCountEditable);
	}
	if (TxtSelected)
	{
		FText Text;
		if (Selected)
		{
			const UDataManager* DataManager = GetGameInstance() ? GetGameInstance()->GetSubsystem<UDataManager>() : nullptr;
			const FItemTableRow* Row = DataManager ? DataManager->FindItemById(Selected->ItemId) : nullptr;
			const FString Name = Row ? Row->DisplayName.ToString() : FString::FromInt(Selected->ItemId);
			Text = FText::FromString(FString::Printf(TEXT("%s x%lld"), *Name, Selected->Count));
		}
		TxtSelected->SetText(Text);
	}

	if (BtnLock) BtnLock->SetIsEnabled(State == EJMTradeState::Offering);
	if (TxtLock) TxtLock->SetText(FText::FromString(Mine.bLocked ? TEXT("잠금 해제") : TEXT("잠금")));
	// 확정은 양쪽이 잠근 뒤에만(서버 규칙)
	if (BtnConfirm) BtnConfirm->SetIsEnabled(State == EJMTradeState::Offering && Mine.bLocked && Theirs.bLocked && !Mine.bConfirmed);
	if (BtnCancel) BtnCancel->SetIsEnabled(State == EJMTradeState::Offering);
}

void UTradeWidget::OnSlotClicked(UItemSlotWidget* ItemSlot)
{
	if (ItemSlot == nullptr) return;
	SelectedDbId = ItemSlot->GetItemDbId();
	bSelectedMine = MySlots.Contains(ItemSlot);
	Refresh();
}

void UTradeWidget::OnMySlotRightClicked(UItemSlotWidget* ItemSlot)
{
	if (ItemSlot == nullptr || !ItemSlot->HasItem()) return;
	if (UJMTradeManager* Trade = GetTradeManager()) Trade->RemoveItem(ItemSlot->GetItemDbId());
}

void UTradeWidget::OnSlotDropped(UItemSlotWidget* From, UItemSlotWidget* To)
{
	OfferFromInventorySlot(From);
}

bool UTradeWidget::NativeOnDrop(const FGeometry& InGeometry, const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation)
{
	UItemSlotWidget* From = InOperation ? Cast<UItemSlotWidget>(InOperation->Payload) : nullptr;
	if (From == nullptr) return Super::NativeOnDrop(InGeometry, InDragDropEvent, InOperation);
	OfferFromInventorySlot(From);
	return true;
}

void UTradeWidget::OfferFromInventorySlot(UItemSlotWidget* From)
{
	// 인벤토리 칸의 아이템만 올릴 수 있다(장착 칸은 서버도 거부). 행 전부를 올리고 수량은 EditCount로 줄인다.
	if (From == nullptr || !From->HasItem() || From->IsEquipSlot()) return;
	if (MySlots.Contains(From) || TheirSlots.Contains(From)) return;
	if (UJMTradeManager* Trade = GetTradeManager()) Trade->AddItem(From->GetItemDbId(), 0);
}

void UTradeWidget::OnAcceptClicked()
{
	if (UJMTradeManager* Trade = GetTradeManager()) Trade->Respond(true);
}

void UTradeWidget::OnDeclineClicked()
{
	UJMTradeManager* Trade = GetTradeManager();
	if (Trade == nullptr) return;
	if (Trade->GetState() == EJMTradeState::Incoming) Trade->Respond(false);
	else Trade->Cancel();
}

void UTradeWidget::OnLockClicked()
{
	if (UJMTradeManager* Trade = GetTradeManager()) Trade->SetLocked(!Trade->GetMyOffer().bLocked);
}

void UTradeWidget::OnConfirmClicked()
{
	if (UJMTradeManager* Trade = GetTradeManager()) Trade->Confirm();
}

void UTradeWidget::OnCancelClicked()
{
	if (UJMTradeManager* Trade = GetTradeManager()) Trade->Cancel();
}

void UTradeWidget::OnGoldCommitted(const FText& Text, ETextCommit::Type CommitMethod)
{
	if (CommitMethod == ETextCommit::OnCleared) return;
	UJMTradeManager* Trade = GetTradeManager();
	if (Trade == nullptr || !Trade->IsOffering() || Trade->GetMyOffer().bLocked) return;

	// 가진 골드 안에서만(검증은 서버가 다시 한다)
	const UJMInventoryManager* Inventory = GetGameInstance() ? GetGameInstance()->GetSubsystem<UJMInventoryManager>() : nullptr;
	const int64 MaxGold = Inventory ? Inventory->GetGold() : 0;
	const int64 Gold = FMath::Clamp<int64>(FCString::Atoi64(*Text.ToString()), 0, MaxGold);
	if (EditGold) EditGold->SetText(FText::FromString(FString::Printf(TEXT("%lld"), Gold)));
	Trade->SetGold(Gold);
}

void UTradeWidget::OnCountCommitted(const FText& Text, ETextCommit::Type CommitMethod)
{
	if (CommitMethod == ETextCommit::OnCleared) return;
	UJMTradeManager* Trade = GetTradeManager();
	if (Trade == nullptr || !Trade->IsOffering() || Trade->GetMyOffer().bLocked) return;
	if (SelectedDbId == 0 || !bSelectedMine) return;

	// 1 ~ 그 행의 수량으로 맞추는 일은 UJMTradeManager::AddItem이 한다. 0 이하를 적으면 1개로 본다.
	Trade->AddItem(SelectedDbId, FMath::Max<int64>(FCString::Atoi64(*Text.ToString()), 1));
}
