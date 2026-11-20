#include "Game/JMTradeManager.h"
#include "Game/JMInventoryManager.h"
#include "Game/JMObjectManager.h"
#include "Character/JMCharacterBase.h"
#include "Player/JMMyPlayer.h"
#include "Network/NetworkManager.h"
#include "UI/JMUIManager.h"
#include "UI/WidgetNames.h"

namespace
{
	FJMTradeOffer ToOffer(const Protocol::TradeOffer& In)
	{
		FJMTradeOffer Out;
		Out.ObjectId = In.object_id();
		Out.Gold = In.gold();
		Out.bLocked = In.locked();
		Out.bConfirmed = In.confirmed();
		for (const Protocol::TradeOfferItem& Item : In.items())
		{
			FJMTradeOfferItem OutItem;
			OutItem.ItemDbId = Item.item_db_id();
			OutItem.ItemId = Item.item_id();
			OutItem.Count = Item.count();
			Out.Items.Add(OutItem);
		}
		return Out;
	}

	// 거부(SC_TRADE_RESULT)·종료(SC_TRADE_END) 사유 문구
	FText TradeResultText(Protocol::TradeResult Result)
	{
		switch (Result)
		{
		case Protocol::TRADE_RESULT_SUCCESS:		return FText::FromString(TEXT("거래가 완료되었습니다."));
		case Protocol::TRADE_RESULT_NO_TARGET:		return FText::FromString(TEXT("거래할 수 없는 대상입니다."));
		case Protocol::TRADE_RESULT_TOO_FAR:		return FText::FromString(TEXT("상대와 너무 멀리 떨어져 있습니다."));
		case Protocol::TRADE_RESULT_TARGET_BUSY:	return FText::FromString(TEXT("상대가 다른 거래를 하고 있습니다."));
		case Protocol::TRADE_RESULT_DECLINED:		return FText::FromString(TEXT("상대가 거래를 거절했습니다."));
		case Protocol::TRADE_RESULT_TIMEOUT:		return FText::FromString(TEXT("시간이 지나 거래가 취소되었습니다."));
		case Protocol::TRADE_RESULT_CANCELLED:		return FText::FromString(TEXT("거래가 취소되었습니다."));
		case Protocol::TRADE_RESULT_DEAD:			return FText::FromString(TEXT("사망하여 거래가 취소되었습니다."));
		case Protocol::TRADE_RESULT_DISCONNECTED:	return FText::FromString(TEXT("상대가 떠나 거래가 취소되었습니다."));
		case Protocol::TRADE_RESULT_MOVED:			return FText::FromString(TEXT("상대나 내가 다른 곳으로 이동해 거래가 취소되었습니다."));
		case Protocol::TRADE_RESULT_LOCKED:			return FText::FromString(TEXT("잠금을 풀어야 제시를 바꿀 수 있습니다."));
		case Protocol::TRADE_RESULT_INVALID_OFFER:	return FText::FromString(TEXT("올릴 수 없는 제시입니다."));
		case Protocol::TRADE_RESULT_INVENTORY_FULL:	return FText::FromString(TEXT("받는 쪽 인벤토리에 빈 칸이 부족합니다."));
		case Protocol::TRADE_RESULT_GOLD_FULL:		return FText::FromString(TEXT("받는 쪽이 골드를 더 가질 수 없습니다."));
		case Protocol::TRADE_RESULT_BUSY:			return FText::FromString(TEXT("지금은 할 수 없습니다. 잠시 후 다시 시도해 주세요."));
		case Protocol::TRADE_RESULT_NOT_ALLOWED:	return FText::FromString(TEXT("지금은 할 수 없는 요청입니다."));
		case Protocol::TRADE_RESULT_DB_ERROR:		return FText::FromString(TEXT("서버 오류로 거래하지 못했습니다. 바뀐 것은 없습니다."));
		default:									return FText::FromString(TEXT("거래를 진행할 수 없습니다."));
		}
	}
}

void UJMTradeManager::HandleRequest(const Protocol::SC_TRADE_REQUEST_PACKET& Pkt)
{
	// 화면은 뒤따라 오는 SC_TRADE_STATE(REQUESTING)에서 연다. 여기서는 요청한 사람의 이름만 받아 둔다.
	PartnerName = FString(UTF8_TO_TCHAR(Pkt.from_name().c_str()));
	UE_LOG(LogTemp, Log, TEXT("Trade request from %d (%s), timeout %d sec"), Pkt.from_id(), *PartnerName, Pkt.timeout_sec());
}

