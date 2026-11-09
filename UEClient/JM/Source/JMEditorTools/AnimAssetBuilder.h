#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "AnimAssetBuilder.generated.h"

// MC4.2 애니메이션 에셋 빌더(에디터 전용). 공식 MCP의 그래프 DSL이 AnimGraph를 다루지 못해 C++로 만든다.
// 각 함수는 BlueprintCallable 정적 함수라 에디터 Python(unreal.AnimAssetBuilder.*)에서 부를 수 있고, jm_mcp 툴셋이 감싼다.
// 결과는 사람이 읽는 텍스트(성공/실패 사유)를 돌려준다. 실패하면 저장하지 않는다.
UCLASS()
class JMEDITORTOOLS_API UAnimAssetBuilder : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	// AnimBP의 모든 그래프(AnimGraph·스테이트 머신·스테이트·전이·레이어·이벤트)를 노드·핀·연결·Anim 노드 속성까지 텍스트로 덤프한다.
	UFUNCTION(BlueprintCallable, Category = "JM|AnimAssetBuilder")
	static FString DumpAnimBlueprint(const FString& BlueprintPath);

	// Animation Layer Interface(ALI_*)를 만들고 레이어(그래프)를 추가한다. LayerHasInputPose[i]가 true면 입력 포즈(InPose)가 있는 레이어.
	// PackagePath 예: /Game/Character/Mannequin/Anims/ALI_Weapon. 이미 있으면 실패.
	UFUNCTION(BlueprintCallable, Category = "JM|AnimAssetBuilder")
	static FString CreateLayerInterface(const FString& PackagePath, const TArray<FString>& LayerNames, const TArray<bool>& LayerHasInputPose);

	// 인터페이스를 구현하는 레이어 AnimBP(ABP_Layer_*)를 만든다. 구현 그래프는 엔진이 인터페이스에 맞춰 만든다(입력 포즈 노드 포함).
	UFUNCTION(BlueprintCallable, Category = "JM|AnimAssetBuilder")
	static FString CreateLayerAnimBlueprint(const FString& PackagePath, const FString& InterfacePath, const FString& SkeletonPath, const FString& ParentClassPath);

	// 레이어 그래프를 "블렌드스페이스 플레이어 → 결과"로 채운다. X/Y 핀은 멤버 변수(부모 클래스 프로퍼티 포함) Get 노드에 잇는다(비우면 연결 안 함).
	UFUNCTION(BlueprintCallable, Category = "JM|AnimAssetBuilder")
	static FString SetLayerBlendSpace(const FString& BlueprintPath, const FString& LayerName, const FString& BlendSpacePath, const FString& XVariable, const FString& YVariable);

	// 1D 블렌드스페이스(BS_*)를 만든다(MC4.2c). 샘플 i = SequencePaths[i]를 SampleValues[i] 위치에, 샘플 재생 배율 RateScales[i]
	// (비우면 1 — 모션 속도와 샘플 위치가 다를 때 발 미끄러짐 보정, 예: 382cm/s 달리기를 이동 속도 500 위치에 1.31배). 이미 있으면 실패.
	UFUNCTION(BlueprintCallable, Category = "JM|AnimAssetBuilder")
	static FString CreateBlendSpace1D(const FString& PackagePath, const FString& SkeletonPath, const FString& AxisName, float AxisMin, float AxisMax,
		const TArray<FString>& SequencePaths, const TArray<float>& SampleValues, const TArray<float>& RateScales);

	// 레이어 그래프를 "블렌드스페이스(기본) + 덮기 블렌드스페이스를 BranchBone 아래에만 레이어 블렌드 → 결과"로 채운다(MC4.2c 왼팔 맨손 덮기).
	// 기본 X ← XVariable, 덮기 X/Y ← OverlayXVariable/OverlayYVariable(비우면 연결 안 함), 블렌드 가중치 ← AlphaVariable(멤버 변수).
	// 메시 공간 회전 블렌드를 켠다(상체 자세가 달라도 팔 방향 유지).
	UFUNCTION(BlueprintCallable, Category = "JM|AnimAssetBuilder")
	static FString SetLayerBlendSpaceWithOverride(const FString& BlueprintPath, const FString& LayerName, const FString& BlendSpacePath, const FString& XVariable,
		const FString& OverlayBlendSpacePath, const FString& OverlayXVariable, const FString& OverlayYVariable, const FString& BranchBone, const FString& AlphaVariable);

	// 레이어 그래프를 "입력 포즈 → 결과" 통과로 채운다(상체 포즈 레이어의 기본 구현).
	UFUNCTION(BlueprintCallable, Category = "JM|AnimAssetBuilder")
	static FString SetLayerPassThrough(const FString& BlueprintPath, const FString& LayerName);

	// 시퀀스로 몽타주를 만든다. SlotName(UpperBody/DefaultSlot), bAutoBlendOut(사망은 false), SkillHitTimeSec >= 0이면 그 시각에 AnimNotify_SkillHit를 단다.
	UFUNCTION(BlueprintCallable, Category = "JM|AnimAssetBuilder")
	static FString CreateMontage(const FString& SequencePath, const FString& PackagePath, const FString& SlotName, bool bAutoBlendOut, float SkillHitTimeSec);

	// 시퀀스를 Samples개로 나눠 본의 루트 기준 위치(앞쪽 = 메시 공간 Y, 위 = Z, 옆 = X)를 표로 돌려준다. 타격 시점(손이 가장 앞으로 뻗는 순간) 고르기용.
	UFUNCTION(BlueprintCallable, Category = "JM|AnimAssetBuilder")
	static FString MeasureBoneReach(const FString& SequencePath, const FString& BoneName, int32 Samples);

	// ABP_PlayerBase 재배선(MC4.2a):
	//  1) LocomotionStateName 스테이트의 내용을 "Linked Anim Layer(Interface, LocomotionLayer) → 결과"로 바꾼다.
	//  2) AnimGraph의 결과 앞에 "Linked Anim Layer(UpperBodyLayer, 입력 포즈) → Slot UpperBody → Slot DefaultSlot"을 끼운다(이미 있으면 건너뜀).
	//  3) LandStateName 스테이트에서 Locomotion으로 가는 전이를 "시퀀스 재생이 끝나면 자동"으로 바꾼다(스테이트가 없으면 건너뜀).
	// bSave가 false면 컴파일까지만 하고 저장하지 않는다(미리보기).
	UFUNCTION(BlueprintCallable, Category = "JM|AnimAssetBuilder")
	static FString RewirePlayerBase(const FString& BlueprintPath, const FString& InterfacePath, const FString& LocomotionLayer, const FString& UpperBodyLayer,
		const FString& LocomotionStateName, const FString& LandStateName, bool bSave);
};
