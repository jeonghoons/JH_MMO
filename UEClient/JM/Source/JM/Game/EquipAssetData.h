#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"

#include "EquipAssetData.generated.h"

UENUM(BlueprintType)
enum class EAppearancePart : uint8
{
	Face, Torso, Hands, Legs, Feet, Hair
};

USTRUCT(BlueprintType)
struct FAppearanceAssetData : public FTableRowBase
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FText DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	EAppearancePart AppearancePart;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TSoftObjectPtr<USkeletalMesh> AppearanceMesh;
};

UENUM(BlueprintType)
enum class EEquipPart : uint8
{
	OutfitUpper, OutfitLower, OutfitShoes, Weapon
};

USTRUCT(BlueprintType)
struct FEquipAssetData : public FTableRowBase
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FText DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	EEquipPart EquipPart;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TSoftObjectPtr<USkeletalMesh> EquipMesh;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon")
	FName AttachSocketName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon Override")
	TSoftClassPtr<UAnimInstance> WeaponAnimClass;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon Override")
	TSoftObjectPtr<UAnimMontage> AttackMontage;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon Override")
	TSoftObjectPtr<UAnimMontage> HitMontage;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon Override")
	TSoftObjectPtr<UAnimMontage> DeadMontage;
};
