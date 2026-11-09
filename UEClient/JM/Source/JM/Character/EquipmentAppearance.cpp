#include "Character/EquipmentAppearance.h"
#include "GameFramework/Character.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"

namespace
{
	constexpr int32 WEAPON_SLOT = 5;   // = EquipPart WEAPON(6) - 1
	const TCHAR* const SlotNames[EQUIP_SLOT_COUNT] = { TEXT("Head"), TEXT("Top"), TEXT("Bottom"), TEXT("Gloves"), TEXT("Boots"), TEXT("Weapon"), TEXT("Offhand") };

	// 몸 섹션 머티리얼 슬롯: 이름에 body가 들어간 슬롯(jm_kit.body_mask._body_slot_index와 같은 규칙). 얼굴 섹션은 키트 원본을 둔다.
	int32 FindBodyMaterialIndex(const USkeletalMeshComponent* Comp)
	{
		const TArray<FName> Names = Comp->GetMaterialSlotNames();
		for (int32 i = 0; i < Names.Num(); ++i)
		{
			if (Names[i].ToString().Contains(TEXT("body"))) return i;
		}
		return 0;
	}
}

USkeletalMeshComponent* ULeaderPoseAppearance::EnsureFollower(ACharacter* Owner, TObjectPtr<USkeletalMeshComponent>& Comp, const TCHAR* Name)
{
	if (Comp == nullptr)
	{
		Comp = NewObject<USkeletalMeshComponent>(Owner, Name);
		Comp->SetupAttachment(Owner->GetMesh());
		Comp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Comp->SetCanEverAffectNavigation(false);
		// 부위 메시 자체 bounds는 기준 자세라 팔을 뻗는 모션에서 잘못 컬링된다 — 리더 bounds 사용(MC1.3)
		Comp->bUseBoundsFromLeaderPoseComponent = true;
		Comp->RegisterComponent();
		Comp->SetLeaderPoseComponent(Owner->GetMesh());
	}
	return Comp;
}

UStaticMeshComponent* ULeaderPoseAppearance::EnsureStatic(ACharacter* Owner, TObjectPtr<UStaticMeshComponent>& Comp, const TCHAR* Name)
{
	if (Comp == nullptr)
	{
		Comp = NewObject<UStaticMeshComponent>(Owner, Name);
		Comp->SetupAttachment(Owner->GetMesh());
		Comp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Comp->SetCanEverAffectNavigation(false);
		Comp->SetMobility(EComponentMobility::Movable);
		Comp->RegisterComponent();
	}
	return Comp;
}

void ULeaderPoseAppearance::ApplyBodyMask(USkeletalMeshComponent* Leader, const FEquipmentLook& Look)
{
	if (Look.BodyMaterial == nullptr) return;

	// 몸 메시나 마스크 머티리얼이 바뀌면 MID를 다시 만든다(체형은 가입 때 고정이라 보통 한 번)
	if (BodyMID == nullptr || BodyMIDParent != Look.BodyMaterial || Leader->GetMaterial(BodyMaterialIndex) != BodyMID)
	{
		BodyMaterialIndex = FindBodyMaterialIndex(Leader);
		BodyMID = Leader->CreateDynamicMaterialInstance(BodyMaterialIndex, Look.BodyMaterial);
		BodyMIDParent = Look.BodyMaterial;
	}
	if (BodyMID == nullptr) return;

	// 파라미터 이름 = jm_kit.body_mask.PARAMS = FBodyMaskParams 필드 이름
	const FBodyMaskParams& M = Look.Mask;
	BodyMID->SetScalarParameterValue(TEXT("HideTorso"), M.HideTorso);
	BodyMID->SetScalarParameterValue(TEXT("HideArms"), M.HideArms);
	BodyMID->SetScalarParameterValue(TEXT("ArmKeepCm"), M.ArmKeepCm);
	BodyMID->SetScalarParameterValue(TEXT("HideLegs"), M.HideLegs);
	BodyMID->SetScalarParameterValue(TEXT("LegKeepCm"), M.LegKeepCm);
	BodyMID->SetScalarParameterValue(TEXT("HideFeet"), M.HideFeet);
	BodyMID->SetScalarParameterValue(TEXT("HideHands"), M.HideHands);
}

