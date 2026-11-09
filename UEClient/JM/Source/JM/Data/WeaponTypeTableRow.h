#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Data/ItemTableRow.h"
#include "WeaponTypeTableRow.generated.h"

// Common/DataTable/WeaponTypeTable.csv 의 한 행(클라이언트 전용, 서버는 읽지 않는다). 컬럼명은 CSV 헤더와 1:1이다.
// RowName = EWeaponType 값(1 Unarmed, 2 Sword, 3 Bow — SkillTable ID 첫째 자리와 같다). 무기 "종류"마다 달라지는 연출 값을 둔다(DECISIONS 2026-11-08).
// 무기 타입을 추가하면 enum 항목과 이 표의 행, 레이어 AnimBP·몽타주만 더하면 된다(Docs/DESIGN_ANIMATION.md).
USTRUCT(BlueprintType)
struct FWeaponTypeTableRow : public FTableRowBase
{
	GENERATED_BODY()

	// 이 행의 무기 타입. RowName 숫자와 같아야 하며 UDataManager::FindWeaponType이 어긋나면 경고한다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly) EWeaponType WeaponType = EWeaponType::None;
	// ABP_PlayerBase에 LinkAnimClassLayers로 연결하는 레이어 AnimBP(ALI_Weapon 구현, ABP_Layer_<WeaponType>). 비면 레이어를 바꾸지 않는다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly) TSoftClassPtr<UAnimInstance> AnimLayer;

	// 무기 등에 메기(MC4.2d-2, DECISIONS 2026-11-09): 마지막 공격·조준 해제(손에 든 동안 피격 포함) 뒤 이 시간이 지나면 넣는다.
	// 0이면 이 무기 타입은 항상 손에 든다(지금 검 — MC4.3에서 확장). 아이템의 ItemTable.BackSocket도 있어야 넣는다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float SheatheDelaySec = 0.f;
	// 넣는 동작(상체 슬롯 몽타주)과 그 안에서 무기를 등 소켓으로 옮기는 시각(초, 손이 등에 닿는 순간 — 본 궤적 측정)
	UPROPERTY(EditAnywhere, BlueprintReadOnly) TSoftObjectPtr<UAnimMontage> SheatheMontage;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float SheatheSwapSec = 0.f;
};
