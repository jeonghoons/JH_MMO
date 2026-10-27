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
	void HandleAttack(int32 AttackerId, int32 SkillId, float AimYaw, float AimX, float AimY);
	void HandleDamage(int32 AttackerId, int32 TargetId, int32 Damage, int32 RemainHp);
	void HandleDead(int32 CharacterId);

private:
	// SkillTable의 Fx를 HitShape에 맞춰 재생한다. 장판은 조준 지점에 DurationMs 동안, 부채꼴·직선은 타격 시점에 시전자 발밑에서 1회.
	void SpawnSkillFx(AJMCharacterBase* Attacker, const struct FSkillTableRow& Skill, float AimYaw, float AimX, float AimY);

public:
	UPROPERTY()
	TObjectPtr<AJMMyPlayer> MyPlayer;
	UPROPERTY()
	TMap<int32, TObjectPtr<AJMCharacterBase>> Objects;
};
