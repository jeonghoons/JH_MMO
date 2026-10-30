#include "MapExportCommandlet.h"
#include "LevelMarker.h"

#include "EngineUtils.h"
#include "Editor.h"
#include "FileHelpers.h"
#include "Components/StaticMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Interfaces/Interface_CollisionDataProvider.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"
#include "JsonObjectConverter.h"

namespace
{
	// USTRUCT 프로퍼티 이름(PascalCase)을 Logic JSON 스키마(camelCase)에 맞춘다.
	// 필드 이름에 약어가 없는 이번 스키마 한정으로는 첫 글자만 소문자로 바꾸면 충분하다.
	FString ToCamelCase(const FString& In)
	{
		if (In.IsEmpty()) return In;
		FString Out = In;
		Out[0] = FChar::ToLower(Out[0]);
		return Out;
	}

	FString MarkerTypeToString(EMarkerType Type)
	{
		switch (Type)
		{
		case EMarkerType::PlayerStart:   return TEXT("PlayerStart");
		case EMarkerType::MonsterSpawn:  return TEXT("MonsterSpawn");
		case EMarkerType::Npc:           return TEXT("Npc");
		case EMarkerType::PortalTrigger: return TEXT("PortalTrigger");
		default:                         return TEXT("Unknown");
		}
	}
}

UMapExportCommandlet::UMapExportCommandlet()
{
	IsClient = false;
	IsEditor = true;
	IsServer = false;
	LogToConsole = true;
}

int32 UMapExportCommandlet::Main(const FString& Params)
{
	FString MapName;
	if (!FParse::Value(*Params, TEXT("Map="), MapName))
	{
		UE_LOG(LogTemp, Error, TEXT("[MapExport] -Map=<MapName> 인자가 필요합니다."));
		return 1;
	}

	FString MapPath;
	if (!FParse::Value(*Params, TEXT("MapPath="), MapPath))
	{
		MapPath = TEXT("/Game/Maps/") + MapName;
	}

	if (!FEditorFileUtils::LoadMap(MapPath, /*bLoadAsTemplate*/ false, /*bShowProgress*/ true))
	{
		UE_LOG(LogTemp, Error, TEXT("[MapExport] 맵 로드 실패: %s"), *MapPath);
		return 1;
	}

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World)
	{
		UE_LOG(LogTemp, Error, TEXT("[MapExport] 에디터 월드를 가져오지 못했습니다."));
		return 1;
	}

	// Server/MapResource/Export/<MapName>/... 로 고정 - Server/MapResource/MapExport.bat, GameMap::LoadMapData와 경로 계약을 맞춘다.
	const FString ExportRoot = FPaths::ProjectDir() / TEXT("../../Server/MapResource/Export") / MapName;
	const FString GeoBinPath = ExportRoot / TEXT("Geometry") / (MapName + TEXT("_Geo.bin"));
	const FString LogicJsonPath = ExportRoot / TEXT("Logic") / (MapName + TEXT("_Logic.json"));

	const bool bGeoOk = ExportGeometry(World, GeoBinPath);
	const bool bLogicOk = ExportLogic(World, MapName, LogicJsonPath);

	if (bGeoOk && bLogicOk)
	{
		UE_LOG(LogTemp, Display, TEXT("[MapExport] %s 익스포트 완료"), *MapName);
		return 0;
	}

	UE_LOG(LogTemp, Error, TEXT("[MapExport] %s 익스포트 실패 (Geometry:%d Logic:%d)"), *MapName, bGeoOk, bLogicOk);
	return 1;
}

