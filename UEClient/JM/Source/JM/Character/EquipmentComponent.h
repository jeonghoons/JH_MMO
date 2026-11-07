#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "EquipmentComponent.generated.h"

class UEquipmentAppearance;
struct FStreamableHandle;

// 플레이어 외형 = 체형 + 장착 목록(서버 값 ObjectInfo.player_type·equip_items, SC_CHANGE_EQUIP).
// BodyTable·ItemTable로 필요한 에셋을 풀고 비동기 로드한 뒤 표시는 UEquipmentAppearance에 맡긴다(표시 방식 교체 가능 — DECISIONS 2026-11-05).
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

	// 늦게 끝난 옛 로드 결과를 버리기 위한 요청 번호
	int32 RequestSerial = 0;
	// 마지막 로드 요청
	TSharedPtr<FStreamableHandle> PendingHandle;
};
