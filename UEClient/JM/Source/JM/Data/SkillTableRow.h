#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "Particles/ParticleSystem.h"
#include "Animation/AnimMontage.h"
#include "Engine/Texture2D.h"
#include "Engine/StaticMesh.h"
#include "Data/ItemTableRow.h"
#include "SkillTableRow.generated.h"

// Common/DataTable/SkillTable.csv 의 enum 값과 이름이 같아야 한다. (서버 ServerData.h 의 HitShape / SkillTargeting)
UENUM(BlueprintType)
enum class ESkillTargetingType : uint8
{
	NonTargeted,   // 방향/지점을 지정한다
	Targeted       // 대상(액터)을 지정한다
};

UENUM(BlueprintType)
enum class ESkillHitShape : uint8
{
	Sector,
	Line,
	Projectile,
	Zone,
	Pierce     // 느린 관통 투사체(SK2, D-278): 조준 방향으로 ProjSpeed로 Range까지, 폭 Width 안의 대상을 지나가며 한 번씩 맞힘
};

// Common/DataTable/SkillTable.csv 의 한 행. 컬럼명은 CSV 헤더와 1:1이다.
// RowName = skillId 문자열. 수치의 권위는 서버이며, 클라이언트는 조준 인디케이터/입력 분기에만 사용한다.
USTRUCT(BlueprintType)
struct FSkillTableRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly) EWeaponType WeaponType = EWeaponType::Unarmed;   // 이 스킬을 쓰는 무기 타입 (ID 첫째 자리: 맨손 1, 검 2, 활 3, 몬스터 9)
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32 Slot = 0;         // 0 = 기본 공격, 1~5 = 숫자키
	UPROPERTY(EditAnywhere, BlueprintReadOnly) ESkillTargetingType TargetingType = ESkillTargetingType::NonTargeted;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) ESkillHitShape HitShape = ESkillHitShape::Sector;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32 HitDelayMs = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32 CooldownMs = 0;   // 스킬 쿨타임(시전 순간부터). 기본 공격은 0
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32 AnimMs = 0;       // 행동 시간 = 애니메이션 1회. 기본 공격은 AttackSpeed로 나눈다
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32 MoveLockMs = 0;   // 이동 잠금: 시전부터 휘두르기 끝(Swing End 노티파이)까지. 서버가 그동안 이동을 거부한다. 0 = 잠그지 않음. 기본 공격은 AttackSpeed로 나눈다 (D-199)
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float CastRange = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float Radius = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float Angle = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float Range = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float Width = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32 BaseDamage = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float ProjSpeed = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32 DurationMs = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32 TickMs = 0;
	// 스킬 조준(MC4.2d-1): 조준 중 이동 속도 배율(0 < 값 <= 1), 최대 유지 시간(0 = 조준할 수 없는 스킬). 서버도 같은 값으로 검증한다
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float AimMoveSpeedMul = 1.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32 AimMaxMs = 0;
	// 성장·MP(A2.3b, D-253): 시전에 드는 MP(서버가 검사·차감 — 부족하면 ATTACK_RESULT_NOT_ENOUGH_MP), 공격력 반영 %(피해 = BaseDamage + 공격력 × AttackScale / 100, 서버 계산)
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32 MpCost = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32 AttackScale = 100;

	// 클라이언트 전용(서버 로더는 읽지 않음). 시전 몽타주. 비어 있으면 캐릭터의 기본 공격 몽타주(`AJMCharacterBase::CurrentAttackMontage`)를 쓴다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly) TSoftObjectPtr<UAnimMontage> Montage;

	// 클라이언트 전용(서버 로더는 읽지 않음). 스킬 이펙트. 장판은 조준 지점, 부채꼴·직선은 시전자 발밑에서 재생한다.
	// 비어 있으면 장판만 디버그 원으로 표시한다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly) TSoftObjectPtr<UParticleSystem> Fx;
	// 이펙트가 배율 1일 때 덮는 반지름. 재생 배율 = Radius / FxBaseRadius 이므로 조준 인디케이터(Radius)와 크기가 같아진다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float FxBaseRadius = 0.f;
	// 클라이언트 전용(서버 로더는 읽지 않음, D-254). 맞은 대상 위치에 한 번 재생하는 피격 이펙트. 비어 있으면 재생하지 않는다.
	// SC_DAMAGE에는 스킬 ID가 없으므로 공격자의 마지막 SC_ATTACK 스킬로 찾는다. 투사체(HitShape Projectile)의 Fx는 날아가는 화살 몸체다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly) TSoftObjectPtr<UParticleSystem> HitFx;

	// 클라이언트 전용(서버 로더는 읽지 않음). 전투 HUD 스킬 슬롯 아이콘. 비어 있으면 키 글자만 표시한다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly) TSoftObjectPtr<UTexture2D> Icon;

	// 클라이언트 전용(SK2, D-278). 투사체 연출(AProjectileFx)에 싣는 메시(예: 3003 빛의 화살). 비어 있으면 Fx 파티클만 날아간다.
	// 크기: 활에서 나갈 때 ProjStartScale에서 ProjGrowMs 동안 ProjMeshScale까지 커진다(일반 화살 크기 → 판정 폭에 맞는 크기).
	UPROPERTY(EditAnywhere, BlueprintReadOnly) TSoftObjectPtr<UStaticMesh> ProjMesh;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float ProjMeshScale = 1.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float ProjStartScale = 1.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32 ProjGrowMs = 0;
};