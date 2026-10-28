#include "UI/CombatHudWidget.h"
#include "UI/SkillSlotWidget.h"
#include "Components/ProgressBar.h"
#include "Components/TextBlock.h"
#include "Game/DataManager.h"
#include "Game/JMObjectManager.h"
#include "Player/JMMyPlayer.h"

void UCombatHudWidget::NativeConstruct()
{
	Super::NativeConstruct();

	UJMObjectManager* ObjectManager = GetGameInstance() ? GetGameInstance()->GetSubsystem<UJMObjectManager>() : nullptr;
	if (ObjectManager && ObjectManager->MyPlayer)
	{
		const Protocol::ObjectInfo& Info = ObjectManager->MyPlayer->GetObjectInfo();
		SetHero(static_cast<int32>(Info.player_type()));
		SetHp(Info.stat().hp(), Info.stat().max_hp());
	}
	else
	{
		SetHero(0);
		SetHp(0, 0);
	}
}

void UCombatHudWidget::SetHp(int32 Hp, int32 MaxHp)
{
	Hp = FMath::Max(Hp, 0);
	if (HpBar) HpBar->SetPercent(MaxHp > 0 ? static_cast<float>(Hp) / MaxHp : 0.f);
	if (TxtHp) TxtHp->SetText(FText::FromString(FString::Printf(TEXT("%d / %d"), Hp, MaxHp)));
}

void UCombatHudWidget::SetHero(int32 PlayerType)
{
	UDataManager* DataManager = GetGameInstance() ? GetGameInstance()->GetSubsystem<UDataManager>() : nullptr;
	for (USkillSlotWidget* SkillSlotWidget : GetSkillSlots())
	{
		SkillSlotWidget->SetSkill(DataManager ? DataManager->FindSkillBySlot(PlayerType, SkillSlotWidget->GetSkillSlot()) : nullptr);
	}
}

void UCombatHudWidget::StartCooldown(int32 SkillSlot, int32 CooldownMs)
{
	for (USkillSlotWidget* SkillSlotWidget : GetSkillSlots())
	{
		if (SkillSlotWidget->GetSkillSlot() == SkillSlot) SkillSlotWidget->StartCooldown(CooldownMs);
	}
}

TArray<USkillSlotWidget*, TInlineAllocator<5>> UCombatHudWidget::GetSkillSlots() const
{
	TArray<USkillSlotWidget*, TInlineAllocator<5>> SkillSlots;
	if (Slot1) SkillSlots.Add(Slot1);
	return SkillSlots;
}
