// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/GameInstance.h"
#include "Engine/StreamableManager.h"
#include "JMGameInstance.generated.h"


UCLASS()
class JM_API UJMGameInstance : public UGameInstance
{
	GENERATED_BODY()
	
public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GameData|Tables")
	UDataTable* AppearanceAssetTable;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GameData|Tables")
	UDataTable* EquipAssetTable;

	FStreamableManager AssetLoader;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GameData|Tables")
	UDataTable* CharacterAssetTable;

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GameData|Classes")
	TSubclassOf<class AJMMyPlayer> MyPlayerClass;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GameData|Classes")
	TSubclassOf<class AJMPlayer> PlayerClass;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GameData|Classes")
	TSubclassOf<class ANpcCharaceter> NpcCharacterClass;
};
