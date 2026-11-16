#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Protocol/Protocol.pb.h"
#include "JMTradeManager.generated.h"

// 거래 창이 지금 보여 줄 화면
enum class EJMTradeState : uint8
{
	None,        // 거래 없음
	Incoming,    // 요청을 받았다 — 수락·거절
	Waiting,     // 내가 요청했고 상대의 응답을 기다린다
	Offering,    // 제시·잠금·확정을 주고받는 중
	Confirming,  // 양쪽 확정 — 서버가 교환하는 중
};

struct FJMTradeOfferItem
{
	int64 ItemDbId = 0;
	int32 ItemId = 0;
	int64 Count = 0;
};

struct FJMTradeOffer
{
	int32 ObjectId = 0;
	TArray<FJMTradeOfferItem> Items;
	int64 Gold = 0;
	bool bLocked = false;
	bool bConfirmed = false;
};

DECLARE_MULTICAST_DELEGATE(FOnJMTradeChanged);

// 1:1 거래의 클라이언트 사본 (A6.3a, DESIGN_ITEMS_TRADE "1:1 거래").
// 서버가 보낸 상태(SC_TRADE_STATE)를 그대로 담고, 조작은 요청만 보낸다. 화면(UTradeWidget)은 서버 상태가 와야 바뀐다.
// 거부(SC_TRADE_RESULT)와 끝난 이유(SC_TRADE_END)는 시스템 메시지 창으로 알린다.
UCLASS()
class JM_API UJMTradeManager : public UGameInstanceSubsystem
{
	GENERATED_BODY()
public:
	void HandleRequest(const Protocol::SC_TRADE_REQUEST_PACKET& Pkt);
	void HandleState(const Protocol::SC_TRADE_STATE_PACKET& Pkt);
	void HandleEnd(Protocol::TradeResult Result);
	void HandleResult(Protocol::PacketId Request, Protocol::TradeResult Result);
	// 로그인 화면으로 돌아갈 때 비운다
	void Clear();

	// TargetId = 거래할 플레이어의 오브젝트 id. 거리·상태 검증은 서버가 한다.
	void RequestTrade(int32 TargetId);
	void Respond(bool bAccept);
	// 내 제시에 인벤토리 아이템을 올린다. Count가 0 이하면 그 행 전부. 이미 올린 행이면 수량만 바꾼다.
	void AddItem(int64 ItemDbId, int64 Count);
	void RemoveItem(int64 ItemDbId);
	void SetGold(int64 Gold);
	void SetLocked(bool bLocked);
	void Confirm();
	// 요청 중·제시 중 어느 때나. 받은 요청의 거절은 Respond(false)
	void Cancel();

	EJMTradeState GetState() const { return State; }
	bool IsActive() const { return State != EJMTradeState::None; }
	bool IsOffering() const { return State == EJMTradeState::Offering; }
	const FJMTradeOffer& GetMyOffer() const { return Mine; }
	const FJMTradeOffer& GetTheirOffer() const { return Theirs; }
	const FString& GetPartnerName() const { return PartnerName; }

	// 거래 상태가 바뀔 때마다(거부로 화면을 서버 값에 다시 맞춰야 할 때 포함)
	FOnJMTradeChanged OnTradeChanged;

private:
	// 내 제시 전체를 NewOffer로 바꾸자고 요청한다(CS_TRADE_SET_OFFER는 전체 교체).
	void SendOffer(const FJMTradeOffer& NewOffer);
	// 다음 요청의 바탕: 응답을 기다리는 요청이 있으면 그것, 없으면 서버가 알려 준 내 제시.
	// (연달아 두 개를 올릴 때 두 번째 요청이 첫 번째를 덮어쓰지 않게)
	const FJMTradeOffer& GetEditBase() const { return bHasPending ? Pending : Mine; }
	void SetWindowVisible(bool bVisible, bool bWithInventory);
	void ShowMessage(const FText& Message);

	EJMTradeState State = EJMTradeState::None;
	FJMTradeOffer Mine;
	FJMTradeOffer Theirs;
	FString PartnerName;

	FJMTradeOffer Pending;
	bool bHasPending = false;
	// 내가 취소·거절을 보냈다(그 결과로 오는 종료 안내는 띄우지 않는다)
	bool bSelfEnded = false;
};
