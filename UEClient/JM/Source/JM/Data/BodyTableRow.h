#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "Engine/SkeletalMesh.h"
#include "Materials/MaterialInterface.h"
#include "Animation/AnimInstance.h"
#include "BodyTableRow.generated.h"

// Common/DataTable/BodyTable.csv 의 한 행(클라이언트 전용, 서버는 읽지 않는다). 컬럼명은 CSV 헤더와 1:1이다.
// RowName = PlayerType 숫자(1 MALE, 2 FEMALE). jm_kit.item_table.export가 kit_profiles.json에서 생성한다(MC4.1).
USTRUCT(BlueprintType)
struct FBodyTableRow : public FTableRowBase
{
	GENERATED_BODY()

	// 보이는 몸(키트 Body_Combined — 얼굴 섹션 + 몸 섹션). 애니메이션을 재생하는 Leader.
	UPROPERTY(EditAnywhere, BlueprintReadOnly) TSoftObjectPtr<USkeletalMesh> Leader;
	// Leader Pose로 Leader를 따라가는 키트 부위
	UPROPERTY(EditAnywhere, BlueprintReadOnly) TSoftObjectPtr<USkeletalMesh> Eyes;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) TSoftObjectPtr<USkeletalMesh> Teeth;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) TSoftObjectPtr<USkeletalMesh> Hair;
	// 몸 섹션에 씌우는 마스크 머티리얼(MI_Body_<M|F>). 장비가 덮는 영역을 FBodyMaskParams로 지운다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly) TSoftObjectPtr<UMaterialInterface> BodyMaterial;
	// Leader의 AnimBP(ABP_PlayerBase)
	UPROPERTY(EditAnywhere, BlueprintReadOnly) TSoftClassPtr<UAnimInstance> AnimClass;
};
