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

USTRUCT(BlueprintType)
struct FCharacterBaseData : public FTableRowBase
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Appearance")
	TSoftObjectPtr<USkeletalMesh> BaseMesh;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation")
	TSoftObjectPtr<UAnimMontage> HitMontage;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation")
	TSoftObjectPtr<UAnimMontage> DeadMontage;
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
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Common")
	FText DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Common")
	EEquipPart EquipPart;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Common")
	TSoftObjectPtr<USkeletalMesh> EquipMesh;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Common")
	FName AttachSocketName;

	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WeaponOnly")
	TSoftClassPtr<UAnimInstance> WeaponAnimClass;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WeaponOnly")
	TSoftObjectPtr<UAnimMontage> AttackMontage;
};


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