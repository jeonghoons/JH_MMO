#include "Player/JMPlayer.h"
#include "Game/JMGameInstance.h"
#include "Character/EquipmentComponent.h"
#include "Game/DataManager.h"
#include "GameFramework/CharacterMovementComponent.h"

AJMPlayer::AJMPlayer()
{
	Equipment = CreateDefaultSubobject<UEquipmentComponent>(TEXT("Equipment"));
}

EWeaponType AJMPlayer::GetWeaponType() const
{
	const int32 WeaponIndex = static_cast<int32>(Protocol::EQUIP_PART_WEAPON) - 1;   // equip_items는 슬롯 순서(EquipPart - 1)
	const int32 WeaponId = WeaponIndex < ObjectInfo.equip_items_size() ? ObjectInfo.equip_items(WeaponIndex) : 0;
	if (WeaponId == 0) return EWeaponType::Unarmed;

	const UDataManager* DataManager = GetGameInstance() ? GetGameInstance()->GetSubsystem<UDataManager>() : nullptr;
	const FItemTableRow* Item = DataManager ? DataManager->FindItemById(WeaponId) : nullptr;
	return (Item && Item->WeaponType != EWeaponType::None) ? Item->WeaponType : EWeaponType::Unarmed;
}

void AJMPlayer::ApplyEquipChange(Protocol::EquipPart Part, int32 ItemId, const Protocol::StatInfo& Stat)
{
	const int32 Index = static_cast<int32>(Part) - 1;
	if (Index < 0 || Index >= 6) return;
	while (ObjectInfo.equip_items_size() < 6) ObjectInfo.add_equip_items(0);
	ObjectInfo.set_equip_items(Index, ItemId);

	ObjectInfo.mutable_stat()->CopyFrom(Stat);
	if (Stat.move_speed() > 0.f && GetCharacterMovement())
		GetCharacterMovement()->MaxWalkSpeed = Stat.move_speed();

	if (Equipment) Equipment->SetSlotItem(Index, ItemId);
}

void AJMPlayer::SetPlayerData(const Protocol::ObjectInfo& ObjInfo)
{
	Super::SetPlayerData(ObjInfo);
	RefreshEquipment();
}

void AJMPlayer::RefreshEquipment()
{
	if (Equipment == nullptr) return;
	// equip_items는 슬롯 순서(EquipPart - 1), 빈 슬롯 0. 체형은 가입 때 고정(1 MALE, 2 FEMALE)
	TArray<int32> Items;
	for (int32 i = 0; i < ObjectInfo.equip_items_size(); ++i) Items.Add(ObjectInfo.equip_items(i));
	Equipment->SetLoadout(static_cast<int32>(ObjectInfo.player_type()), Items);
}
