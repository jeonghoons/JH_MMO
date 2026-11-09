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

	// 원격 캐릭터: 서버 위치(DestPosition)로 보간하고 속도로 앞질러 간다. 원격 플레이어는 마지막 패킷 이후 RemoteExtrapolateMaxMs까지만.
	void ApplyNetworkMovement(float DeltaTime);

	void SyncTransformToInfo();

public:	
	const Protocol::ObjectInfo& GetObjectInfo() const { return ObjectInfo; }
	bool IsDeadActor() const { return bIsDead; }
	// 서버가 확정한 시전 방향(SC_ATTACK.aim_yaw)으로 즉시 돌린다.
	void FaceYaw(float Yaw);
	// 시전 직후 Seconds 동안 이동 방향 회전을 끄고 현재 방향을 유지한다. 이동하며 시전해도 조준 방향이 바로 풀리지 않게 한다.
	void LockFacing(float Seconds);
	// 스킬의 타격 시점(HitDelayMs ÷ 재생 배율)까지 방향을 유지한다.
	void LockFacingForSkill(const struct FSkillTableRow& Skill);
	// 시전 애니메이션 재생 배율. 기본 공격(Slot 0, 또는 SkillTable에 없는 공격)은 서버 스탯 AttackSpeed, 스킬은 1.
	// 서버도 같은 배율로 행동 시간·판정 시점을 나눈다. (Docs/DECISIONS.md 2026-10-28)
	float GetAttackPlayRate(const struct FSkillTableRow* Skill) const;
	const Protocol::PositionInfo& GetDestInfo() const { return DestPosition; }
	Protocol::MoveState GetMoveState() const { return ObjectInfo.position().state(); }
	FVector GetDestVelocity() const { return FVector(DestPosition.v_x(), DestPosition.v_y(), DestPosition.v_z()); }

	void SetDestInfo(const Protocol::PositionInfo& PosInfo);

public:
	virtual void SetPlayerData(const Protocol::ObjectInfo& ObjInfo);
	virtual void OnDamaged(int32_t Damage, int32_t RemainHP);
	virtual void OnDead();
	// Skill이 있고 Montage가 지정돼 있으면 그 몽타주를, 아니면 기본 공격 몽타주를 재생한다. (몬스터 스킬은 SkillTable에 없을 수 있음)
	virtual void OnAttack(const struct FSkillTableRow* Skill);

	UFUNCTION()
	void OnHitMontageEnded(UAnimMontage* Montage, bool bInterrupted);
	UFUNCTION()
	void OnDeathMontageEnded(UAnimMontage* Montage, bool bInterrupted);


	UFUNCTION(BlueprintCallable, Category = "Action")
	void PlayAttackAnim(float PlayRate = 1.f);

	// 스킬 조준 상태(MC4.2d-1). SkillId 0 = 끔. 원격 캐릭터는 SC_SKILL_AIM·스폰(ObjectInfo.aim_skill_id)·암묵 해제(SC_ATTACK·SC_DEAD·무기 교체)로,
	// 내 캐릭터는 로컬 입력(AJMMyPlayer)으로 정한다. 이동 속도에 SkillTable.AimMoveSpeedMul을 곱한다(서버 이동 예산과 같은 배율).
	virtual void SetSkillAiming(int32 SkillId);
	bool IsSkillAiming() const { return AimSkillId != 0; }
	int32 GetAimSkillId() const { return AimSkillId; }
	// MaxWalkSpeed = 서버 스탯 move_speed × 조준 배율. 스탯이 바뀌거나 조준이 바뀔 때 부른다.
	void RefreshMoveSpeed();

	// 무기 자세(MC4.2d-2, UJMAnimInstance.WeaponStance): 0 = 등에 멤(맨손 이동), 1 = 손에 듦, 2 = 스킬 조준. 등에 메지 않는 캐릭터는 1/2.
	virtual int32 GetWeaponStance() const { return IsSkillAiming() ? 2 : 1; }
	// 장착 무기의 넣기 값(UEquipmentComponent가 로드 뒤 호출). bCanSheathe가 false면 무기를 항상 손에 든다.
	virtual void SetSheatheData(bool bCanSheathe, float DelaySec, class UAnimMontage* Montage, float SwapSec) {}

	// 피격·사망 몽타주를 데이터(BodyTable)에서 넣는다(MC4.2, UEquipmentComponent가 로드 뒤 호출). nullptr면 그 몽타주를 재생하지 않는다.
	void SetReactionMontages(UAnimMontage* Hit, UAnimMontage* Dead);

	
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

	int32 AimSkillId = 0;
	float AimSpeedMul = 1.f;
	// 직접 조종하는 캐릭터(내 캐릭터·테스트 플레이어)는 조준 중 카메라 방향을 본다(MC4.2d-2). 원격은 서버 yaw를 따른다.
	void ApplyAimRotationMode(bool bAim);
	bool bAimRotationActive = false;
	bool bAimSavedOrient = false;

protected:
	Protocol::ObjectInfo				ObjectInfo;
	Protocol::PositionInfo				DestPosition;

	// 원격 캐릭터 이동 동기화 수치(MovementTable 1001, BeginPlay에서 읽음). 여기 값은 테이블이 없을 때의 대비용이다.
	float RemoteInterpSpeed = 10.f;
	float RemoteSnapDistCm = 500.f;
	float RemoteExtrapolateMaxSec = 0.3f;
	double LastNetUpdateTime = 0.0;   // 마지막으로 서버 위치를 받은 시각(월드 시간)
	float RemoteCatchUpSpeedMul = 0.f;   // 따라잡기 속도 상한 배율(0 = 끔, MC4.1b)

public:
	bool IsMyPlayer = false;
};