void UJMTradeManager::HandleState(const Protocol::SC_TRADE_STATE_PACKET& Pkt)
{
	UGameInstance* GameInstance = GetGameInstance();
	const UJMObjectManager* ObjectManager = GameInstance ? GameInstance->GetSubsystem<UJMObjectManager>() : nullptr;
	if (ObjectManager == nullptr || ObjectManager->MyPlayer == nullptr || Pkt.offers_size() != 2) return;

	// offers[0] = 요청한 쪽, offers[1] = 받은 쪽
	const int32 MyId = ObjectManager->MyPlayer->GetObjectInfo().id();
	const int32 MyIndex = Pkt.offers(0).object_id() == MyId ? 0 : (Pkt.offers(1).object_id() == MyId ? 1 : -1);
	if (MyIndex < 0) return;

	Mine = ToOffer(Pkt.offers(MyIndex));
	Theirs = ToOffer(Pkt.offers(1 - MyIndex));
	bHasPending = false;

	const EJMTradeState OldState = State;
	switch (Pkt.phase())
	{
	case Protocol::TRADE_PHASE_REQUESTING:	State = MyIndex == 0 ? EJMTradeState::Waiting : EJMTradeState::Incoming; break;
	case Protocol::TRADE_PHASE_OFFERING:	State = EJMTradeState::Offering; break;
	case Protocol::TRADE_PHASE_CONFIRMING:	State = EJMTradeState::Confirming; break;
	default: return;
	}

	if (OldState == EJMTradeState::None)
	{
		bSelfEnded = false;
		// 내가 요청한 쪽이면 상대 이름은 화면에 있는 캐릭터에서 읽는다(받은 쪽은 SC_TRADE_REQUEST가 알려 줬다).
		if (MyIndex == 0)
		{
			const TObjectPtr<AJMCharacterBase>* Partner = ObjectManager->Objects.Find(Theirs.ObjectId);
			PartnerName = (Partner && *Partner) ? FString(UTF8_TO_TCHAR((*Partner)->GetObjectInfo().name().c_str())) : FString();
		}
	}

	// 제시 단계에 들어가면 아이템을 고를 수 있게 인벤토리도 함께 연다.
	const bool bEnteredOffering = State == EJMTradeState::Offering && OldState != EJMTradeState::Offering && OldState != EJMTradeState::Confirming;
	SetWindowVisible(true, bEnteredOffering);
	OnTradeChanged.Broadcast();
}

void UJMTradeManager::HandleEnd(Protocol::TradeResult Result)
{
	UE_LOG(LogTemp, Log, TEXT("Trade end: result=%d"), static_cast<int32>(Result));
	const bool bSilent = bSelfEnded && (Result == Protocol::TRADE_RESULT_CANCELLED || Result == Protocol::TRADE_RESULT_DECLINED);
	const bool bHadOffering = State == EJMTradeState::Offering || State == EJMTradeState::Confirming;

	State = EJMTradeState::None;
	Mine = FJMTradeOffer();
	Theirs = FJMTradeOffer();
	PartnerName.Reset();
	bHasPending = false;
	bSelfEnded = false;

	SetWindowVisible(false, bHadOffering);
	OnTradeChanged.Broadcast();
	if (!bSilent) ShowMessage(TradeResultText(Result));
}

void UJMTradeManager::HandleResult(Protocol::PacketId Request, Protocol::TradeResult Result)
{
	UE_LOG(LogTemp, Log, TEXT("Trade request rejected: request=%d result=%d"), static_cast<int32>(Request), static_cast<int32>(Result));
	// 거부된 요청은 반영되지 않았다. 화면을 서버가 알려 준 값으로 되돌린다.
	bHasPending = false;
	bSelfEnded = false;
	OnTradeChanged.Broadcast();
	ShowMessage(TradeResultText(Result));
}

void UJMTradeManager::Clear()
{
	State = EJMTradeState::None;
	Mine = FJMTradeOffer();
	Theirs = FJMTradeOffer();
	PartnerName.Reset();
	bHasPending = false;
	bSelfEnded = false;
	OnTradeChanged.Broadcast();
}

void UJMTradeManager::RequestTrade(int32 TargetId)
{
	if (IsActive()) return;
	if (UNetworkManager* NetManager = UNetworkManager::Get(this)) NetManager->SendTradeRequest(TargetId);
}

