#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "EquipmentComponent.generated.h"

class UEquipmentAppearance;
struct FStreamableHandle;

// 플레이어 외형 = 체형 + 장착 목록(서버 값 ObjectInfo.player_type·equip_items, SC_CHANGE_EQUIP).
// BodyTable·ItemTable로 필요한 에셋을 풀고 비동기 로드한 뒤 표시는 UEquipmentAppearance에 맡긴다(표시 방식 교체 가능 — DECISIONS 2026-11-05).
// MC4.2: 무기 타입 레이어(WeaponTypeTable.AnimLayer)와 피격·사망 몽타주(BodyTable)도 여기서 로드해 넘긴다.
// 외형만 다루며 스탯·무기 타입은 서버 값을 그대로 쓴다.
UCLASS(ClassGroup = (JM), meta = (BlueprintSpawnableComponent))
class JM_API UEquipmentComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UEquipmentComponent();

	// 체형(PlayerType 1 MALE, 2 FEMALE)과 장착 목록 전체(인덱스 = EquipPart - 1, 빈 슬롯 0)
	void SetLoadout(int32 InPlayerType, const TArray<int32>& InItems);
	// 한 슬롯만 바뀜(SC_CHANGE_EQUIP). Index = EquipPart - 1
	void SetSlotItem(int32 Index, int32 ItemId);

	int32 GetPlayerType() const { return PlayerType; }

	// 무기를 등(true)/손(false)에 둔다(MC4.2d-2, 캐릭터의 무기 자세가 부른다). 다음 외형 갱신에도 유지된다.
	void SetWeaponOnBack(bool bOnBack);
	const TArray<int32>& GetItems() const { return Items; }

protected:
	// 표시 구현. 기본 ULeaderPoseAppearance
	UPROPERTY(EditDefaultsOnly, Category = "Equipment")
	TSubclassOf<UEquipmentAppearance> AppearanceClass;

private:
	void Refresh();
	void OnAssetsLoaded(int32 Serial);

	UPROPERTY(Transient)
	TObjectPtr<UEquipmentAppearance> Appearance;

	int32 PlayerType = 0;
	TArray<int32> Items;
	bool bWeaponOnBack = true;   // 처음 보이는 캐릭터는 등에 멘 상태(DESIGN_ANIMATION)

	// 늦게 끝난 옛 로드 결과를 버리기 위한 요청 번호
	int32 RequestSerial = 0;
	// 마지막 로드 요청
	TSharedPtr<FStreamableHandle> PendingHandle;
};
