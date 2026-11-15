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

	SetExpRatio(0.f);

	UJMObjectManager* ObjectManager = GetGameInstance() ? GetGameInstance()->GetSubsystem<UJMObjectManager>() : nullptr;
	if (ObjectManager && ObjectManager->MyPlayer)
	{
		AJMMyPlayer* MyPlayer = ObjectManager->MyPlayer;
		const Protocol::ObjectInfo& Info = MyPlayer->GetObjectInfo();
		SetIdentity(UTF8_TO_TCHAR(Info.name().c_str()), Info.level());
		SetQuickSlots(MyPlayer->GetQuickSlots(), MyPlayer->GetWeaponType());
		SetHp(Info.stat().hp(), Info.stat().max_hp());
	}
	else
	{
		SetIdentity(FString(), 0);
		SetQuickSlots(TArray<FQuickSlot>(), EWeaponType::None);
		SetHp(0, 0);
	}
}

void UCombatHudWidget::SetHp(int32 Hp, int32 MaxHp)
{
	Hp = FMath::Max(Hp, 0);
	if (HpBar) HpBar->SetPercent(MaxHp > 0 ? static_cast<float>(Hp) / MaxHp : 0.f);
	if (TxtHp) TxtHp->SetText(FText::FromString(FString::Printf(TEXT("%d / %d"), Hp, MaxHp)));
}

void UCombatHudWidget::SetIdentity(const FString& Name, int32 Level)
{
	if (TxtName) TxtName->SetText(FText::FromString(Name));
	if (TxtLevel) TxtLevel->SetText(Level > 0 ? FText::FromString(FString::Printf(TEXT("Lv. %d"), Level)) : FText::GetEmpty());
}

void UCombatHudWidget::SetExpRatio(float Ratio)
{
	if (ExpBar) ExpBar->SetPercent(FMath::Clamp(Ratio, 0.f, 1.f));
}

void UCombatHudWidget::SetQuickSlots(const TArray<FQuickSlot>& QuickSlots, EWeaponType WeaponType)
{
	CurrentQuickSlots = QuickSlots;

	UDataManager* DataManager = GetGameInstance() ? GetGameInstance()->GetSubsystem<UDataManager>() : nullptr;
	for (USkillSlotWidget* SlotWidget : GetSlotWidgets())
	{
		const int32 Index = SlotWidget->GetSkillSlot() - 1;   // 위젯의 칸 번호 1~5
		const FQuickSlot QuickSlot = CurrentQuickSlots.IsValidIndex(Index) ? CurrentQuickSlots[Index] : FQuickSlot();

		switch (QuickSlot.Kind)
		{
		case EQuickSlotKind::Skill:
			SlotWidget->SetSkill(DataManager ? DataManager->FindSkillBySlot(WeaponType, QuickSlot.Value) : nullptr);
			break;
		case EQuickSlotKind::Item:
			SlotWidget->SetItem(DataManager ? DataManager->FindItemById(QuickSlot.Value) : nullptr);
			break;
		default:
			SlotWidget->SetSkill(nullptr);   // 빈 칸: 아이콘 숨김, 키 글자만
			break;
		}
	}
}

void UCombatHudWidget::StartCooldown(int32 SkillSlot, int32 CooldownMs)
{
	for (USkillSlotWidget* SlotWidget : GetSlotWidgets())
	{
		const int32 Index = SlotWidget->GetSkillSlot() - 1;
		if (!CurrentQuickSlots.IsValidIndex(Index)) continue;
		const FQuickSlot& QuickSlot = CurrentQuickSlots[Index];
		if (QuickSlot.Kind == EQuickSlotKind::Skill && QuickSlot.Value == SkillSlot) SlotWidget->StartCooldown(CooldownMs);
	}
}

TArray<USkillSlotWidget*, TInlineAllocator<5>> UCombatHudWidget::GetSlotWidgets() const
{
	TArray<USkillSlotWidget*, TInlineAllocator<5>> SlotWidgets;
	for (USkillSlotWidget* SlotWidget : { Slot1.Get(), Slot2.Get(), Slot3.Get(), Slot4.Get(), Slot5.Get() })
	{
		if (SlotWidget) SlotWidgets.Add(SlotWidget);
	}
	return SlotWidgets;
}
