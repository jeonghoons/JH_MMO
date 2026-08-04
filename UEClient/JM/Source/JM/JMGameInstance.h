// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/GameInstance.h"
#include "Engine/StreamableManager.h"
#include "JMGameInstance.generated.h"

USTRUCT(BlueprintType)
struct FCharacterAssetData : public FTableRowBase
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Asset")
	TSoftObjectPtr<USkeletalMesh> CharacterMesh;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Asset")
	TSoftClassPtr<UAnimInstance> AnimClass;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TSoftObjectPtr<class UAnimMontage> AttackMontage;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Asset")
	TSoftObjectPtr<class UAnimMontage> HitMontage;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Asset")
	TSoftObjectPtr<class UAnimMontage> DeadMontage;
};

UCLASS()
class JM_API UJMGameInstance : public UGameInstance
{
	GENERATED_BODY()
	
public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Data")
	TObjectPtr<UDataTable> CharacterAssetTable;

	FStreamableManager AssetLoader;

public:
	UPROPERTY(EditAnywhere)
	TSubclassOf<class AJMPlayer> PlayerClass;

	UPROPERTY(EditAnywhere)
	TSubclassOf<class AJMMyPlayer> MyPlayerClass;

	UPROPERTY(EditAnywhere)
	TSubclassOf<class ANpcCharaceter> NpcCharacterClass;
};
