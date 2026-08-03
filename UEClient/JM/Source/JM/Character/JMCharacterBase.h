#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "Protocol/Protocol.pb.h"
#include "JMCharacterBase.generated.h"

UCLASS()
class JM_API AJMCharacterBase : public ACharacter
{
	GENERATED_BODY()
public:
	AJMCharacterBase();

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;

public:	
	const Protocol::ObjectInfo& GetObjectInfo() const { return ObjectInfo; }
	const Protocol::PositionInfo& GetDestInfo() const { return DestPosition; }
	Protocol::MoveState GetMoveState() const { return ObjectInfo.position().state(); }
	
public:
	virtual void SetPlayerData(const Protocol::ObjectInfo& ObjInfo);
	static FName GetCharacterRowName(const Protocol::PlayerType Type);
	virtual void OnDamaged(int32_t Damage, int32_t RemainHP);
	virtual void OnDead();
	virtual void OnAttack();

protected:
	void OnAssetLoadCompleted(TSoftObjectPtr<USkeletalMesh> MeshAsset, TSoftClassPtr<UAnimInstance> AnimAsset, TSoftObjectPtr<class UAnimMontage> AttackMontageAsset, TSoftObjectPtr<class UAnimMontage> HitMontageAsset, TSoftObjectPtr<class UAnimMontage> DeadMontageAsset);

protected:
	TSharedPtr<struct FStreamableHandle> AssetLoadHandle;
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Animation")
	class UAnimMontage* AttackMontage;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Animation")
	class UAnimMontage* DeadMontage;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Animation")
	class UAnimMontage* HitMontage;

protected:
	bool IsMyPlayer = false;
	Protocol::ObjectInfo				ObjectInfo;
	Protocol::PositionInfo				DestPosition;
};
