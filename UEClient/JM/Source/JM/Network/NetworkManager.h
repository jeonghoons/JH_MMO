#pragma once
#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "Protocol/Protocol.pb.h"


#include "NetworkManager.generated.h"

class NetworkSession;
class AJMPlayer;
class AJMMyPlayer;

UCLASS()
class JM_API UNetworkManager : public UGameInstanceSubsystem, public FTickableGameObject
{
	GENERATED_BODY()
public:
	virtual void Deinitialize() override;

	virtual void Tick(float DeltaTime) override;
	virtual bool IsTickable() const override { return true; }
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UNetworkManager, STATGROUP_Tickables); }

public:
	UFUNCTION(BlueprintCallable)
	void ConnectToServer();

	UFUNCTION(BlueprintCallable)
	void DisconnectFromServer();

	void SendPacket(TSharedPtr<class SendBuffer> sendBuffer);

	// 세션이 있고 끊김이 감지되지 않았으면 true
	bool IsConnected() const;

	// 월드 컨텍스트 오브젝트(액터, 위젯 등)로 NetworkManager를 얻는다. GameInstance가 없으면 nullptr.
	static UNetworkManager* Get(const UObject* WorldContext);

	// 패킷별 송신 함수. 패킷 구성과 SendBuffer 생성은 이 함수들에서만 한다.
	void SendLogin(const FString& AccountId, const FString& AccountPw);
	void SendSignUp(const FString& AccountId, const FString& AccountPw, int32 PlayerType);   // PlayerType = Protocol::PlayerType (전사/궁수)
	void SendEnterRoom();
	// CorrectionSeq: 서버에게서 받은 마지막 내 위치 보정 순번(S1). 서버는 순번이 다른 이동을 버린다.
	void SendMove(const Protocol::PositionInfo& PosInfo, bool bForce, uint32 CorrectionSeq);
	// Seq: 요청 순번(CP1). 서버가 SC_ATTACK·SC_ATTACK_RESULT에 그대로 돌려준다
	void SendAttack(int32 SkillSlot, int32 TargetId, float AimYaw, float AimX, float AimY, uint32 Seq);
	void SendChat(const FString& Message);   // 검증(길이·제어 문자·도배)은 서버가 한다
	// Part = Protocol::EquipPart(1~7), ItemDbId = 내 인벤토리의 아이템(UJMInventoryManager), 0 = 해제. 검증은 서버가 한다(A3.3)
	void SendChangeEquip(int32 Part, int64 ItemDbId);
	// 개발 지급 명령(A3.3). ItemId 0 = 골드. 서버 [Debug] DevCommands가 켜져 있을 때만 받는다
	void SendDevGive(int32 ItemId, int64 Count);
	// 인벤토리 칸 옮기기(A6.1). 대상 칸에 아이템이 있으면 서버가 맞바꾼다. 결과는 SC_INVENTORY_UPDATE.
	void SendMoveItem(int64 ItemDbId, int32 ToSlot);
	// A6.2: 바닥 드랍 줍기(ObjectId = 드랍 오브젝트 id), 인벤토리 아이템 버리기(스택 통째). 검증은 서버가 한다
	void SendPickupItem(int32 ObjectId);
	void SendDropItem(int64 ItemDbId);
	// A3.6: 소비 아이템 사용(ItemDbId 0이면 서버가 ItemId 종류 중에서 고른다), 액티브 칸 배치(Slot 1~5, ItemId 0 = 비우기)
	void SendUseItem(int64 ItemDbId, int32 ItemId);
	void SendSetQuickSlot(int32 Slot, int32 ItemId);
	// A6.3a: 1:1 거래. 요청(대상 오브젝트 id)·응답·제시 전체 교체(아이템 = (ItemDbId, 수량))·잠금·확정·취소. 검증은 서버가 한다
	// 상점 NPC(L4). 거리·거래 중·골드·빈 칸 검증은 서버가 한다. 성공은 SC_SHOP_OPEN / SC_INVENTORY_UPDATE, 거부는 SC_ITEM_RESULT
	void SendShopOpen(int32 NpcId);
	void SendShopBuy(int32 NpcId, int32 ItemId, int32 Count);
	void SendShopSell(int32 NpcId, int64 ItemDbId, int32 Count);
	void SendTradeRequest(int32 TargetId);
	void SendTradeRespond(bool bAccept);
	void SendTradeSetOffer(const TArray<TPair<int64, int64>>& Items, int64 Gold);
	void SendTradeLock(bool bLocked);
	void SendTradeConfirm();
	void SendTradeCancel();
	// 스킬 조준 켜기/끄기(MC4.2d-1). 상태가 바뀔 때만, Seq는 보낼 때마다 1씩 올린 값. 확정(공격)은 서버가 함께 해제하므로 끄기를 보내지 않는다
	// 부활 요청(A2.4). 사망 여부·대기 시간은 서버가 검증하고, 받아들이면 SC_REVIVE가 온다
	void SendRevive();
	// 포털 사용 요청(L5, D-203). 거리·상태는 서버가 검증하고, 받아들이면 SC_TELEPORT가 온다(거부는 응답 없음)
	void SendPortal(int32 PortalId);
	void SendSkillAim(int32 Slot, bool bActive, uint32 Seq);   // Part = Protocol::EquipPart(1~7), ItemId 0 = 해제. 검증은 서버가 한다

public:
	// 서버가 로그인 응답(SC_LOGIN map_name)으로 알려 준 레벨을 연다(D-185). 레벨은 /Game/Maps/<이름>에 있어야 한다.
	void EnterGame(const FString& MapName);
	// 연결을 닫고 로그인 화면(레벨)으로 돌아가 Message를 시스템 메시지로 보여준다(SC_KICK, 게임 중 연결 끊김 — A2.7).
	// 로그인 레벨의 LoginGameMode가 서버에 다시 연결한다.
	void ReturnToLogin(const FText& Message);
	void SwitchGameMapLevel(FName LevelToLoad);

public:
	int32 NetworkId = -1;

	// 시험용 자동 로그인(ULoginWidget::TryTestAutoLogin)을 이미 시도했는지. 로그인 화면으로 돌아왔을 때(SC_KICK 등) 되풀이하지 않게 한다.
	bool bTestAutoLoginTried = false;

private:

	TSharedPtr<NetworkSession> Packet_Session;

};
