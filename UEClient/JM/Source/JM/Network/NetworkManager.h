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

	// 월드 컨텍스트 오브젝트(액터, 위젯 등)로 NetworkManager를 얻는다. GameInstance가 없으면 nullptr.
	static UNetworkManager* Get(const UObject* WorldContext);

	// 패킷별 송신 함수. 패킷 구성과 SendBuffer 생성은 이 함수들에서만 한다.
	void SendLogin(const FString& AccountId, const FString& AccountPw);
	void SendSignUp(const FString& AccountId, const FString& AccountPw);
	void SendEnterRoom();
	void SendMove(const Protocol::PositionInfo& PosInfo, bool bForce);
	void SendAttack(int32 SkillSlot, int32 TargetId, float AimYaw, float AimX, float AimY);

public:
	void EnterGame();
	void SwitchGameMapLevel(FName LevelToLoad);


public:

	UFUNCTION(BlueprintCallable, Category = "Network")
	void SendAttackPacket();

public:
	int32 NetworkId = -1;

private:

	TSharedPtr<NetworkSession> Packet_Session;

};
