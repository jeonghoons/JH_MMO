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
	void SendAttack(int32 SkillSlot, int32 TargetId, float AimYaw, float AimX, float AimY);
	void SendChat(const FString& Message);   // 검증(길이·제어 문자·도배)은 서버가 한다
	// Part = Protocol::EquipPart(1~7), ItemDbId = 내 인벤토리의 아이템(UJMInventoryManager), 0 = 해제. 검증은 서버가 한다(A3.3)
	void SendChangeEquip(int32 Part, int64 ItemDbId);
	// 개발 지급 명령(A3.3). ItemId 0 = 골드. 서버 [Debug] DevCommands가 켜져 있을 때만 받는다
	void SendDevGive(int32 ItemId, int64 Count);
	// 인벤토리 칸 옮기기(A6.1). 대상 칸에 아이템이 있으면 서버가 맞바꾼다. 결과는 SC_INVENTORY_UPDATE.
	void SendMoveItem(int64 ItemDbId, int32 ToSlot);
	// 스킬 조준 켜기/끄기(MC4.2d-1). 상태가 바뀔 때만, Seq는 보낼 때마다 1씩 올린 값. 확정(공격)은 서버가 함께 해제하므로 끄기를 보내지 않는다
	// 부활 요청(A2.4). 사망 여부·대기 시간은 서버가 검증하고, 받아들이면 SC_REVIVE가 온다
	void SendRevive();
	void SendSkillAim(int32 Slot, bool bActive, uint32 Seq);   // Part = Protocol::EquipPart(1~7), ItemId 0 = 해제. 검증은 서버가 한다

public:
	void EnterGame();
	// 연결을 닫고 로그인 화면(레벨)으로 돌아가 Message를 시스템 메시지로 보여준다(SC_KICK, 게임 중 연결 끊김 — A2.7).
	// 로그인 레벨의 LoginGameMode가 서버에 다시 연결한다.
	void ReturnToLogin(const FText& Message);
	void SwitchGameMapLevel(FName LevelToLoad);

public:
	int32 NetworkId = -1;

private:

	TSharedPtr<NetworkSession> Packet_Session;

};
