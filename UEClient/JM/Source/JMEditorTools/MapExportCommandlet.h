#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "MapExportCommandlet.generated.h"

class UWorld;

// 사용법: UnrealEditor-Cmd.exe <uproject경로> -run=MapExport -Map=<맵이름> [-MapPath=/Game/Maps/<맵이름>] -unattended -nopause
// -MapPath를 생략하면 "/Game/Maps/<맵이름>"을 기본 패키지 경로로 사용한다.
//
// 출력 (Server/MapResource/MapExport.bat이 기대하는 경로와 일치해야 함):
//   Server/MapResource/Export/<맵이름>/Geometry/<맵이름>_Geo.bin  - 콜리전 정점/인덱스 (raw binary, NavGenerator 입력)
//   Server/MapResource/Export/<맵이름>/Logic/<맵이름>_Logic.json  - ALevelMarker 배열 (JSON, GameMap::LoadMapData 입력)
UCLASS()
class JMEDITORTOOLS_API UMapExportCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	UMapExportCommandlet();

	virtual int32 Main(const FString& Params) override;

private:
	// "NavExport" 콜리전 프로파일이 지정된 스태틱메시 컴포넌트만 대상으로 콜리전 지오메트리를 뽑는다.
	bool ExportGeometry(UWorld* World, const FString& OutBinPath) const;

	// 레벨의 ALevelMarker를 전부 순회해 Logic JSON으로 직렬화한다.
	bool ExportLogic(UWorld* World, const FString& MapName, const FString& OutJsonPath) const;
};
