#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Protocol/Protocol.pb.h"
#include "JMShopManager.generated.h"

// 상점 품목 한 줄. 구매가는 서버가 알려 준 값(ShopTable은 서버 전용)이다.
struct FJMShopEntry
{
	int32 ItemId = 0;
	int64 BuyPrice = 0;
};

DECLARE_MULTICAST_DELEGATE(FOnJMShopChanged);

// 상점 NPC 창의 클라이언트 상태 (L4, D-201·D-202, DESIGN_ITEMS_TRADE "상점 NPC").
// 서버에는 "상점을 연 상태"가 없다 — 열기 응답(SC_SHOP_OPEN)으로 받은 품목을 담아 창을 띄우고, 구매·판매는 요청만 보낸다.
// 결과는 SC_INVENTORY_UPDATE(성공)·SC_ITEM_RESULT(거부)로 오므로 골드·아이템은 UJMInventoryManager가 맡는다.
// NPC에서 멀어지거나 죽거나 1:1 거래가 시작되면 창을 닫는다(AJMMyPlayer가 0.1초마다 UpdateFromPlayer를 부른다).
UCLASS()
class JM_API UJMShopManager : public UGameInstanceSubsystem
{
	GENERATED_BODY()
public:
	void HandleOpen(const Protocol::SC_SHOP_OPEN_PACKET& Pkt);
	// 로그인 화면으로 돌아갈 때 비운다
	void Clear();

	// NpcId = 상점 NPC의 오브젝트 id. 거리·거래 중·사망 검증은 서버가 한다.
	void RequestOpen(int32 NpcId);
	void Close();
	void Buy(int32 ItemId, int32 Count);
	// 판매 후보로 고른 행(SellCandidate)에서 Count개를 판다
	void SellSelected(int32 Count);
	// 상점이 열린 동안 인벤토리 아이템을 우클릭하면 판매 후보로만 고른다(되사기가 없어 [판매] 버튼으로 확정한다)
	void SetSellCandidate(int64 ItemDbId);
	// 내 위치·상태로 창을 계속 열어 둘 수 있는지 본다
	void UpdateFromPlayer(const FVector& PlayerLocation, bool bDead);

	bool IsOpen() const { return bOpen; }
	int32 GetNpcId() const { return NpcId; }
	const FString& GetNpcName() const { return NpcName; }
	const TArray<FJMShopEntry>& GetEntries() const { return Entries; }
	int64 GetSellCandidate() const { return SellCandidate; }

	// 열림·닫힘·품목·판매 후보가 바뀔 때마다
	FOnJMShopChanged OnShopChanged;

private:
	void SetWindowVisible(bool bVisible);

	bool bOpen = false;
	int32 NpcId = 0;
	FString NpcName;
	TArray<FJMShopEntry> Entries;
	int64 SellCandidate = 0;
};
