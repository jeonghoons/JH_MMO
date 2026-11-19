#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Data/ItemTableRow.h"
#include "Player/QuickSlot.h"
#include "CombatHudWidget.generated.h"

class USkillSlotWidget;

// 전투 HUD(D-038·D-148): 내 이름·레벨, HP 바·수치, EXP 바, 액티브 칸 5개. 값은 모두 서버 패킷에서 온다(UJMObjectManager가 갱신).
UCLASS()
class JM_API UCombatHudWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void SetHp(int32 Hp, int32 MaxHp);

	// 내 이름·레벨(ObjectInfo.name·level). 레벨은 경험치 보상(A2.3b) 전까지 시작 레벨 고정.
	void SetIdentity(const FString& Name, int32 Level);

	// EXP 바(0~1). 보상(A2.3b) 전까지 0으로 둔다.
	void SetExpRatio(float Ratio);

	// 액티브 칸 내용(AJMMyPlayer::GetQuickSlots)과 현재 무기로 칸 아이콘을 채운다. 무기를 바꾸면 다시 부른다.
	void SetQuickSlots(const TArray<FQuickSlot>& QuickSlots, EWeaponType WeaponType);

	// 서버가 확정한 시전(SC_ATTACK)의 쿨타임을 그 스킬이 든 칸에 표시한다. SkillSlot = SkillTable의 Slot.
	void StartCooldown(int32 SkillSlot, int32 CooldownMs);
	// 그 스킬 칸의 쿨타임 표시를 남은 시간으로 다시 맞춘다(0 이하면 지움). 서버가 시전을 거부했을 때 쓴다 (CP1)
	void SetCooldownRemain(int32 SkillSlot, int32 RemainMs, int32 TotalMs);
	// 서버가 확정한 아이템 사용(SC_USE_ITEM)의 재사용 시간을 그 종류가 든 칸에 표시한다 (A3.6)
	void StartItemCooldown(int32 ItemId, int32 CooldownMs);

	// 상호작용 안내(A6.2): 주울 수 있는 드랍이 가까이 있으면 "[F] 줍기: 이름". 빈 글이면 숨긴다.
	void SetInteractPrompt(const FText& Prompt);

protected:
	// HUD가 내 캐릭터보다 늦게 만들어져도 현재 값으로 시작한다.
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UTextBlock> TxtName;

	// "Lv. N"
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UTextBlock> TxtLevel;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UProgressBar> HpBar;

	// "현재 / 최대"
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UTextBlock> TxtHp;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UProgressBar> ExpBar;

	// 상호작용 안내 글(없으면 안내를 띄우지 않는다)
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UTextBlock> TxtInteract;

	// 액티브 칸 1~5(숫자키). 칸 번호는 각 인스턴스의 SkillSlot 값. 기본 공격은 좌클릭이라 칸을 두지 않는다. (D-038·D-148)
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<USkillSlotWidget> Slot1;
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<USkillSlotWidget> Slot2;
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<USkillSlotWidget> Slot3;
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<USkillSlotWidget> Slot4;
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<USkillSlotWidget> Slot5;

private:
	TArray<USkillSlotWidget*, TInlineAllocator<5>> GetSlotWidgets() const;

	// 인벤토리·액티브 칸 배치가 바뀌면(UJMInventoryManager::OnInventoryChanged) 내 캐릭터의 칸 내용을 다시 만들어 그린다(수량·배치).
	void OnInventoryChanged();
	// 칸에 인벤토리 아이템을 끌어다 놓음 = 그 종류를 올리기, 칸 우클릭 = 비우기 (CS_SET_QUICKSLOT, 결과는 SC_INVENTORY_UPDATE)
	void OnSlotItemDropped(USkillSlotWidget* SlotWidget, int32 ItemId);
	void OnSlotRightClicked(USkillSlotWidget* SlotWidget);

	FDelegateHandle InventoryChangedHandle;

	// 마지막으로 받은 칸 내용(쿨타임을 어느 칸에 표시할지 찾는 데 쓴다)
	TArray<FQuickSlot> CurrentQuickSlots;
};
