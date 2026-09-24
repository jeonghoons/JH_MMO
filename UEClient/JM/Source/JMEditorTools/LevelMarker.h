#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "StructUtils/InstancedStruct.h"
#include "LevelMarker.generated.h"

// 서버 Common/Protocol/Enum.proto의 PlayerType, Server/Server/GameMap.h의 마커 구조체와 대응된다.
// 새 마커 타입을 추가할 때는 이 enum에 값을 추가하고, 아래에 대응하는 FXxxParams 구조체를 하나 추가하면 된다.
// (ALevelMarker, MapExportCommandlet은 그대로 둬도 됨)
UENUM(BlueprintType)
enum class EMarkerType : uint8
{
	PlayerStart,
	MonsterSpawn,
	Npc,
	PortalTrigger
};

USTRUCT(BlueprintType)
struct FMonsterSpawnParams
{
	GENERATED_BODY()

	// Server/Server/ServerData.h DataManager::_characterTable의 키(Protocol::PlayerType)와 매칭된다.
	UPROPERTY(EditAnywhere, Category = "MonsterSpawn")
	int32 MonsterTemplateId = 0;

	UPROPERTY(EditAnywhere, Category = "MonsterSpawn")
	float SpawnRadius = 0.f;

	UPROPERTY(EditAnywhere, Category = "MonsterSpawn")
	int32 MaxCount = 1;
};

USTRUCT(BlueprintType)
struct FNpcSpawnParams
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Npc")
	int32 NpcTemplateId = 0;
};

USTRUCT(BlueprintType)
struct FPortalTriggerParams
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "PortalTrigger")
	float TriggerRadius = 0.f;

	UPROPERTY(EditAnywhere, Category = "PortalTrigger")
	FString TargetMap;

	UPROPERTY(EditAnywhere, Category = "PortalTrigger")
	int32 TargetPointId = 0;
};

// 레벨에 배치하는 공용 서버 데이터 마커.
// MapExportCommandlet이 레벨의 ALevelMarker를 전부 순회해서 <MapName>_Logic.json으로 직렬화한다.
// 마커 타입이 늘어나도 이 액터 클래스와 커맨드릿 코드는 바뀌지 않는다 - 타입별 파라미터 구조체만 추가하면 됨.
UCLASS(BlueprintType)
class JMEDITORTOOLS_API ALevelMarker : public AActor
{
	GENERATED_BODY()

public:
	ALevelMarker();

	// 서버 로직에서 참조하는 고유 ID (예: PortalTrigger의 TargetPointId가 다른 마커의 PointId를 가리킴)
	UPROPERTY(EditAnywhere, Category = "LevelMarker")
	int32 PointId = 0;

	UPROPERTY(EditAnywhere, Category = "LevelMarker")
	EMarkerType MarkerType = EMarkerType::MonsterSpawn;

	// MarkerType에 맞는 FMonsterSpawnParams / FNpcSpawnParams / FPortalTriggerParams 등을 선택해서 채운다.
	// PlayerStart 타입은 부가 파라미터가 없으므로 비워둔다.
	UPROPERTY(EditAnywhere, Category = "LevelMarker")
	FInstancedStruct MarkerParams;
};
