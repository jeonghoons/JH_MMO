#include "LandscapeBuildLibrary.h"

#include "Editor.h"
#include "EngineUtils.h"
#include "Landscape.h"
#include "LandscapeComponent.h"
#include "LandscapeEditTypes.h"
#include "LandscapeInfo.h"
#include "LandscapeLayerInfoObject.h"
#include "LandscapeProxy.h"
#include "LandscapeUtils.h"
#include "LevelMarker.h"
#include "Materials/MaterialInterface.h"
#include "Misc/FileHelper.h"

namespace
{
	const int32 QuadsPerComponent = 63;   // 컴포넌트 한 변(섹션 1개 × 63칸)

	UWorld* GetEditorWorld()
	{
		return GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	}
}

FString ULandscapeBuildLibrary::CreateLandscape(const FString& HeightFile, int32 SizeVerts, float SpacingCm, float ZScale, UMaterialInterface* Material, bool bReplaceExisting)
{
	return CreateLandscapeLayered(HeightFile, SizeVerts, SpacingCm, ZScale, Material, bReplaceExisting, TArray<FString>(), FString(), FString());
}

FString ULandscapeBuildLibrary::CreateLandscapeLayered(const FString& HeightFile, int32 SizeVerts, float SpacingCm, float ZScale, UMaterialInterface* Material, bool bReplaceExisting,
	const TArray<FString>& LayerNames, const FString& WeightFile, const FString& LayerInfoDir)
{
	UWorld* World = GetEditorWorld();
	if (!World)
	{
		return TEXT("ERROR: 에디터 월드가 없음");
	}
	if (SizeVerts < QuadsPerComponent + 1 || (SizeVerts - 1) % QuadsPerComponent != 0)
	{
		return FString::Printf(TEXT("ERROR: SizeVerts - 1은 %d의 배수여야 함: %d"), QuadsPerComponent, SizeVerts);
	}
	if (SpacingCm <= 0.f || ZScale <= 0.f)
	{
		return TEXT("ERROR: SpacingCm·ZScale은 0보다 커야 함");
	}

	TArray<uint8> Raw;
	if (!FFileHelper::LoadFileToArray(Raw, *HeightFile))
	{
		return FString::Printf(TEXT("ERROR: 높이 파일을 읽지 못함: %s"), *HeightFile);
	}
	const int64 Expected = static_cast<int64>(SizeVerts) * SizeVerts * sizeof(uint16);
	if (Raw.Num() != Expected)
	{
		return FString::Printf(TEXT("ERROR: 높이 파일 크기 %d바이트, 기대 %lld바이트"), Raw.Num(), Expected);
	}

	TArray<uint16> Heights;
	Heights.SetNumUninitialized(SizeVerts * SizeVerts);
	FMemory::Memcpy(Heights.GetData(), Raw.GetData(), Raw.Num());

	// 재질 레이어 가중치(없으면 높이만). 기존 지형을 지우기 전에 파일·에셋을 모두 확인한다.
	const int32 VertCount = SizeVerts * SizeVerts;
	TArray<FLandscapeImportLayerInfo> ImportLayers;
	if (LayerNames.Num() > 0)
	{
		TArray<uint8> Weights;
		if (!FFileHelper::LoadFileToArray(Weights, *WeightFile))
		{
			return FString::Printf(TEXT("ERROR: 가중치 파일을 읽지 못함: %s"), *WeightFile);
		}
		const int64 ExpectedWeights = static_cast<int64>(VertCount) * LayerNames.Num();
		if (Weights.Num() != ExpectedWeights)
		{
			return FString::Printf(TEXT("ERROR: 가중치 파일 크기 %d바이트, 기대 %lld바이트(레이어 %d개)"), Weights.Num(), ExpectedWeights, LayerNames.Num());
		}
		if (!LayerInfoDir.StartsWith(TEXT("/Game/")))
		{
			return FString::Printf(TEXT("ERROR: LayerInfoDir는 /Game/으로 시작해야 함: %s"), *LayerInfoDir);
		}

		for (int32 i = 0; i < LayerNames.Num(); ++i)
		{
			const FName LayerName(*LayerNames[i]);
			const FString FileName = TEXT("LI_") + LayerNames[i];
			const FString ObjectPath = LayerInfoDir / FileName + TEXT(".") + FileName;
			ULandscapeLayerInfoObject* LayerInfo = LoadObject<ULandscapeLayerInfoObject>(nullptr, *ObjectPath, nullptr, LOAD_NoWarn | LOAD_Quiet);
			if (!LayerInfo)
			{
				LayerInfo = UE::Landscape::CreateTargetLayerInfo(LayerName, LayerInfoDir, FileName);
			}
			if (!LayerInfo)
			{
				return FString::Printf(TEXT("ERROR: 레이어 정보 에셋을 만들지 못함: %s"), *ObjectPath);
			}
			// 5.7부터 bNoWeightBlend 대신 BlendMethod. 기본값 None은 다른 레이어와 섞이지 않는다.
			if (LayerInfo->GetBlendMethod() != ELandscapeTargetLayerBlendMethod::FinalWeightBlending)
			{
				LayerInfo->SetBlendMethod(ELandscapeTargetLayerBlendMethod::FinalWeightBlending, /*bInModify*/ false);
				LayerInfo->MarkPackageDirty();
			}

			FLandscapeImportLayerInfo& Layer = ImportLayers.Emplace_GetRef(LayerName);
			Layer.LayerInfo = LayerInfo;
			Layer.LayerData.SetNumUninitialized(VertCount);
			FMemory::Memcpy(Layer.LayerData.GetData(), Weights.GetData() + static_cast<int64>(i) * VertCount, VertCount);
		}
	}

	// 기존 지형 처리
	TArray<ALandscapeProxy*> Existing;
	for (TActorIterator<ALandscapeProxy> It(World); It; ++It)
	{
		Existing.Add(*It);
	}
	if (Existing.Num() > 0 && !bReplaceExisting)
	{
		return FString::Printf(TEXT("ERROR: 레벨에 Landscape가 이미 %d개 있음(bReplaceExisting = true로 교체)"), Existing.Num());
	}
	for (ALandscapeProxy* Old : Existing)
	{
		if (!Material)
		{
			Material = Old->LandscapeMaterial;
		}
		World->EditorDestroyActor(Old, /*bShouldModifyLevel*/ true);
	}

	const FVector Scale(SpacingCm, SpacingCm, ZScale);
	const double Half = (SizeVerts - 1) * 0.5 * SpacingCm;
	ALandscape* Landscape = World->SpawnActor<ALandscape>(FVector(-Half, -Half, 0.0), FRotator::ZeroRotator);
	if (!Landscape)
	{
		return TEXT("ERROR: ALandscape 생성 실패");
	}
	Landscape->LandscapeMaterial = Material;
	Landscape->SetActorRelativeScale3D(Scale);
	Landscape->StaticLightingLOD = 0;

	// 엔진의 "새 Landscape" 버튼과 같은 순서(LandscapeEditorDetailCustomization_NewLandscape.cpp)
	TMap<FGuid, TArray<uint16>> HeightDataPerLayers;
	TMap<FGuid, TArray<FLandscapeImportLayerInfo>> MaterialLayerDataPerLayers;
	HeightDataPerLayers.Add(FGuid(), MoveTemp(Heights));
	MaterialLayerDataPerLayers.Add(FGuid(), ImportLayers);

	Landscape->Import(FGuid::NewGuid(), 0, 0, SizeVerts - 1, SizeVerts - 1, /*NumSubsections*/ 1, QuadsPerComponent,
		HeightDataPerLayers, nullptr, MaterialLayerDataPerLayers, ELandscapeImportAlphamapType::Additive, TArrayView<const FLandscapeLayer>());

	ULandscapeInfo* Info = Landscape->GetLandscapeInfo();
	if (Info)
	{
		Info->UpdateLayerInfoMap(Landscape);
	}
	// 가져온 레이어를 지형의 대상 레이어로 등록한다(엔진 "새 Landscape"와 같은 순서 — 등록해야 Landscape 모드의 칠하기 목록에 나온다).
	for (const FLandscapeImportLayerInfo& Layer : ImportLayers)
	{
		Landscape->AddTargetLayer(Layer.LayerName, FLandscapeTargetLayerSettings(Layer.LayerInfo));
		if (Info)
		{
			const int32 LayerIndex = Info->GetLayerInfoIndex(Layer.LayerName);
			if (LayerIndex != INDEX_NONE)
			{
				Info->Layers[LayerIndex].LayerInfoObj = Layer.LayerInfo;
			}
		}
	}
	Landscape->SetActorLabel(TEXT("Landscape"));
	Landscape->MarkPackageDirty();

	return FString::Printf(TEXT("OK: Landscape %d x %d 정점, 간격 %.1fcm(한 변 %.1fm), 컴포넌트 %d개, 레이어 %d개, 교체 %d개"),
		SizeVerts, SizeVerts, SpacingCm, (SizeVerts - 1) * SpacingCm / 100.f, Landscape->LandscapeComponents.Num(), ImportLayers.Num(), Existing.Num());
}

