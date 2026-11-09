#include "Character/EquipmentComponent.h"
#include "Character/EquipmentAppearance.h"
#include "Game/DataManager.h"
#include "Game/JMGameInstance.h"
#include "Character/JMCharacterBase.h"
#include "GameFramework/Character.h"
#include "Engine/StreamableManager.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"

namespace
{
	constexpr int32 PLAYER_TYPE_FEMALE_ID = 2;   // = Protocol::PLAYER_TYPE_FEMALE (BodyTable RowName)
	constexpr int32 WEAPON_SLOT_INDEX = static_cast<int32>(Protocol::EQUIP_PART_WEAPON) - 1;   // Items 인덱스 = EquipPart - 1

	// 장착 목록의 무기 타입(AJMPlayer::GetWeaponType과 같은 규칙: 무기 슬롯이 비면 Unarmed)
	EWeaponType WeaponTypeOf(const UDataManager& Data, const TArray<int32>& Items)
	{
		const int32 WeaponId = Items.IsValidIndex(WEAPON_SLOT_INDEX) ? Items[WEAPON_SLOT_INDEX] : 0;
		if (WeaponId == 0) return EWeaponType::Unarmed;
		const FItemTableRow* Item = Data.FindItemById(WeaponId);
		return (Item && Item->WeaponType != EWeaponType::None) ? Item->WeaponType : EWeaponType::Unarmed;
	}

	// 여러 아이템의 마스크를 합친다: Hide*는 큰 값, KeepCm은 그 영역을 지운 아이템의 값
	void MergeMask(FBodyMaskParams& Out, const FBodyMaskParams& In)
	{
		Out.HideTorso = FMath::Max(Out.HideTorso, In.HideTorso);
		if (In.HideArms > 0.f) Out.ArmKeepCm = In.ArmKeepCm;
		Out.HideArms = FMath::Max(Out.HideArms, In.HideArms);
		if (In.HideLegs > 0.f) Out.LegKeepCm = In.LegKeepCm;
		Out.HideLegs = FMath::Max(Out.HideLegs, In.HideLegs);
		Out.HideFeet = FMath::Max(Out.HideFeet, In.HideFeet);
		Out.HideHands = FMath::Max(Out.HideHands, In.HideHands);
	}
}

UEquipmentComponent::UEquipmentComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	AppearanceClass = ULeaderPoseAppearance::StaticClass();
	Items.Init(0, EQUIP_SLOT_COUNT);
}

void UEquipmentComponent::SetLoadout(int32 InPlayerType, const TArray<int32>& InItems)
{
	PlayerType = InPlayerType;
	Items.Init(0, EQUIP_SLOT_COUNT);
	for (int32 i = 0; i < EQUIP_SLOT_COUNT && i < InItems.Num(); ++i) Items[i] = InItems[i];
	Refresh();
}

void UEquipmentComponent::SetSlotItem(int32 Index, int32 ItemId)
{
	if (!Items.IsValidIndex(Index) || Items[Index] == ItemId) return;
	Items[Index] = ItemId;
	Refresh();
}

void UEquipmentComponent::Refresh()
{
	const UGameInstance* GI = GetWorld() ? GetWorld()->GetGameInstance() : nullptr;
	UJMGameInstance* GameInst = Cast<UJMGameInstance>(const_cast<UGameInstance*>(GI));
	const UDataManager* Data = GI ? GI->GetSubsystem<UDataManager>() : nullptr;
	if (GameInst == nullptr || Data == nullptr) return;

	const FBodyTableRow* Body = Data->FindBody(PlayerType);
	if (Body == nullptr)
	{
		UE_LOG(LogTemp, Warning, TEXT("Equipment: BodyTable has no row for PlayerType %d"), PlayerType);
		return;
	}
	const bool bFemale = PlayerType == PLAYER_TYPE_FEMALE_ID;

	TArray<FSoftObjectPath> Paths;
	auto Add = [&Paths](const FSoftObjectPath& P) { if (!P.IsNull()) Paths.AddUnique(P); };
	Add(Body->Leader.ToSoftObjectPath());
	Add(Body->Eyes.ToSoftObjectPath());
	Add(Body->Teeth.ToSoftObjectPath());
	Add(Body->Hair.ToSoftObjectPath());
	Add(Body->BodyMaterial.ToSoftObjectPath());
	Add(Body->AnimClass.ToSoftObjectPath());
	Add(Body->HitMontage.ToSoftObjectPath());
	Add(Body->DeathMontage.ToSoftObjectPath());
	if (const FWeaponTypeTableRow* Weapon = Data->FindWeaponType(WeaponTypeOf(*Data, Items)))
	{
		Add(Weapon->AnimLayer.ToSoftObjectPath());
		Add(Weapon->SheatheMontage.ToSoftObjectPath());
	}
	for (int32 ItemId : Items)
	{
		if (ItemId == 0) continue;
		const FItemTableRow* Item = Data->FindItemById(ItemId);
		if (Item == nullptr)
		{
			UE_LOG(LogTemp, Warning, TEXT("Equipment: ItemTable has no row %d"), ItemId);
			continue;
		}
		Add((bFemale ? Item->MeshF : Item->MeshM).ToSoftObjectPath());
		Add(Item->StaticMesh.ToSoftObjectPath());
	}

	const int32 Serial = ++RequestSerial;
	// 아직 로드 중인 옛 요청은 취소한다(끝난 요청의 콜백은 요청 번호로 걸러진다)
	if (PendingHandle.IsValid() && PendingHandle->IsLoadingInProgress()) PendingHandle->CancelHandle();
	PendingHandle = GameInst->AssetLoader.RequestAsyncLoad(Paths, FStreamableDelegate::CreateUObject(this, &UEquipmentComponent::OnAssetsLoaded, Serial));
	if (!PendingHandle.IsValid()) OnAssetsLoaded(Serial);   // 경로가 없거나 이미 끝난 경우
}

