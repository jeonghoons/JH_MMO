#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "JMDataSettings.generated.h"

class UDataTable;

// 프로젝트 설정(Project Settings > Game > JM Data)에서 DataTable 애셋을 지정한다. 코드에 애셋 경로를 쓰지 않기 위함이다.
// 값은 Config/DefaultGame.ini 의 [/Script/JM.JMDataSettings] 에 저장된다.
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "JM Data"))
class JM_API UJMDataSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UPROPERTY(Config, EditAnywhere, Category = "DataTable")
	TSoftObjectPtr<UDataTable> MovementTable;

	UPROPERTY(Config, EditAnywhere, Category = "DataTable")
	TSoftObjectPtr<UDataTable> SkillTable;
};