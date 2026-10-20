#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "MovementTableRow.generated.h"

// Common/DataTable/MovementTable.csv 의 한 행. 컬럼명은 CSV 헤더와 1:1이다. (Docs/DESIGN_DATA.md)
// RowName = ID 문자열 (플레이어 기본값 = "1001").
USTRUCT(BlueprintType)
struct FMovementTableRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly) float MaxSpeedCmSec = 600.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float AccelCmSec2 = 2048.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float BrakeDecelCmSec2 = 2048.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float RotationRateYawDegSec = 500.f;

	// 3인칭 마우스 회전 카메라(검은사막 방식): 카메라 거리, 시작 피치(음수 = 아래로 내려다봄), 피치 제한, 마우스 감도
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float CameraArmLengthCm = 450.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float CameraDefaultPitchDeg = -15.f;
	// 크로스헤어를 화면 중앙에 두고 캐릭터를 화면 아래쪽에 보이게 하기 위해 카메라 피벗을 캐릭터 위로 올린다(검은사막 방식).
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float CameraPivotHeightCm = 150.f;
	// 어깨 너머 시점: 카메라를 캐릭터 오른쪽(+)/왼쪽(-)으로 옮긴다. 0 = 캐릭터 바로 뒤
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float CameraShoulderOffsetCm = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly) float CameraMinPitchDeg = -60.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float CameraMaxPitchDeg = 30.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float LookSensitivityYaw = 1.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float LookSensitivityPitch = 1.f;
};