FString ULandscapeBuildLibrary::DescribeLandscape()
{
	UWorld* World = GetEditorWorld();
	if (!World)
	{
		return TEXT("ERROR: 에디터 월드가 없음");
	}

	FString Items;
	int32 Count = 0;
	for (TActorIterator<ALandscapeProxy> It(World); It; ++It)
	{
		ALandscapeProxy* Proxy = *It;
		const FBox Box = Proxy->GetComponentsBoundingBox(/*bNonColliding*/ true);
		const FVector Loc = Proxy->GetActorLocation();
		const FVector Scale = Proxy->GetActorScale3D();
		if (Count++ > 0)
		{
			Items += TEXT(",");
		}
		// 레이어마다 그 레이어의 가중치 맵을 가진 컴포넌트 수(0이면 가중치가 들어가지 않은 것)
		TMap<FName, int32> LayerComps;
		for (const TPair<FName, FLandscapeTargetLayerSettings>& Target : Proxy->GetTargetLayers())
		{
			LayerComps.Add(Target.Key, 0);
		}
		for (const ULandscapeComponent* Comp : Proxy->LandscapeComponents)
		{
			if (!Comp)
			{
				continue;
			}
			for (const FWeightmapLayerAllocationInfo& Alloc : Comp->GetWeightmapLayerAllocations())
			{
				// FWeightmapLayerAllocationInfo::GetLayerName은 엔진이 내보내지 않아(링크 실패) 레이어 정보에서 직접 읽는다.
				if (Alloc.LayerInfo)
				{
					LayerComps.FindOrAdd(Alloc.LayerInfo->GetLayerName())++;
				}
			}
		}
		FString Layers;
		for (const TPair<FName, int32>& Layer : LayerComps)
		{
			if (!Layers.IsEmpty())
			{
				Layers += TEXT(",");
			}
			Layers += FString::Printf(TEXT("{\"name\":\"%s\",\"components\":%d}"), *Layer.Key.ToString(), Layer.Value);
		}

		Items += FString::Printf(
			TEXT("{\"label\":\"%s\",\"components\":%d,\"location\":[%.1f,%.1f,%.1f],\"scale\":[%.2f,%.2f,%.2f],\"min\":[%.1f,%.1f,%.1f],\"max\":[%.1f,%.1f,%.1f],\"material\":\"%s\",\"layers\":[%s]}"),
			*Proxy->GetActorLabel(), Proxy->LandscapeComponents.Num(),
			Loc.X, Loc.Y, Loc.Z, Scale.X, Scale.Y, Scale.Z,
			Box.Min.X, Box.Min.Y, Box.Min.Z, Box.Max.X, Box.Max.Y, Box.Max.Z,
			Proxy->LandscapeMaterial ? *Proxy->LandscapeMaterial->GetPathName() : TEXT(""), *Layers);
	}
	return FString::Printf(TEXT("{\"count\":%d,\"landscapes\":[%s]}"), Count, *Items);
}

