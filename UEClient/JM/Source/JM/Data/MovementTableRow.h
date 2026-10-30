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

	// 최고 이동 속도는 여기 두지 않는다. 서버 스탯(StatTable.MoveSpeed, ObjectInfo.stat.move_speed)을 쓴다. (DECISIONS 2026-10-30 S1)
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

	// 이동 동기화(S1): 이동 중 위치 전송 주기, 원격 캐릭터 보간 속도·순간이동 거리·외삽 시간 상한
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32 MoveSendIntervalMs = 200;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float RemoteInterpSpeed = 10.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float RemoteSnapDistCm = 500.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32 RemoteExtrapolateMaxMs = 300;

	// 서버 이동 검증 허용치(서버 전용). 클라이언트는 ServerMinMoveIntervalMs만 전송 간격 하한으로 쓴다(이보다 잦으면 서버가 버림).
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float ServerSpeedTolerance = 1.2f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32 ServerMoveBudgetMs = 1000;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float ServerMoveSlackCm = 50.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32 ServerMinMoveIntervalMs = 50;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float ServerNavToleranceCm = 50.f;
};