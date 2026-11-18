#include "Game/JMShopManager.h"
#include "Game/JMInventoryManager.h"
#include "Game/JMObjectManager.h"
#include "Game/JMTradeManager.h"
#include "Network/NetworkManager.h"
#include "UI/JMUIManager.h"
#include "UI/WidgetNames.h"

void UJMShopManager::HandleOpen(const Protocol::SC_SHOP_OPEN_PACKET& Pkt)
{
	NpcId = Pkt.npc_id();
	Entries.Reset();
	for (const Protocol::ShopItem& Item : Pkt.items())
	{
		FJMShopEntry Entry;
		Entry.ItemId = Item.item_id();
		Entry.BuyPrice = Item.buy_price();
		Entries.Add(Entry);
	}
	NpcName.Reset();
	if (const UJMObjectManager* ObjectManager = GetGameInstance() ? GetGameInstance()->GetSubsystem<UJMObjectManager>() : nullptr)
	{
		NpcName = ObjectManager->GetNpcName(NpcId);
	}
	SellCandidate = 0;
	bOpen = true;
	SetWindowVisible(true);
	OnShopChanged.Broadcast();
}

void UJMShopManager::Clear()
{
	bOpen = false;
	NpcId = 0;
	NpcName.Reset();
	Entries.Reset();
	SellCandidate = 0;
	OnShopChanged.Broadcast();
}

void UJMShopManager::RequestOpen(int32 InNpcId)
{
	if (InNpcId == 0) return;
	if (UNetworkManager* NetManager = UNetworkManager::Get(this)) NetManager->SendShopOpen(InNpcId);
}

void UJMShopManager::Close()
{
	if (!bOpen) return;
	bOpen = false;
	SellCandidate = 0;
	SetWindowVisible(false);
	OnShopChanged.Broadcast();
}

void UJMShopManager::Buy(int32 ItemId, int32 Count)
{
	if (!bOpen || ItemId == 0 || Count < 1) return;
	if (UNetworkManager* NetManager = UNetworkManager::Get(this)) NetManager->SendShopBuy(NpcId, ItemId, Count);
}

void UJMShopManager::SellSelected(int32 Count)
{
	if (!bOpen || SellCandidate == 0 || Count < 1) return;
	if (UNetworkManager* NetManager = UNetworkManager::Get(this)) NetManager->SendShopSell(NpcId, SellCandidate, Count);
}

void UJMShopManager::SetSellCandidate(int64 ItemDbId)
{
	if (!bOpen || SellCandidate == ItemDbId) return;
	SellCandidate = ItemDbId;
	OnShopChanged.Broadcast();
}

void UJMShopManager::UpdateFromPlayer(const FVector& PlayerLocation, bool bDead)
{
	if (!bOpen) return;
	const UGameInstance* GameInstance = GetGameInstance();
	if (GameInstance == nullptr) return;

	// 죽었거나 1:1 거래가 시작됐으면 닫는다(서버도 그 상태의 상점 요청을 거부한다).
	const UJMTradeManager* Trade = GameInstance->GetSubsystem<UJMTradeManager>();
	if (bDead || (Trade && Trade->IsActive())) { Close(); return; }

	// NPC가 시야에서 사라졌거나 이용 거리(서버 값 SC_INVENTORY.shop_range_cm)를 벗어나면 닫는다.
	const UJMObjectManager* ObjectManager = GameInstance->GetSubsystem<UJMObjectManager>();
	const UJMInventoryManager* Inventory = GameInstance->GetSubsystem<UJMInventoryManager>();
	FVector NpcLocation;
	if (ObjectManager == nullptr || !ObjectManager->GetNpcLocation(NpcId, NpcLocation)) { Close(); return; }
	const float Range = Inventory ? static_cast<float>(Inventory->GetShopRangeCm()) : 0.f;
	if (FVector::DistSquared2D(PlayerLocation, NpcLocation) > Range * Range) Close();
}

void UJMShopManager::SetWindowVisible(bool bVisible)
{
	UJMUIManager* UIManager = GetGameInstance() ? GetGameInstance()->GetSubsystem<UJMUIManager>() : nullptr;
	if (UIManager == nullptr) return;
	UIManager->SetWidgetVisible(WidgetNames::Shop, bVisible);
	// 팔 아이템을 고르려면 인벤토리가 보여야 하므로 함께 열고, 상점이 닫히면(닫기 버튼·F·멀어짐·사망·거래 시작) 인벤토리도 함께 닫는다.
	UIManager->SetWidgetVisible(WidgetNames::Inventory, bVisible);
}
