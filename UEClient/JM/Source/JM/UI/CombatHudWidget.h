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

protected:
	// HUD가 내 캐릭터보다 늦게 만들어져도 현재 값으로 시작한다.
	virtual void NativeConstruct() override;

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

	// 마지막으로 받은 칸 내용(쿨타임을 어느 칸에 표시할지 찾는 데 쓴다)
	TArray<FQuickSlot> CurrentQuickSlots;
};
