#include "Player/JMPlayer.h"
#include "Game/ModularAppearanceComponent.h"
#include "Game/ModularEquipmentComponent.h"

AJMPlayer::AJMPlayer()
{
	AppearanceComponent = CreateDefaultSubobject<UModularAppearanceComponent>(TEXT("AppearanceComponent"));
	EquipmentComponent = CreateDefaultSubobject<UModularEquipmentComponent>(TEXT("EquipmentComponent"));
}

void AJMPlayer::SetPlayerData(const Protocol::ObjectInfo& ObjInfo)
{
	Super::SetPlayerData(ObjInfo);

	TArray<int32> AppearanceIDs;
	AppearanceIDs.Reserve(ObjInfo.appearance_items_size());
	/*for (int i = 0; i < ObjInfo.appearance_items_size(); ++i)
	{
		AppearanceIDs.Add(ObjInfo.appearance_items(i));
	}*/
	for (int i = 0; i < 6; ++i) {
		AppearanceIDs.Add((i+1) * 1000 + 2); // 임시 로직 유지
	}
	
	if (AppearanceComponent && AppearanceIDs.Num() > 0)
	{
		AppearanceComponent->ApplyAppearance(AppearanceIDs);
	}

	TArray<int32> EquipIDs;
	EquipIDs.Reserve(ObjInfo.equip_items_size());
	for (int i = 0; i < ObjInfo.equip_items_size(); ++i)
	{
		EquipIDs.Add(ObjInfo.equip_items(i));
	}

	if (EquipmentComponent && EquipIDs.Num() > 0)
	{
		EquipmentComponent->ApplyEquipment(EquipIDs);
	}
}
