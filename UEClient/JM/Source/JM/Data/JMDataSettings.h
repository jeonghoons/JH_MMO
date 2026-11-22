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

	UPROPERTY(Config, EditAnywhere, Category = "DataTable")
	TSoftObjectPtr<UDataTable> ItemTable;

	UPROPERTY(Config, EditAnywhere, Category = "DataTable")
	TSoftObjectPtr<UDataTable> BodyTable;

	UPROPERTY(Config, EditAnywhere, Category = "DataTable")
	TSoftObjectPtr<UDataTable> WeaponTypeTable;

	UPROPERTY(Config, EditAnywhere, Category = "DataTable")
	TSoftObjectPtr<UDataTable> MonsterTable;

	// 레벨별 스탯·다음 레벨 경험치. 클라이언트는 HUD 경험치 바의 분모만 읽는다 (A2.3b)
	UPROPERTY(Config, EditAnywhere, Category = "DataTable")
	TSoftObjectPtr<UDataTable> PlayerStatTable;

	// 퀘스트 글자·목표 수(Q1). 판정·보상은 서버가 같은 CSV로 한다
	UPROPERTY(Config, EditAnywhere, Category = "DataTable")
	TSoftObjectPtr<UDataTable> QuestTable;
};