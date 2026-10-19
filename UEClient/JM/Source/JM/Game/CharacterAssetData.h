#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"

#include "CharacterAssetData.generated.h"

// 영웅(Paragon) 한 명의 메시/AnimBP/몽타주 참조. 이후 HeroTable로 대체 예정 (Docs/DESIGN_CHARACTER_EQUIPMENT.md)
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