FString ULandscapeBuildLibrary::SpawnLevelMarker(const FString& Type, int32 PointId, FVector Location, float Yaw, int32 TemplateId, float Radius, int32 MaxCount, const FString& TargetMap, const FString& Label, const FString& ZoneName)
{
	UWorld* World = GetEditorWorld();
	if (!World)
	{
		return TEXT("ERROR: 에디터 월드가 없음");
	}

	EMarkerType MarkerType;
	FInstancedStruct Params;
	if (Type == TEXT("PlayerStart"))
	{
		MarkerType = EMarkerType::PlayerStart;
	}
	else if (Type == TEXT("MonsterSpawn"))
	{
		MarkerType = EMarkerType::MonsterSpawn;
		FMonsterSpawnParams P;
		P.MonsterTemplateId = TemplateId;
		P.SpawnRadius = Radius;
		P.MaxCount = MaxCount;
		P.ZoneName = ZoneName;
		Params = FInstancedStruct::Make(P);
	}
	else if (Type == TEXT("SafeZone"))
	{
		MarkerType = EMarkerType::SafeZone;
		FSafeZoneParams P;
		P.Radius = Radius;
		Params = FInstancedStruct::Make(P);
	}
	else if (Type == TEXT("Npc"))
	{
		MarkerType = EMarkerType::Npc;
		FNpcSpawnParams P;
		P.NpcTemplateId = TemplateId;
		Params = FInstancedStruct::Make(P);
	}
	else if (Type == TEXT("PortalTrigger"))
	{
		MarkerType = EMarkerType::PortalTrigger;
		FPortalTriggerParams P;
		P.TriggerRadius = Radius;
		P.TargetMap = TargetMap;
		P.TargetPointId = TemplateId;
		Params = FInstancedStruct::Make(P);
	}
	else
	{
		return FString::Printf(TEXT("ERROR: 모르는 마커 타입: %s"), *Type);
	}

	ALevelMarker* Marker = World->SpawnActor<ALevelMarker>(Location, FRotator(0.0, Yaw, 0.0));
	if (!Marker)
	{
		return TEXT("ERROR: ALevelMarker 생성 실패");
	}
	Marker->PointId = PointId;
	Marker->MarkerType = MarkerType;
	Marker->MarkerParams = Params;
	Marker->SetActorLabel(Label.IsEmpty() ? FString::Printf(TEXT("Marker_%s_%d"), *Type, PointId) : Label);
	Marker->SetFolderPath(TEXT("Markers"));
	Marker->MarkPackageDirty();
	return FString::Printf(TEXT("OK: %s"), *Marker->GetActorLabel());
}

