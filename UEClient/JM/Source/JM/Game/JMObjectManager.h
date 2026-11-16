#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Protocol/Protocol.pb.h"
#include "JMObjectManager.generated.h"

class AJMCharacterBase;
class AJMMyPlayer;
class ADropItemActor;

UCLASS()
class JM_API UJMObjectManager : public UGameInstanceSubsystem
{
	GENERATED_BODY()
public:
	void ClearObjects();

	void HandleSpawn(const Protocol::ObjectInfo& ObjInfo);
	void HandleDespawn(int32 ObjectId);
	// 내 캐릭터에 대한 SC_MOVE는 서버가 내 이동을 거부했다는 보정이다(CorrectionSeq와 함께 적용).
	void HandleMove(const Protocol::ObjectInfo& ObjInfo, uint32 CorrectionSeq);
	// CooldownMs: 서버가 적용한 쿨타임(SC_ATTACK.cooldown_ms). 내 시전이면 HUD 슬롯에 표시한다.
	void HandleAttack(int32 AttackerId, int32 SkillId, float AimYaw, float AimX, float AimY, int32 CooldownMs);
	void HandleDamage(int32 AttackerId, int32 TargetId, int32 Damage, int32 RemainHp);
	// ReviveDelayMs: 내 캐릭터가 죽었을 때 부활 버튼이 켜지기까지의 시간(SC_DEAD.revive_delay_ms, 서버 값)
	void HandleDead(int32 CharacterId, int32 ReviveDelayMs);
	// SC_REVIVE(A2.4): 사망 해제·순간이동·HP 반영. 내 캐릭터면 보정 순번·HUD·부활 창까지.
	void HandleRevive(int32 CharacterId, const Protocol::PositionInfo& Pos, int32 Hp, uint32 CorrectionSeq);
	// SC_CHANGE_EQUIP: 장착 목록·스탯 반영, 내 캐릭터면 HUD(HP·무기 타입별 스킬 슬롯) 갱신
	void HandleChangeEquip(int32 ObjectId, Protocol::EquipPart Part, int32 ItemId, const Protocol::StatInfo& Stat);
	// SC_SKILL_AIM(MC4.2d-1): 원격은 그대로 반영, 내 캐릭터는 해제(거부·시간 초과)만 Seq가 맞을 때 반영한다.
	void HandleSkillAim(int32 ObjectId, int32 SkillId, bool bActive, uint32 Seq);

	// From에서 수평 거리 RangeCm 안의 가장 가까운 바닥 드랍(A6.2). 없으면 nullptr. 서버도 수평 거리로 판정한다.
	ADropItemActor* FindNearestDrop(const FVector& From, float RangeCm) const;

private:
	// 전투 HUD(UIManager에 HUD 이름으로 등록된 위젯). 로그인 화면처럼 HUD가 없으면 nullptr
	class UCombatHudWidget* GetCombatHud() const;

	// SkillTable의 Fx를 HitShape에 맞춰 재생한다. 장판은 조준 지점에 DurationMs 동안, 부채꼴·직선은 타격 시점에 시전자 발밑에서 1회.
	void SpawnSkillFx(AJMCharacterBase* Attacker, const struct FSkillTableRow& Skill, float AimYaw, float AimX, float AimY);

public:
	UPROPERTY()
	TObjectPtr<AJMMyPlayer> MyPlayer;
	UPROPERTY()
	TMap<int32, TObjectPtr<AJMCharacterBase>> Objects;
	// 바닥 드랍(OBJECT_TYPE_ITEM). Objects는 캐릭터 전용이라 따로 둔다 (A6.2, D-157)
	UPROPERTY()
	TMap<int32, TObjectPtr<ADropItemActor>> Drops;
};
