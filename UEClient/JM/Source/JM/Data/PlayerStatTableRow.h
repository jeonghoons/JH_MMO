#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "PlayerStatTableRow.generated.h"

// Common/DataTable/PlayerStatTable.csv 의 한 행. 컬럼명은 CSV 헤더와 1:1이다. RowName = Name 열(1001 = 레벨 1 ...).
// 스탯 값의 권위는 서버다(ObjectInfo.stat으로 온다). 클라이언트는 HUD 경험치 바의 분모(ExpToNext)만 읽는다 (A2.3b, D-260).
USTRUCT(BlueprintType)
struct FPlayerStatTableRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32 Level = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32 MaxHp = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32 AttackDamage = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float AttackSpeed = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float MoveSpeed = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32 MaxMp = 0;
	// 이 레벨에서 다음 레벨까지 필요한 경험치(최대 레벨은 0)
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int64 ExpToNext = 0;
};