bool UMapExportCommandlet::ExportGeometry(UWorld* World, const FString& OutBinPath) const
{
	TArray<FVector> Vertices;
	TArray<int32> Indices;
	int32 ExportedComps = 0, SkippedComps = 0;

	// 메시 하나를 변환해 누적한다.
	auto AppendMesh = [&Vertices, &Indices](const FTriMeshCollisionData& TriData, const FTransform& TM)
	{
		const int32 BaseIndex = Vertices.Num();

		Vertices.Reserve(Vertices.Num() + TriData.Vertices.Num());
		for (const FVector3f& LocalVertex : TriData.Vertices)
		{
			Vertices.Add(TM.TransformPosition(FVector(LocalVertex)));
		}

		Indices.Reserve(Indices.Num() + TriData.Indices.Num() * 3);
		for (const FTriIndices& Tri : TriData.Indices)
		{
			// UE(왼손) -> Detour(오른손)로 축을 바꾸면 앞뒤가 뒤집히므로 정점 순서를 바꿔 위를 향하는 면이 걷기 가능하게 한다.
			Indices.Add(BaseIndex + Tri.v0);
			Indices.Add(BaseIndex + Tri.v2);
			Indices.Add(BaseIndex + Tri.v1);
		}
	};

	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;

		TInlineComponentArray<UStaticMeshComponent*> Comps;
		Actor->GetComponents<UStaticMeshComponent>(Comps);

		for (UStaticMeshComponent* Comp : Comps)
		{
			if (!Comp)
			{
				continue;
			}

			// 서버 내비메시 = UE에서 캐릭터가 실제로 부딪히는 것. 충돌이 꺼졌거나(풀 등 NoCollision) Pawn을 막지 않는 메시는 내보내지 않는다.
			// (2026-10-30 S1: 예전 내보내기는 풀까지 장애물로 넣어 서버가 지나갈 수 있는 곳의 이동을 거부했다)
			if (!Comp->IsCollisionEnabled() || Comp->GetCollisionResponseToChannel(ECC_Pawn) != ECR_Block)
			{
				++SkippedComps;
				continue;
			}

			UStaticMesh* Mesh = Comp->GetStaticMesh();
			if (!Mesh)
			{
				continue;
			}

			FTriMeshCollisionData TriData;
			if (!Mesh->GetPhysicsTriMeshData(&TriData, true))
			{
				continue;
			}

			// 인스턴스 메시(폴리지 등)는 인스턴스마다 변환을 적용한다.
			if (const UInstancedStaticMeshComponent* ISM = Cast<UInstancedStaticMeshComponent>(Comp))
			{
				for (int32 i = 0; i < ISM->GetInstanceCount(); ++i)
				{
					FTransform InstanceTM;
					if (ISM->GetInstanceTransform(i, InstanceTM, /*bWorldSpace*/ true))
					{
						AppendMesh(TriData, InstanceTM);
					}
				}
			}
			else
			{
				AppendMesh(TriData, Comp->GetComponentTransform());
			}
			++ExportedComps;
		}
	}

	UE_LOG(LogTemp, Display, TEXT("[MapExport] 지오메트리: 내보낸 컴포넌트 %d개, 제외(충돌 없음·Pawn 비차단) %d개, 정점 %d, 삼각형 %d"),
		ExportedComps, SkippedComps, Vertices.Num(), Indices.Num() / 3);

	if (Vertices.Num() == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("[MapExport] Pawn을 막는 충돌을 가진 스태틱메시를 찾지 못했습니다."));
	}

	IFileManager::Get().MakeDirectory(*FPaths::GetPath(OutBinPath), true);
	TUniquePtr<FArchive> Writer(IFileManager::Get().CreateFileWriter(*OutBinPath));
	if (!Writer)
	{
		UE_LOG(LogTemp, Error, TEXT("[MapExport] Geometry 파일을 열 수 없습니다: %s"), *OutBinPath);
		return false;
	}

	// Server/NavGenerator/RecastNavMesh.cpp의 LoadBinFile이 기대하는 포맷과 정확히 일치해야 한다:
	// int32 vCount + vCount*(float x,y,z) + int32 iCount + iCount*int32
	// NavGenerator는 좌표를 변환하지 않고 Detour 좌표(Y-up)로 그대로 쓰므로 여기서 바꿔 저장한다.
	// UE(x, y, z) -> Detour(y, z, -x) = 서버 NavmeshManager::UeToDetour와 같은 변환. (2026-10-30 S1: 예전 .nav와 같은 형식)
	int32 VCount = Vertices.Num();
	*Writer << VCount;
	for (const FVector& V : Vertices)
	{
		float X = static_cast<float>(V.Y);
		float Y = static_cast<float>(V.Z);
		float Z = static_cast<float>(-V.X);
		*Writer << X << Y << Z;
	}

	int32 ICount = Indices.Num();
	*Writer << ICount;
	for (int32 Idx : Indices)
	{
		*Writer << Idx;
	}

	Writer->Close();
	return true;
}

