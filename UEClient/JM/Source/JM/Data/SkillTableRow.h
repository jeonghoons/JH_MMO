#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "Particles/ParticleSystem.h"
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
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32 CooldownMs = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float CastRange = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float Radius = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float Angle = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float Range = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float Width = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32 BaseDamage = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float ProjSpeed = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32 DurationMs = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32 TickMs = 0;

	// 클라이언트 전용(서버 로더는 읽지 않음). 지면 장판 이펙트. 비어 있으면 디버그 원으로 표시한다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly) TSoftObjectPtr<UParticleSystem> ZoneFx;
	// 이펙트가 배율 1일 때 덮는 반지름. 재생 배율 = Radius / ZoneFxBaseRadius 이므로 조준 원(Radius)과 크기가 같아진다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float ZoneFxBaseRadius = 0.f;
};