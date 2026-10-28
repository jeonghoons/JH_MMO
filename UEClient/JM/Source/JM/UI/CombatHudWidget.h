#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "CombatHudWidget.generated.h"

class USkillSlotWidget;

// 전투 HUD: 내 HP 바·수치와 스킬 슬롯. 값은 모두 서버 패킷에서 온다(UJMObjectManager가 갱신).
UCLASS()
class JM_API UCombatHudWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void SetHp(int32 Hp, int32 MaxHp);

	// 영웅(Protocol::PlayerType)의 SkillTable 행으로 슬롯 아이콘을 채운다.
	void SetHero(int32 PlayerType);

	// 서버가 확정한 시전(SC_ATTACK)의 쿨타임을 해당 슬롯에 표시한다.
	void StartCooldown(int32 SkillSlot, int32 CooldownMs);

protected:
	// HUD가 내 캐릭터보다 늦게 만들어져도 현재 값으로 시작한다.
	virtual void NativeConstruct() override;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UProgressBar> HpBar;

	// "현재 / 최대"
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UTextBlock> TxtHp;

	// 스킬 슬롯(1~5번 키). 기본 공격은 쿨타임이 없어 슬롯을 두지 않는다. (Docs/DECISIONS.md 2026-10-28)
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<USkillSlotWidget> Slot1;

private:
	TArray<USkillSlotWidget*, TInlineAllocator<5>> GetSkillSlots() const;
};
