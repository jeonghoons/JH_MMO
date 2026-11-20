#include "UI/CombatHudWidget.h"
#include "UI/SkillSlotWidget.h"
#include "Components/ProgressBar.h"
#include "Components/TextBlock.h"
#include "Game/DataManager.h"
#include "Game/JMObjectManager.h"
#include "Player/JMMyPlayer.h"
#include "Game/JMInventoryManager.h"
#include "Network/NetworkManager.h"

void UCombatHudWidget::NativeConstruct()
{
	Super::NativeConstruct();

	SetExpRatio(0.f);

	for (USkillSlotWidget* SlotWidget : GetSlotWidgets())
	{
		SlotWidget->OnItemDropped.BindUObject(this, &UCombatHudWidget::OnSlotItemDropped);
		SlotWidget->OnRightClicked.BindUObject(this, &UCombatHudWidget::OnSlotRightClicked);
	}
	if (UJMInventoryManager* Inventory = GetGameInstance() ? GetGameInstance()->GetSubsystem<UJMInventoryManager>() : nullptr)
	{
		InventoryChangedHandle = Inventory->OnInventoryChanged.AddUObject(this, &UCombatHudWidget::OnInventoryChanged);
	}

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

	// 던전 상태: 패킷은 HUD가 만들어진 뒤에 오지만, 순서가 바뀌어도 지금 값으로 시작한다.
	if (ObjectManager) SetDungeonState(ObjectManager->GetDungeonState(), ObjectManager->GetDungeonClearEndSec());
	else SetDungeonState(0, 0.0);
}

void UCombatHudWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	// 클리어 뒤 남은 시간만 시간이 흐르면 바뀐다.
	if (DungeonStateValue == Protocol::DUNGEON_STATE_CLEARED) RefreshDungeonText();
}

void UCombatHudWidget::SetDungeonState(int32 State, double ClearEndSec)
{
	DungeonStateValue = State;
	DungeonClearEndSec = ClearEndSec;
	ShownDungeonSec = -1;
	RefreshDungeonText();
}

void UCombatHudWidget::RefreshDungeonText()
{
	if (TxtDungeon == nullptr) return;

	if (DungeonStateValue == Protocol::DUNGEON_STATE_CLEARED)
	{
		const int32 Remain = FMath::Max(FMath::CeilToInt32(DungeonClearEndSec - FPlatformTime::Seconds()), 0);
		if (Remain == ShownDungeonSec) return;
		ShownDungeonSec = Remain;
		TxtDungeon->SetText(FText::FromString(FString::Printf(TEXT("던전 클리어 — %d초 뒤 마을로 이동"), Remain)));
		TxtDungeon->SetVisibility(ESlateVisibility::HitTestInvisible);
	}
	else if (DungeonStateValue == Protocol::DUNGEON_STATE_IN_PROGRESS)
	{
		TxtDungeon->SetText(FText::FromString(TEXT("던전 진행 중")));
		TxtDungeon->SetVisibility(ESlateVisibility::HitTestInvisible);
	}
	else
	{
		TxtDungeon->SetVisibility(ESlateVisibility::Collapsed);
	}
}

void UCombatHudWidget::NativeDestruct()
{
	if (UJMInventoryManager* Inventory = GetGameInstance() ? GetGameInstance()->GetSubsystem<UJMInventoryManager>() : nullptr)
	{
		Inventory->OnInventoryChanged.Remove(InventoryChangedHandle);
	}
	Super::NativeDestruct();
}

void UCombatHudWidget::OnInventoryChanged()
{
	UJMObjectManager* ObjectManager = GetGameInstance() ? GetGameInstance()->GetSubsystem<UJMObjectManager>() : nullptr;
	AJMMyPlayer* MyPlayer = ObjectManager ? ObjectManager->MyPlayer.Get() : nullptr;
	if (MyPlayer == nullptr) return;
	MyPlayer->RebuildQuickSlots();
	SetQuickSlots(MyPlayer->GetQuickSlots(), MyPlayer->GetWeaponType());
}

void UCombatHudWidget::OnSlotItemDropped(USkillSlotWidget* SlotWidget, int32 ItemId)
{
	if (SlotWidget == nullptr || ItemId == 0) return;
	// 검증(사용할 수 있는 종류인지, 스킬이 있는 칸인지)은 서버가 하고 거부는 시스템 메시지로 온다.
	if (UNetworkManager* NetManager = UNetworkManager::Get(this))
	{
		NetManager->SendSetQuickSlot(SlotWidget->GetSkillSlot(), ItemId);
	}
}

void UCombatHudWidget::OnSlotRightClicked(USkillSlotWidget* SlotWidget)
{
	if (SlotWidget == nullptr) return;
	// 아이템이 든 칸만 비운다(스킬 칸·빈 칸은 그대로).
	const int32 Index = SlotWidget->GetSkillSlot() - 1;
	if (!CurrentQuickSlots.IsValidIndex(Index) || CurrentQuickSlots[Index].Kind != EQuickSlotKind::Item) return;
	if (UNetworkManager* NetManager = UNetworkManager::Get(this))
	{
		NetManager->SendSetQuickSlot(SlotWidget->GetSkillSlot(), 0);
	}
}

void UCombatHudWidget::StartItemCooldown(int32 ItemId, int32 CooldownMs)
{
	for (USkillSlotWidget* SlotWidget : GetSlotWidgets())
	{
		const int32 Index = SlotWidget->GetSkillSlot() - 1;
		if (!CurrentQuickSlots.IsValidIndex(Index)) continue;
		const FQuickSlot& QuickSlot = CurrentQuickSlots[Index];
		if (QuickSlot.Kind == EQuickSlotKind::Item && QuickSlot.Value == ItemId) SlotWidget->StartCooldown(CooldownMs);
	}
}

void UCombatHudWidget::SetInteractPrompt(const FText& Prompt)
{
	if (TxtInteract == nullptr) return;
	TxtInteract->SetText(Prompt);
	TxtInteract->SetVisibility(Prompt.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
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
	const UJMInventoryManager* Inventory = GetGameInstance() ? GetGameInstance()->GetSubsystem<UJMInventoryManager>() : nullptr;
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
			SlotWidget->SetItem(DataManager ? DataManager->FindItemById(QuickSlot.Value) : nullptr, Inventory ? Inventory->GetBagItemCount(QuickSlot.Value) : 0);
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

void UCombatHudWidget::SetCooldownRemain(int32 SkillSlot, int32 RemainMs, int32 TotalMs)
{
	for (USkillSlotWidget* SlotWidget : GetSlotWidgets())
	{
		const int32 Index = SlotWidget->GetSkillSlot() - 1;
		if (!CurrentQuickSlots.IsValidIndex(Index)) continue;
		const FQuickSlot& QuickSlot = CurrentQuickSlots[Index];
		if (QuickSlot.Kind == EQuickSlotKind::Skill && QuickSlot.Value == SkillSlot) SlotWidget->SetCooldownRemain(RemainMs, TotalMs);
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
