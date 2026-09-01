#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "Protocol/Protocol.pb.h"
#include "Game/EquipAssetData.h"
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

	void ApplyNetworkMovement(float DeltaTime);

	void SyncTransformToInfo();

public:	
	const Protocol::ObjectInfo& GetObjectInfo() const { return ObjectInfo; }
	const Protocol::PositionInfo& GetDestInfo() const { return DestPosition; }
	Protocol::MoveState GetMoveState() const { return ObjectInfo.position().state(); }
	FVector GetDestVelocity() const { return FVector(DestPosition.v_x(), DestPosition.v_y(), DestPosition.v_z()); }

	void SetDestInfo(const Protocol::PositionInfo& PosInfo);

public:
	virtual void SetPlayerData(const Protocol::ObjectInfo& ObjInfo);
	virtual void OnDamaged(int32_t Damage, int32_t RemainHP);
	virtual void OnDead();
	virtual void OnAttack();

	void UpdateWeaponAnimation(TSubclassOf<UAnimInstance> AnimClass, UAnimMontage* Attack, UAnimMontage* Hit, UAnimMontage* Dead);

	
protected:

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	class UModularAppearanceComponent* AppearanceComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	class UModularEquipmentComponent* EquipmentComponent;

	UPROPERTY() 
	class UAnimMontage* CurrentAttackMontage;
	UPROPERTY() 
	class UAnimMontage* CurrentHitMontage;
	UPROPERTY() 
	class UAnimMontage* CurrentDeadMontage;

protected:
	Protocol::ObjectInfo				ObjectInfo;
	Protocol::PositionInfo				DestPosition;

public:
	bool IsMyPlayer = false;
};
