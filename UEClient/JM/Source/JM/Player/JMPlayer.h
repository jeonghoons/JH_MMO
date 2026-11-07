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
	// 외형 = 체형 + 장착 목록(BodyTable·ItemTable, MC4.1)
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Equipment")
	TObjectPtr<class UEquipmentComponent> Equipment;

	void RefreshEquipment();

public:
	// 장착 무기의 타입(스킬 조회 키). 무기 슬롯이 비면 Unarmed. ObjectInfo.equip_items(서버 값)의 무기로 ItemTable을 조회한다.
	EWeaponType GetWeaponType() const;

	// SC_CHANGE_EQUIP 반영: 장착 목록과 서버가 계산한 최종 스탯을 그대로 넣고 외형을 바꾼다.
	void ApplyEquipChange(Protocol::EquipPart Part, int32 ItemId, const Protocol::StatInfo& Stat);

};
