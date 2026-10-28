#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "Particles/ParticleSystem.h"
#include "Animation/AnimMontage.h"
#include "Engine/Texture2D.h"
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
	Zone
};

// Common/DataTable/SkillTable.csv 의 한 행. 컬럼명은 CSV 헤더와 1:1이다.
// RowName = skillId 문자열. 수치의 권위는 서버이며, 클라이언트는 조준 인디케이터/입력 분기에만 사용한다.
USTRUCT(BlueprintType)
struct FSkillTableRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32 PlayerType = 0;   // 영웅(Protocol::PlayerType)
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32 Slot = 0;         // 0 = 기본 공격, 1~5 = 숫자키
	UPROPERTY(EditAnywhere, BlueprintReadOnly) ESkillTargetingType TargetingType = ESkillTargetingType::NonTargeted;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) ESkillHitShape HitShape = ESkillHitShape::Sector;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32 HitDelayMs = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32 CooldownMs = 0;   // 스킬 쿨타임(시전 순간부터). 기본 공격은 0
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32 AnimMs = 0;       // 행동 시간 = 애니메이션 1회. 기본 공격은 AttackSpeed로 나눈다
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float CastRange = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float Radius = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float Angle = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float Range = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float Width = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32 BaseDamage = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float ProjSpeed = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32 DurationMs = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32 TickMs = 0;

	// 클라이언트 전용(서버 로더는 읽지 않음). 시전 몽타주. 비어 있으면 DT_CharacterAsset의 기본 공격 몽타주를 쓴다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly) TSoftObjectPtr<UAnimMontage> Montage;

	// 클라이언트 전용(서버 로더는 읽지 않음). 스킬 이펙트. 장판은 조준 지점, 부채꼴·직선은 시전자 발밑에서 재생한다.
	// 비어 있으면 장판만 디버그 원으로 표시한다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly) TSoftObjectPtr<UParticleSystem> Fx;
	// 이펙트가 배율 1일 때 덮는 반지름. 재생 배율 = Radius / FxBaseRadius 이므로 조준 인디케이터(Radius)와 크기가 같아진다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float FxBaseRadius = 0.f;

	// 클라이언트 전용(서버 로더는 읽지 않음). 전투 HUD 스킬 슬롯 아이콘. 비어 있으면 키 글자만 표시한다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly) TSoftObjectPtr<UTexture2D> Icon;
};