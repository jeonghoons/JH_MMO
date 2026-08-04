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
	virtual void Tick(float DeltaTime) override;
	virtual bool IsTickable() const override { return true; }
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UNetworkManager, STATGROUP_Tickables); }

public:
	UFUNCTION(BlueprintCallable)
	void ConnectToServer();

	UFUNCTION(BlueprintCallable)
	void DisconnectFromServer();

	void SendPacket(TSharedPtr<class SendBuffer> sendBuffer);

public:
	void EnterGame();
	void SwitchGameMapLevel(FName LevelToLoad);

public:
	int32 NetworkId = -1;

private:
	
	TSharedPtr<NetworkSession> Packet_Session;

};
