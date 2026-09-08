#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Protocol/Protocol.pb.h"
#include "JMObjectManager.generated.h"

class AJMCharacterBase;
class AJMMyPlayer;

UCLASS()
class JM_API UJMObjectManager : public UGameInstanceSubsystem
{
	GENERATED_BODY()
public:
	void ClearObjects();

	void HandleSpawn(const Protocol::ObjectInfo& ObjInfo);
	void HandleDespawn(int32 ObjectId);
	void HandleMove(const Protocol::ObjectInfo& ObjInfo);
	void HandleAttack(int32 AttackerId);
	void HandleDamage(int32 AttackerId, int32 TargetId, int32 Damage, int32 RemainHp);
	void HandleDead(int32 CharacterId);

public:
	UPROPERTY()
	TObjectPtr<AJMMyPlayer> MyPlayer;
	UPROPERTY()
	TMap<int32, TObjectPtr<AJMCharacterBase>> Objects;
};