int32 ULandscapeBuildLibrary::ClearLevelMarkers()
{
	UWorld* World = GetEditorWorld();
	if (!World)
	{
		return 0;
	}
	TArray<ALevelMarker*> Markers;
	for (TActorIterator<ALevelMarker> It(World); It; ++It)
	{
		Markers.Add(*It);
	}
	for (ALevelMarker* Marker : Markers)
	{
		World->EditorDestroyActor(Marker, /*bShouldModifyLevel*/ true);
	}
	return Markers.Num();
}

FString ULandscapeBuildLibrary::ListLevelMarkers()
{
	UWorld* World = GetEditorWorld();
	if (!World)
	{
		return TEXT("ERROR: 에디터 월드가 없음");
	}
	static const TCHAR* TypeNames[] = { TEXT("PlayerStart"), TEXT("MonsterSpawn"), TEXT("Npc"), TEXT("PortalTrigger"), TEXT("SafeZone") };
	FString Items;
	int32 Count = 0;
	for (TActorIterator<ALevelMarker> It(World); It; ++It)
	{
		const ALevelMarker* Marker = *It;
		const FVector Loc = Marker->GetActorLocation();
		const int32 TypeIndex = FMath::Clamp(static_cast<int32>(Marker->MarkerType), 0, 4);
		if (Count++ > 0)
		{
			Items += TEXT(",");
		}
		Items += FString::Printf(TEXT("{\"label\":\"%s\",\"pointId\":%d,\"type\":\"%s\",\"x\":%.1f,\"y\":%.1f,\"z\":%.1f}"),
			*Marker->GetActorLabel(), Marker->PointId, TypeNames[TypeIndex], Loc.X, Loc.Y, Loc.Z);
	}
	return FString::Printf(TEXT("[%s]"), *Items);
}