void UEquipmentComponent::OnAssetsLoaded(int32 Serial)
{
	if (Serial != RequestSerial) return;   // 더 새 요청이 있다

	ACharacter* Owner = Cast<ACharacter>(GetOwner());
	const UGameInstance* GI = GetWorld() ? GetWorld()->GetGameInstance() : nullptr;
	const UDataManager* Data = GI ? GI->GetSubsystem<UDataManager>() : nullptr;
	const FBodyTableRow* Body = Data ? Data->FindBody(PlayerType) : nullptr;
	if (Owner == nullptr || Body == nullptr) return;
	const bool bFemale = PlayerType == PLAYER_TYPE_FEMALE_ID;

	FEquipmentLook Look;
	Look.Leader = Body->Leader.Get();
	Look.Eyes = Body->Eyes.Get();
	Look.Teeth = Body->Teeth.Get();
	Look.Hair = Body->Hair.Get();
	Look.BodyMaterial = Body->BodyMaterial.Get();
	Look.AnimClass = Body->AnimClass.Get();
	const FWeaponTypeTableRow* Weapon = Data->FindWeaponType(WeaponTypeOf(*Data, Items));
	if (Weapon) Look.AnimLayer = Weapon->AnimLayer.Get();

	// 피격·사망 몽타주와 무기 넣기 값은 외형이 아니라 캐릭터 행동이라 Appearance가 아닌 캐릭터에 넣는다
	if (AJMCharacterBase* Character = Cast<AJMCharacterBase>(Owner))
	{
		Character->SetReactionMontages(Body->HitMontage.Get(), Body->DeathMontage.Get());
		const FItemTableRow* WeaponItem = Items.IsValidIndex(WEAPON_SLOT_INDEX) && Items[WEAPON_SLOT_INDEX] != 0 ? Data->FindItemById(Items[WEAPON_SLOT_INDEX]) : nullptr;
		const bool bCanSheathe = Weapon && Weapon->SheatheDelaySec > 0.f && WeaponItem && !WeaponItem->BackSocket.IsNone();
		Character->SetSheatheData(bCanSheathe, bCanSheathe ? Weapon->SheatheDelaySec : 0.f, bCanSheathe ? Weapon->SheatheMontage.Get() : nullptr,
			bCanSheathe ? Weapon->SheatheSwapSec : 0.f);
	}

	for (int32 i = 0; i < EQUIP_SLOT_COUNT; ++i)
	{
		const FItemTableRow* Item = Items[i] != 0 ? Data->FindItemById(Items[i]) : nullptr;
		if (Item == nullptr) continue;
		FEquipSlotLook& S = Look.Slots[i];
		S.Skeletal = (bFemale ? Item->MeshF : Item->MeshM).Get();
		S.Static = Item->StaticMesh.Get();
		S.Socket = Item->AttachSocket;
		S.Attach = bFemale ? Item->AttachF : Item->AttachM;
		S.BackSocket = Item->BackSocket;
		S.BackAttach = bFemale ? Item->BackF : Item->BackM;
		MergeMask(Look.Mask, Item->Mask);
		Look.bHideHair |= Item->HideHair != 0;
	}

	if (Appearance == nullptr)
	{
		UClass* Cls = AppearanceClass ? AppearanceClass.Get() : ULeaderPoseAppearance::StaticClass();
		Appearance = NewObject<UEquipmentAppearance>(this, Cls);
	}
	// 적용 뒤에는 컴포넌트·AnimInstance·MID가 에셋을 참조하므로 핸들을 따로 붙잡지 않아도 된다.
	// (이미 로드된 에셋이면 콜백이 RequestAsyncLoad 안에서 바로 불려 PendingHandle 대입 전일 수 있다 — 여기서 핸들을 건드리지 않는다)
	Appearance->SetWeaponOnBack(bWeaponOnBack);
	Appearance->Apply(Owner, Look);
}

void UEquipmentComponent::SetWeaponOnBack(bool bOnBack)
{
	bWeaponOnBack = bOnBack;
	if (Appearance) Appearance->SetWeaponOnBack(bOnBack);
}
