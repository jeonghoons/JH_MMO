#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "LandscapeBuildLibrary.generated.h"

class UMaterialInterface;

// 맵 제작용 지형 생성 도우미(에디터 전용, Docs/DESIGN_WORLD.md L2a).
// 에디터 MCP와 에디터 Python에는 Landscape를 만드는 함수가 없어 여기서 만들고 jm_mcp WorldTools(jm_kit.world)가 감싼다.
// 결과 문자열이 "ERROR:"로 시작하면 실패(레벨을 바꾸지 않음).
UCLASS()
class JMEDITORTOOLS_API ULandscapeBuildLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	// 지금 열린 레벨에 Landscape 하나를 만든다(원점 중심).
	// HeightFile = uint16 리틀 엔디언 SizeVerts × SizeVerts(행 = Y, 열 = X). 값 32768 = 높이 0, 1 단위 = ZScale / 128 cm.
	// SizeVerts - 1은 63의 배수(컴포넌트 한 변 63칸). SpacingCm = 정점 간격.
	// bReplaceExisting이면 레벨에 있던 Landscape를 먼저 지운다(재질을 안 주면 옛 재질을 이어받음).
	UFUNCTION(BlueprintCallable, Category = "JM|World")
	static FString CreateLandscape(const FString& HeightFile, int32 SizeVerts, float SpacingCm, float ZScale, UMaterialInterface* Material, bool bReplaceExisting);

	// CreateLandscape + 재질 레이어 가중치(L2c). LayerNames가 비면 CreateLandscape와 같다.
	// LayerNames = 레이어 이름(지형 재질의 LandscapeLayerSample 이름과 같아야 함).
	// WeightFile = uint8, 레이어 순서대로 SizeVerts × SizeVerts씩 이어 붙임(행 = Y, 열 = X). 정점마다 레이어 합 = 255.
	// LayerInfoDir = 레이어 정보 에셋 폴더(/Game/...). LI_<이름>이 없으면 만든다(가중치 섞기 켬). 저장은 부르는 쪽이 한다.
	UFUNCTION(BlueprintCallable, Category = "JM|World")
	static FString CreateLandscapeLayered(const FString& HeightFile, int32 SizeVerts, float SpacingCm, float ZScale, UMaterialInterface* Material, bool bReplaceExisting,
		const TArray<FString>& LayerNames, const FString& WeightFile, const FString& LayerInfoDir);

	// 지금 열린 레벨의 Landscape 요약(JSON): 개수, 컴포넌트 수, 위치·배율, 경계, 재질, 레이어(이름 + 그 레이어가 칠해진 컴포넌트 수).
	UFUNCTION(BlueprintCallable, Category = "JM|World")
	static FString DescribeLandscape();

	// ALevelMarker를 하나 놓는다(서버 Logic JSON의 원본 — MapExportCommandlet이 내보냄). Type = PlayerStart / MonsterSpawn / Npc / PortalTrigger.
	// TemplateId·Radius·MaxCount는 MonsterSpawn(몬스터 ID·스폰 반경·수), Npc(TemplateId), PortalTrigger(Radius·TargetMap·TemplateId = 대상 PointId)에 쓴다.
	UFUNCTION(BlueprintCallable, Category = "JM|World")
	static FString SpawnLevelMarker(const FString& Type, int32 PointId, FVector Location, float Yaw, int32 TemplateId, float Radius, int32 MaxCount, const FString& TargetMap, const FString& Label);

	// 지금 열린 레벨의 ALevelMarker를 모두 지우고 개수를 돌려준다.
	UFUNCTION(BlueprintCallable, Category = "JM|World")
	static int32 ClearLevelMarkers();

	// 지금 열린 레벨의 ALevelMarker 목록(JSON): [{label, pointId, type, x, y, z}]
	UFUNCTION(BlueprintCallable, Category = "JM|World")
	static FString ListLevelMarkers();
};
