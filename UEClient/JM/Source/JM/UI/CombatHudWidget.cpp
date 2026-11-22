#include "UI/CombatHudWidget.h"
#include "UI/SkillSlotWidget.h"
#include "Components/ProgressBar.h"
#include "Components/TextBlock.h"
#include "Game/DataManager.h"
#include "Game/JMObjectManager.h"
#include "Player/JMMyPlayer.h"
#include "Game/JMInventoryManager.h"
#include "Network/NetworkManager.h"
#include "Game/JMQuestManager.h"
#include "Game/JMAuctionManager.h"

void UCombatHudWidget::NativeConstruct()
{
	Super::NativeConstruct();

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
		SetMp(Info.stat().mp(), Info.stat().max_mp());
		SetExp(Info.exp(), Info.level());
	}
	else
	{
		SetIdentity(FString(), 0);
		SetQuickSlots(TArray<FQuickSlot>(), EWeaponType::None);
		SetHp(0, 0);
		SetMp(0, 0);
		SetExp(0, 0);
	}

	// 던전 상태: 패킷은 HUD가 만들어진 뒤에 오지만, 순서가 바뀌어도 지금 값으로 시작한다.
	if (ObjectManager) SetDungeonState(ObjectManager->GetDungeonState(), ObjectManager->GetDungeonClearEndSec());
	else SetDungeonState(0, 0.0);

	// 퀘스트 추적 줄·수령함 알림(Q1, D-265·D-266 — WBP_CombatHud의 TxtQuestTracker·TxtMail)
	Notices.Reset();
	RefreshNotice();
	if (UJMQuestManager* Quest = GetGameInstance() ? GetGameInstance()->GetSubsystem<UJMQuestManager>() : nullptr)
	{
		QuestChangedHandle = Quest->OnQuestChanged.AddUObject(this, &UCombatHudWidget::RefreshQuestTracker);
	}
	if (UJMAuctionManager* Auction = GetGameInstance() ? GetGameInstance()->GetSubsystem<UJMAuctionManager>() : nullptr)
	{
		AuctionChangedHandle = Auction->OnAuctionChanged.AddUObject(this, &UCombatHudWidget::RefreshMailNotice);
	}
	RefreshQuestTracker();
	RefreshMailNotice();
}

void UCombatHudWidget::ShowNotice(const FText& Message)
{
	if (TxtNotice == nullptr || Message.IsEmpty()) return;
	// 새 알림은 아래에 붙이고, 줄 수가 넘치면 가장 오래된 것부터 뺀다.
	FNoticeLine& Line = Notices.AddDefaulted_GetRef();
	Line.Text = Message.ToString();
	Line.EndSec = FPlatformTime::Seconds() + FMath::Max(0.5f, NoticeDurationSec);
	while (Notices.Num() > FMath::Max(1, NoticeMaxLines)) Notices.RemoveAt(0);
	RefreshNotice();
}

void UCombatHudWidget::RefreshNotice()
{
	if (TxtNotice == nullptr) return;
	const double Now = FPlatformTime::Seconds();
	Notices.RemoveAll([Now](const FNoticeLine& Line) { return Line.EndSec <= Now; });
	if (Notices.Num() == 0)
	{
		TxtNotice->SetVisibility(ESlateVisibility::Collapsed);
		return;
	}
	FString Text;
	for (const FNoticeLine& Line : Notices)
	{
		if (!Text.IsEmpty()) Text += TEXT("\n");
		Text += Line.Text;
	}
	TxtNotice->SetText(FText::FromString(Text));
	// 마지막 0.5초는 흐려진다(가장 새 알림 기준 — 남은 줄이 함께 사라진다)
	const double Remain = Notices.Last().EndSec - Now;
	TxtNotice->SetRenderOpacity(static_cast<float>(FMath::Clamp(Remain / 0.5, 0.0, 1.0)));
	TxtNotice->SetVisibility(ESlateVisibility::HitTestInvisible);
}