bool UMapExportCommandlet::ExportLogic(UWorld* World, const FString& MapName, const FString& OutJsonPath) const
{
	TArray<TSharedPtr<FJsonValue>> MarkerArray;

	for (TActorIterator<ALevelMarker> It(World); It; ++It)
	{
		ALevelMarker* Marker = *It;

		TSharedRef<FJsonObject> MarkerJson = MakeShared<FJsonObject>();
		MarkerJson->SetNumberField(TEXT("pointId"), Marker->PointId);
		MarkerJson->SetStringField(TEXT("type"), MarkerTypeToString(Marker->MarkerType));

		const FVector Loc = Marker->GetActorLocation();
		const float Yaw = Marker->GetActorRotation().Yaw;

		TSharedRef<FJsonObject> PosJson = MakeShared<FJsonObject>();
		PosJson->SetNumberField(TEXT("x"), Loc.X);
		PosJson->SetNumberField(TEXT("y"), Loc.Y);
		PosJson->SetNumberField(TEXT("z"), Loc.Z);
		PosJson->SetNumberField(TEXT("yaw"), Yaw);
		MarkerJson->SetObjectField(TEXT("position"), PosJson);

		// 타입별 파라미터(FMonsterSpawnParams 등)는 리플렉션으로 뽑아서 공통 필드 옆에 그대로 합친다.
		if (Marker->MarkerParams.IsValid())
		{
			TSharedRef<FJsonObject> ParamsJson = MakeShared<FJsonObject>();
			FJsonObjectConverter::UStructToJsonObject(
				Marker->MarkerParams.GetScriptStruct(),
				Marker->MarkerParams.GetMemory(),
				ParamsJson);

			for (const auto& Pair : ParamsJson->Values)
			{
				MarkerJson->SetField(ToCamelCase(Pair.Key), Pair.Value);
			}
		}

		MarkerArray.Add(MakeShared<FJsonValueObject>(MarkerJson));
	}

	// 맵에 마커 액터가 없으면 손으로 관리하는 기존 Logic JSON을 빈 내용으로 덮어쓰지 않는다. (2026-10-30: ParagonSample에는 ALevelMarker가 없음)
	if (MarkerArray.Num() == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("[MapExport] 맵에 ALevelMarker가 없어 Logic JSON을 그대로 둡니다: %s"), *OutJsonPath);
		return true;
	}

	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("mapName"), MapName);
	Root->SetNumberField(TEXT("version"), 1);
	Root->SetArrayField(TEXT("markers"), MarkerArray);

	FString OutputString;
	TSharedRef<TJsonWriter<>> JsonWriter = TJsonWriterFactory<>::Create(&OutputString);
	if (!FJsonSerializer::Serialize(Root, JsonWriter))
	{
		UE_LOG(LogTemp, Error, TEXT("[MapExport] Logic JSON 직렬화 실패"));
		return false;
	}

	IFileManager::Get().MakeDirectory(*FPaths::GetPath(OutJsonPath), true);
	if (!FFileHelper::SaveStringToFile(OutputString, *OutJsonPath))
	{
		UE_LOG(LogTemp, Error, TEXT("[MapExport] Logic 파일 저장 실패: %s"), *OutJsonPath);
		return false;
	}

	return true;
}