void ULeaderPoseAppearance::Apply(ACharacter* Owner, const FEquipmentLook& Look)
{
	if (Owner == nullptr || Owner->GetMesh() == nullptr) return;
	USkeletalMeshComponent* Leader = Owner->GetMesh();

	// Leader = 보이는 몸. 메시를 바꾸면 머티리얼이 원본으로 돌아가므로 MID는 그 뒤에 만든다.
	if (Look.Leader && Leader->GetSkeletalMeshAsset() != Look.Leader)
	{
		Leader->SetSkeletalMesh(Look.Leader);
		BodyMID = nullptr;
	}
	if (Look.AnimClass && Leader->GetAnimClass() != Look.AnimClass)
	{
		Leader->SetAnimInstanceClass(Look.AnimClass);
	}
	// 무기 타입 레이어(MC4.2): AnimClass를 새로 만들면 연결이 풀리므로 그 뒤에 잇는다. 이미 같은 레이어가 연결돼 있으면 건드리지 않는다.
	if (Look.AnimLayer && Leader->GetAnimInstance() && Leader->GetLinkedAnimLayerInstanceByClass(Look.AnimLayer) == nullptr)
	{
		Leader->LinkAnimClassLayers(Look.AnimLayer);
	}
	ApplyBodyMask(Leader, Look);

	EnsureFollower(Owner, EyesComp, TEXT("Body_Eyes"))->SetSkeletalMesh(Look.Eyes);
	EnsureFollower(Owner, TeethComp, TEXT("Body_Teeth"))->SetSkeletalMesh(Look.Teeth);
	USkeletalMeshComponent* Hair = EnsureFollower(Owner, HairComp, TEXT("Body_Hair"));
	Hair->SetSkeletalMesh(Look.Hair);
	Hair->SetVisibility(!Look.bHideHair);

	for (int32 i = 0; i < EQUIP_SLOT_COUNT; ++i)
	{
		const FEquipSlotLook& S = Look.Slots[i];

		// 스켈레탈 슬롯: 비면 메시를 비운다(컴포넌트는 재사용)
		if (S.Skeletal || SkeletalSlots[i])
		{
			USkeletalMeshComponent* Comp = EnsureFollower(Owner, SkeletalSlots[i], *FString::Printf(TEXT("Equip_%s"), SlotNames[i]));
			if (Comp->GetSkeletalMeshAsset() != S.Skeletal) Comp->SetSkeletalMesh(S.Skeletal);
			Comp->SetVisibility(S.Skeletal != nullptr);
		}

		// 정적 슬롯: 본/소켓에 붙이고 상대 변환은 데이터(ItemTable.AttachM/F)
		if (S.Static || StaticSlots[i])
		{
			UStaticMeshComponent* Comp = EnsureStatic(Owner, StaticSlots[i], *FString::Printf(TEXT("Equip_%s_Static"), SlotNames[i]));
			Comp->SetStaticMesh(S.Static);
			Comp->SetVisibility(S.Static != nullptr);
			if (S.Static)
			{
				Comp->AttachToComponent(Leader, FAttachmentTransformRules::SnapToTargetNotIncludingScale, S.Socket);
				Comp->SetRelativeTransform(S.Attach);
			}
		}
	}

	// 무기는 등/손 상태를 따른다(MC4.2d-2)
	WeaponLook = Look.Slots[WEAPON_SLOT];
	AttachWeapon();
}

void ULeaderPoseAppearance::SetWeaponOnBack(bool bOnBack)
{
	bWeaponOnBack = bOnBack;
	AttachWeapon();
}

void ULeaderPoseAppearance::AttachWeapon()
{
	UStaticMeshComponent* Comp = StaticSlots[WEAPON_SLOT];
	USkeletalMeshComponent* Leader = Comp ? Cast<USkeletalMeshComponent>(Comp->GetAttachParent()) : nullptr;
	if (Comp == nullptr || Leader == nullptr || WeaponLook.Static == nullptr) return;

	const bool bBack = bWeaponOnBack && !WeaponLook.BackSocket.IsNone();
	Comp->AttachToComponent(Leader, FAttachmentTransformRules::SnapToTargetNotIncludingScale, bBack ? WeaponLook.BackSocket : WeaponLook.Socket);
	Comp->SetRelativeTransform(bBack ? WeaponLook.BackAttach : WeaponLook.Attach);
}
