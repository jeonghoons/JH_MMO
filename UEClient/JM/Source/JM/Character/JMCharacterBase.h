#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "Protocol/Protocol.pb.h"
#include "Data/CharacterAssetData.h"
#include "JMCharacterBase.generated.h"

UCLASS()
class JM_API AJMCharacterBase : public ACharacter
{
	GENERATED_BODY()
public:
	AJMCharacterBase();

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;

	void ApplyNetworkMovement(float DeltaTime);

	void SyncTransformToInfo();

public:	
	const Protocol::ObjectInfo& GetObjectInfo() const { return ObjectInfo; }
	bool IsDeadActor() const { return bIsDead; }
	// 서버가 확정한 시전 방향(SC_ATTACK.aim_yaw)으로 즉시 돌린다.
	void FaceYaw(float Yaw);
	// 시전 직후 Seconds 동안 이동 방향 회전을 끄고 현재 방향을 유지한다. 이동하며 시전해도 조준 방향이 바로 풀리지 않게 한다.
	void LockFacing(float Seconds);
	// 스킬의 타격 시점(HitDelayMs)까지 방향을 유지한다.
	void LockFacingForSkill(const struct FSkillTableRow& Skill);
	const Protocol::PositionInfo& GetDestInfo() const { return DestPosition; }
	Protocol::MoveState GetMoveState() const { return ObjectInfo.position().state(); }
	FVector GetDestVelocity() const { return FVector(DestPosition.v_x(), DestPosition.v_y(), DestPosition.v_z()); }

	void SetDestInfo(const Protocol::PositionInfo& PosInfo);

public:
	virtual void SetPlayerData(const Protocol::ObjectInfo& ObjInfo);
	virtual void OnDamaged(int32_t Damage, int32_t RemainHP);
	virtual void OnDead();
	virtual void OnAttack();

	UFUNCTION()
	void OnHitMontageEnded(UAnimMontage* Montage, bool bInterrupted);
	UFUNCTION()
	void OnDeathMontageEnded(UAnimMontage* Montage, bool bInterrupted);


	UFUNCTION(BlueprintCallable, Category = "Action")
	void PlayAttackAnim();

	
protected:
	UPROPERTY(EditAnywhere) 
	class UAnimMontage* CurrentAttackMontage;

	UPROPERTY(EditAnywhere) 
	class UAnimMontage* CurrentHitMontage;

	UPROPERTY(EditAnywhere) 
	class UAnimMontage* CurrentDeadMontage;
	
	// 사망 몽타주의 마지막 포즈를 유지하기 위해, 몽타주가 끝나기 직전에 애니메이션을 멈춘다.
	FTimerHandle DeathFreezeTimer;

	FTimerHandle FacingLockTimer;
	bool bSavedOrientToMovement = false;
	void UnlockFacing();

	// 사망한 뒤에는 서버가 보내는 좌표로 보간하지 않는다. 서버 Z는 바닥 높이라서 따라가면 캡슐 중심이 바닥까지 내려와 몸이 땅에 박힌다.
	bool bIsDead = false;

protected:
	Protocol::ObjectInfo				ObjectInfo;
	Protocol::PositionInfo				DestPosition;

public:
	bool IsMyPlayer = false;
};
