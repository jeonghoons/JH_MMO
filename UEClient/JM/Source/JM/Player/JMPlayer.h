#pragma once

#include "CoreMinimal.h"
#include "Character/JMCharacterBase.h"
#include "JMPlayer.generated.h"
UCLASS()
class JM_API AJMPlayer : public AJMCharacterBase
{
	GENERATED_BODY()
public:
	AJMPlayer();
	virtual void SetPlayerData(const Protocol::ObjectInfo& ObjInfo) override;

protected:

protected:
	TSharedPtr<struct FStreamableHandle> AssetLoadHandle;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Animation")
	class UAnimMontage* AttackMontage;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Animation")
	class UAnimMontage* DeadMontage;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Animation")
	class UAnimMontage* HitMontage;

protected:
	void OnAssetLoadCompleted(TSoftObjectPtr<USkeletalMesh> MeshAsset, TSoftClassPtr<UAnimInstance> AnimAsset, TSoftObjectPtr<class UAnimMontage> AttackMontageAsset, TSoftObjectPtr<class UAnimMontage> HitMontageAsset, TSoftObjectPtr<class UAnimMontage> DeadMontageAsset);

public:

	static FName GetCharacterRowName(Protocol::PlayerType Type);

};
