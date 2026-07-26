#pragma once
#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"

#include "NetworkManager.generated.h"

class NetworkSession;

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


private:
	int32 NetworkId = -1;
	TSharedPtr<NetworkSession> Packet_Session;
};
