#pragma once

#include "CoreMinimal.h"
#include "Character/JMCharacterBase.h"
#include "Data/ItemTableRow.h"
#include "JMPlayer.generated.h"
UCLASS()
class JM_API AJMPlayer : public AJMCharacterBase
{
	GENERATED_BODY()
public:
	AJMPlayer();
	virtual void SetPlayerData(const Protocol::ObjectInfo& ObjInfo) override;

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
	// 장착 무기의 타입(스킬 조회 키). 무기 슬롯이 비면 Unarmed.
	// MC3.1: 장착 시스템 전이라 항상 Unarmed — MC3.2에서 ObjectInfo.equip_items의 무기로 ItemTable을 조회해 정한다.
	EWeaponType GetWeaponType() const { return EWeaponType::Unarmed; }

	static FName GetCharacterRowName(Protocol::PlayerType Type);

};
