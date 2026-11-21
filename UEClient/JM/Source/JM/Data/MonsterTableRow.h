#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "Engine/SkeletalMesh.h"
#include "MonsterTableRow.generated.h"

// Common/DataTable/MonsterTable.csv 의 한 행. 컬럼명은 CSV 헤더와 1:1이다. RowName = 몬스터 종류 ID(1001~) = ObjectInfo.template_id.
// 스탯·행동 열은 서버가 권위를 갖고(클라이언트는 ObjectInfo로 받은 값을 쓴다) 여기서는 임포트만 한다.
// 클라이언트가 실제로 쓰는 것은 외형 열(Scale, Mesh)이다 (MN1, D-198).
USTRUCT(BlueprintType)
struct FMonsterTableRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32 SkillId = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32 MaxHp = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32 AttackDamage = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float AttackSpeed = 1.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float MoveSpeed = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32 RespawnSec = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32 DeathRemoveDelaySec = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32 HitStunMs = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32 WanderRadiusCm = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32 AggroRangeCm = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32 LeashRangeCm = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32 ReturnMode = 0;

	// 크기 배율: 액터 전체(캡슐·메시·HP 막대 위치)에 곱한다. 서버는 같은 행의 BodyRadiusCm를 판정에 더한다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float Scale = 1.f;
	// 서버 판정용 몸 반경(기준 캡슐 반경 42cm × Scale). 클라이언트는 쓰지 않는다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32 BodyRadiusCm = 0;
	// 표시 레벨(이름표용, A2.3b — 서버 판정에는 쓰지 않음)과 처치 경험치(서버가 나눠 준다 — 클라이언트는 쓰지 않는다)
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32 Level = 1;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32 Exp = 0;
	// 이 종류의 스켈레탈 메시(스킨). 스켈레톤은 BP_Npc의 AnimBP와 같아야 한다. 비면 BP_Npc의 기본 메시를 그대로 쓴다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly) TSoftObjectPtr<USkeletalMesh> Mesh;
};
