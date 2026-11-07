#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Data/ItemTableRow.h"
#include "EquipmentAppearance.generated.h"

class ACharacter;
class USkeletalMesh;
class UStaticMesh;
class UMaterialInterface;
class UAnimInstance;
class USkeletalMeshComponent;
class UStaticMeshComponent;
class UMaterialInstanceDynamic;

// 장착 슬롯 수 = Protocol::EquipPart 1~6 (인덱스 = EquipPart - 1)
static constexpr int32 EQUIP_SLOT_COUNT = 6;

// 한 슬롯의 외형(로드된 에셋). 스켈레탈 슬롯(상의·하의·장갑·장화)은 Skeletal, 정적 슬롯(머리·무기)은 Static + 부착값.
struct FEquipSlotLook
{
	USkeletalMesh* Skeletal = nullptr;
	UStaticMesh* Static = nullptr;
	FName Socket;
	FTransform Attach = FTransform::Identity;
};

// 장착 목록을 표시용으로 풀어 둔 결과. UEquipmentComponent가 BodyTable·ItemTable로 만들고 에셋 로드를 끝낸 뒤 넘긴다.
struct FEquipmentLook
{
	USkeletalMesh* Leader = nullptr;
	USkeletalMesh* Eyes = nullptr;
	USkeletalMesh* Teeth = nullptr;
	USkeletalMesh* Hair = nullptr;
	UMaterialInterface* BodyMaterial = nullptr;
	TSubclassOf<UAnimInstance> AnimClass;

	FEquipSlotLook Slots[EQUIP_SLOT_COUNT];
	FBodyMaskParams Mask;      // 입은 아이템 마스크를 합친 값
	bool bHideHair = false;
};

// "장착 목록 → 표시"를 맡는 교체 가능한 구현(DECISIONS 2026-11-05).
// 지금은 Leader Pose(ULeaderPoseAppearance). 메시 병합·Mutable로 바꿔도 서버·패킷·ItemTable·UEquipmentComponent는 그대로 둔다.
UCLASS(Abstract)
class JM_API UEquipmentAppearance : public UObject
{
	GENERATED_BODY()

public:
	// Owner의 외형을 Look 그대로 만든다. 같은 Look으로 여러 번 불러도 결과가 같아야 한다.
	virtual void Apply(ACharacter* Owner, const FEquipmentLook& Look) PURE_VIRTUAL(UEquipmentAppearance::Apply, );
};

// Leader = 캐릭터 기본 Mesh(보이는 몸, 애니메이션 재생). 눈·이·헤어·스켈레탈 슬롯은 Leader Pose로 따라가고,
// 정적 슬롯은 본/소켓에 부착, 몸이 장비에 덮이는 영역은 몸 섹션 MID의 마스크 파라미터로 지운다(Docs/DESIGN_CHARACTER_EQUIPMENT.md).
UCLASS()
class JM_API ULeaderPoseAppearance : public UEquipmentAppearance
{
	GENERATED_BODY()

public:
	virtual void Apply(ACharacter* Owner, const FEquipmentLook& Look) override;

private:
	USkeletalMeshComponent* EnsureFollower(ACharacter* Owner, TObjectPtr<USkeletalMeshComponent>& Comp, const TCHAR* Name);
	UStaticMeshComponent* EnsureStatic(ACharacter* Owner, TObjectPtr<UStaticMeshComponent>& Comp, const TCHAR* Name);
	void ApplyBodyMask(USkeletalMeshComponent* Leader, const FEquipmentLook& Look);

	UPROPERTY() TObjectPtr<USkeletalMeshComponent> EyesComp;
	UPROPERTY() TObjectPtr<USkeletalMeshComponent> TeethComp;
	UPROPERTY() TObjectPtr<USkeletalMeshComponent> HairComp;
	UPROPERTY() TObjectPtr<USkeletalMeshComponent> SkeletalSlots[EQUIP_SLOT_COUNT];
	UPROPERTY() TObjectPtr<UStaticMeshComponent> StaticSlots[EQUIP_SLOT_COUNT];

	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> BodyMID;
	UPROPERTY() TObjectPtr<UMaterialInterface> BodyMIDParent;
	int32 BodyMaterialIndex = INDEX_NONE;
};