void UJMTradeManager::Respond(bool bAccept)
{
	if (State != EJMTradeState::Incoming) return;
	if (!bAccept) bSelfEnded = true;
	if (UNetworkManager* NetManager = UNetworkManager::Get(this)) NetManager->SendTradeRespond(bAccept);
}

void UJMTradeManager::AddItem(int64 ItemDbId, int64 Count)
{
	if (State != EJMTradeState::Offering || ItemDbId == 0) return;

	const UJMInventoryManager* Inventory = GetGameInstance() ? GetGameInstance()->GetSubsystem<UJMInventoryManager>() : nullptr;
	const FJMInventoryItem* Owned = Inventory ? Inventory->GetItems().Find(ItemDbId) : nullptr;
	if (Owned == nullptr || Owned->EquipPart != 0) return;   // 장착 중인 것은 올릴 수 없다(서버도 거부)

	const int64 OfferCount = Count <= 0 ? Owned->Count : FMath::Clamp<int64>(Count, 1, Owned->Count);
	FJMTradeOffer NewOffer = GetEditBase();
	FJMTradeOfferItem* Existing = NewOffer.Items.FindByPredicate([ItemDbId](const FJMTradeOfferItem& Item) { return Item.ItemDbId == ItemDbId; });
	if (Existing)
	{
		if (Existing->Count == OfferCount) return;
		Existing->Count = OfferCount;
	}
	else
	{
		FJMTradeOfferItem Item;
		Item.ItemDbId = ItemDbId;
		Item.ItemId = Owned->ItemId;
		Item.Count = OfferCount;
		NewOffer.Items.Add(Item);
	}
	SendOffer(NewOffer);
}

void UJMTradeManager::RemoveItem(int64 ItemDbId)
{
	if (State != EJMTradeState::Offering) return;
	FJMTradeOffer NewOffer = GetEditBase();
	if (NewOffer.Items.RemoveAll([ItemDbId](const FJMTradeOfferItem& Item) { return Item.ItemDbId == ItemDbId; }) == 0) return;
	SendOffer(NewOffer);
}

void UJMTradeManager::SetGold(int64 Gold)
{
	if (State != EJMTradeState::Offering) return;
	FJMTradeOffer NewOffer = GetEditBase();
	if (NewOffer.Gold == Gold) return;
	NewOffer.Gold = Gold;
	SendOffer(NewOffer);
}

void UJMTradeManager::SendOffer(const FJMTradeOffer& NewOffer)
{
	UNetworkManager* NetManager = UNetworkManager::Get(this);
	if (NetManager == nullptr) return;

	Pending = NewOffer;
	bHasPending = true;

	TArray<TPair<int64, int64>> Items;
	for (const FJMTradeOfferItem& Item : NewOffer.Items) Items.Emplace(Item.ItemDbId, Item.Count);
	NetManager->SendTradeSetOffer(Items, NewOffer.Gold);
}

void UJMTradeManager::SetLocked(bool bLocked)
{
	if (State != EJMTradeState::Offering) return;
	if (UNetworkManager* NetManager = UNetworkManager::Get(this)) NetManager->SendTradeLock(bLocked);
}

void UJMTradeManager::Confirm()
{
	if (State != EJMTradeState::Offering) return;
	if (UNetworkManager* NetManager = UNetworkManager::Get(this)) NetManager->SendTradeConfirm();
}

void UJMTradeManager::Cancel()
{
	if (State == EJMTradeState::None) return;
	bSelfEnded = true;
	if (UNetworkManager* NetManager = UNetworkManager::Get(this)) NetManager->SendTradeCancel();
}

void UJMTradeManager::SetWindowVisible(bool bVisible, bool bWithInventory)
{
	UJMUIManager* UIManager = GetGameInstance() ? GetGameInstance()->GetSubsystem<UJMUIManager>() : nullptr;
	if (UIManager == nullptr) return;
	UIManager->SetWidgetVisible(WidgetNames::Trade, bVisible);
	if (bWithInventory) UIManager->SetWidgetVisible(WidgetNames::Inventory, bVisible);
}

void UJMTradeManager::ShowMessage(const FText& Message)
{
	if (UJMUIManager* UIManager = GetGameInstance() ? GetGameInstance()->GetSubsystem<UJMUIManager>() : nullptr)
	{
		UIManager->ShowSystemMessage(Message);
	}
}