void UCombatHudWidget::RefreshQuestTracker()
{
	if (TxtQuestTracker == nullptr) return;
	const UGameInstance* GameInstance = GetGameInstance();
	const UJMQuestManager* Quest = GameInstance ? GameInstance->GetSubsystem<UJMQuestManager>() : nullptr;
	const UDataManager* DataManager = GameInstance ? GameInstance->GetSubsystem<UDataManager>() : nullptr;
	FString Text;
	if (Quest && DataManager)
	{
		for (const int32 QuestId : Quest->GetActiveQuestIds())
		{
			const FQuestTableRow* Row = DataManager->FindQuest(QuestId);
			if (Row == nullptr) continue;
			if (!Text.IsEmpty()) Text += TEXT("\n");
			Text += Quest->IsReadyToComplete(QuestId)
				? FString::Printf(TEXT("%s  완료 가능"), *Row->DisplayName)
				: FString::Printf(TEXT("%s  %d/%d"), *Row->DisplayName, Quest->GetProgress(QuestId), Row->Count);
		}
	}
	if (!Text.IsEmpty()) Text = FString(TEXT("[퀘스트]\n")) + Text;
	TxtQuestTracker->SetText(FText::FromString(Text));
	TxtQuestTracker->SetVisibility(Text.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
}

void UCombatHudWidget::RefreshMailNotice()
{
	if (TxtMail == nullptr) return;
	const UJMAuctionManager* Auction = GetGameInstance() ? GetGameInstance()->GetSubsystem<UJMAuctionManager>() : nullptr;
	const bool bShow = Auction && Auction->HasMailNotice();
	TxtMail->SetText(FText::FromString(TEXT("수령함에 받을 것이 있습니다")));
	TxtMail->SetVisibility(bShow ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
}

void UCombatHudWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	// 클리어 뒤 남은 시간만 시간이 흐르면 바뀐다.
	if (DungeonStateValue == Protocol::DUNGEON_STATE_CLEARED) RefreshDungeonText();
	// 알림 줄은 떠 있는 동안만 시간에 따라 흐려지고 사라진다.
	if (Notices.Num() > 0) RefreshNotice();
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
	if (UJMQuestManager* Quest = GetGameInstance() ? GetGameInstance()->GetSubsystem<UJMQuestManager>() : nullptr)
	{
		Quest->OnQuestChanged.Remove(QuestChangedHandle);
	}
	if (UJMAuctionManager* Auction = GetGameInstance() ? GetGameInstance()->GetSubsystem<UJMAuctionManager>() : nullptr)
	{
		Auction->OnAuctionChanged.Remove(AuctionChangedHandle);
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

void UCombatHudWidget::SetMp(int32 Mp, int32 MaxMp)
{
	Mp = FMath::Max(Mp, 0);
	if (MpBar) MpBar->SetPercent(MaxMp > 0 ? static_cast<float>(Mp) / MaxMp : 0.f);
	if (TxtMp) TxtMp->SetText(FText::FromString(FString::Printf(TEXT("%d / %d"), Mp, MaxMp)));
}

void UCombatHudWidget::SetExp(int64 Exp, int32 Level)
{
	if (Level <= 0)
	{
		if (ExpBar) ExpBar->SetPercent(0.f);
		if (TxtExp) TxtExp->SetText(FText::GetEmpty());
		return;
	}

	UDataManager* DataManager = GetGameInstance() ? GetGameInstance()->GetSubsystem<UDataManager>() : nullptr;
	const int64 ExpToNext = DataManager ? DataManager->GetExpToNext(Level) : 0;
	if (ExpToNext <= 0)
	{
		// 최대 레벨(서버는 exp 0을 보냄)
		if (ExpBar) ExpBar->SetPercent(1.f);
		if (TxtExp) TxtExp->SetText(FText::FromString(TEXT("MAX")));
		return;
	}

	Exp = FMath::Clamp<int64>(Exp, 0, ExpToNext);
	const float Ratio = static_cast<float>(static_cast<double>(Exp) / static_cast<double>(ExpToNext));
	if (ExpBar) ExpBar->SetPercent(Ratio);
	if (TxtExp) TxtExp->SetText(FText::FromString(FString::Printf(TEXT("%lld / %lld (%.1f%%)"), Exp, ExpToNext, Ratio * 100.f)));
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
