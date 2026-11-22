#include "UI/QuestWidget.h"
#include "UI/UIStyle.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/ButtonSlot.h"
#include "Components/PanelWidget.h"
#include "Components/TextBlock.h"
#include "Fonts/SlateFontInfo.h"
#include "Data/ItemTableRow.h"
#include "Data/QuestTableRow.h"
#include "Game/DataManager.h"
#include "Game/JMQuestManager.h"
#include "UI/AuctionWidget.h"

namespace
{
	const FLinearColor DoneTextColor(0.55f, 0.55f, 0.55f, 1.f);
	const FLinearColor ReadyTextColor(1.f, 0.85f, 0.3f, 1.f);

	void SetShown(UWidget* Widget, bool bShown)
	{
		if (Widget) Widget->SetVisibility(bShown ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}

	// 퀘스트 줄 버튼 안의 글자
	UTextBlock* MakeText(UWidgetTree* Tree, int32 FontSize)
	{
		UTextBlock* Block = Tree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
		FSlateFontInfo FontInfo = Block->GetFont();
		JMUIStyle::ApplyFont(Tree->GetTypedOuter<UUserWidget>(), FontInfo, JMUIStyle::Body);   // 공용 글꼴(D-270)
		FontInfo.Size = FontSize;
		Block->SetFont(FontInfo);
		return Block;
	}

	FString ItemName(const UDataManager* DataManager, int32 ItemId)
	{
		const FItemTableRow* Item = DataManager ? DataManager->FindItemById(ItemId) : nullptr;
		return Item ? Item->DisplayName.ToString() : FString::Printf(TEXT("아이템 %d"), ItemId);
	}
}

UJMQuestManager* UQuestWidget::GetQuestManager() const
{
	const UGameInstance* GameInstance = GetGameInstance();
	return GameInstance ? GameInstance->GetSubsystem<UJMQuestManager>() : nullptr;
}

void UQuestWidget::NativeConstruct()
{
	Super::NativeConstruct();
	if (BtnAction) BtnAction->OnClicked.AddUniqueDynamic(this, &UQuestWidget::OnActionClicked);
	if (BtnClose) BtnClose->OnClicked.AddUniqueDynamic(this, &UQuestWidget::OnCloseClicked);
	if (UJMQuestManager* Quest = GetQuestManager())
	{
		QuestChangedHandle = Quest->OnQuestChanged.AddUObject(this, &UQuestWidget::Refresh);
	}
	Refresh();
}

void UQuestWidget::NativeDestruct()
{
	if (UJMQuestManager* Quest = GetQuestManager())
	{
		Quest->OnQuestChanged.Remove(QuestChangedHandle);
	}
	Super::NativeDestruct();
}

void UQuestWidget::Refresh()
{
	const UJMQuestManager* Quest = GetQuestManager();
	if (Quest == nullptr || !Quest->IsOpen()) return;
	const UDataManager* DataManager = GetGameInstance() ? GetGameInstance()->GetSubsystem<UDataManager>() : nullptr;
	const bool bAtNpc = Quest->IsAtNpc();

	if (TxtTitle) TxtTitle->SetText(FText::FromString(bAtNpc ? TEXT("퀘스트 — 대화") : TEXT("퀘스트 목록")));

	RowQuestIds = Quest->GetWindowQuestIds();
	SetShown(TxtEmpty, RowQuestIds.Num() == 0);
	if (TxtEmpty) TxtEmpty->SetText(FText::FromString(bAtNpc ? TEXT("이 NPC에게서 받을 퀘스트가 없습니다.") : TEXT("받은 퀘스트가 없습니다.")));

	// 줄은 퀘스트 수만큼만 보인다. 모자라면 만들고 남으면 숨긴다(경매 창 줄과 같은 버튼).
	while (QuestList && WidgetTree && Rows.Num() < RowQuestIds.Num())
	{
		UAuctionRowButton* Row = WidgetTree->ConstructWidget<UAuctionRowButton>(UAuctionRowButton::StaticClass());
		UTextBlock* Label = MakeText(WidgetTree, RowFontSize);
		if (UButtonSlot* ButtonSlot = Cast<UButtonSlot>(Row->AddChild(Label)))
		{
			ButtonSlot->SetHorizontalAlignment(HAlign_Left);
			ButtonSlot->SetPadding(FMargin(8.f, 3.f));
		}
		Row->Setup(Rows.Num(), Label);
		Row->OnRowClicked.BindUObject(this, &UQuestWidget::OnRowClicked);
		QuestList->AddChild(Row);
		Rows.Add(Row);
	}

	const int32 Selected = Quest->GetSelectedQuestId();
	for (int32 Index = 0; Index < Rows.Num(); ++Index)
	{
		UAuctionRowButton* Row = Rows[Index];
		if (Row == nullptr) continue;
		if (!RowQuestIds.IsValidIndex(Index))
		{
			Row->SetVisibility(ESlateVisibility::Collapsed);
			continue;
		}
		const int32 QuestId = RowQuestIds[Index];
		const FQuestTableRow* QuestRow = DataManager ? DataManager->FindQuest(QuestId) : nullptr;
		const FString Name = QuestRow ? QuestRow->DisplayName : FString::Printf(TEXT("퀘스트 %d"), QuestId);
		const Protocol::QuestState State = Quest->GetState(QuestId);
		const bool bReady = Quest->IsReadyToComplete(QuestId);
		FString Text;
		FLinearColor Color = FLinearColor::White;
		if (bReady) { Text = FString::Printf(TEXT("? %s"), *Name); Color = ReadyTextColor; }
		else if (State == Protocol::QUEST_STATE_NONE) Text = FString::Printf(TEXT("! %s"), *Name);
		else if (State == Protocol::QUEST_STATE_ACTIVE) Text = FString::Printf(TEXT("%s (%d/%d)"), *Name, Quest->GetProgress(QuestId), QuestRow ? QuestRow->Count : 0);
		else { Text = FString::Printf(TEXT("%s · 완료"), *Name); Color = DoneTextColor; }
		Row->SetRow(Text, QuestId == Selected, Color);
		Row->SetVisibility(ESlateVisibility::Visible);
	}

	if (TxtDetail) TxtDetail->SetText(FText::FromString(Selected != 0 ? BuildDetailText(Selected) : FString()));

	// [수락]/[완료]: 대화 모드에서만. 받지 않음 = 수락, 진행 중 = 완료(목표를 채웠을 때만 누를 수 있음), 완료함 = 숨김
	const Protocol::QuestState SelectedState = Quest->GetState(Selected);
	const bool bShowAction = bAtNpc && Selected != 0 && SelectedState != Protocol::QUEST_STATE_COMPLETED;
	SetShown(BtnAction, bShowAction);
	if (bShowAction)
	{
		const bool bAccept = SelectedState == Protocol::QUEST_STATE_NONE;
		if (TxtAction) TxtAction->SetText(FText::FromString(bAccept ? TEXT("수락") : TEXT("완료")));
		if (BtnAction) BtnAction->SetIsEnabled(bAccept || Quest->IsReadyToComplete(Selected));
	}
}

FString UQuestWidget::BuildDetailText(int32 QuestId) const
{
	const UJMQuestManager* Quest = GetQuestManager();
	const UDataManager* DataManager = GetGameInstance() ? GetGameInstance()->GetSubsystem<UDataManager>() : nullptr;
	const FQuestTableRow* Row = DataManager ? DataManager->FindQuest(QuestId) : nullptr;
	if (Quest == nullptr || Row == nullptr) return FString::Printf(TEXT("퀘스트 %d"), QuestId);

	const Protocol::QuestState State = Quest->GetState(QuestId);
	const TCHAR* StateText = Quest->IsReadyToComplete(QuestId) ? TEXT("완료할 수 있음 — 의뢰한 NPC에게 돌아가세요")
		: State == Protocol::QUEST_STATE_NONE ? TEXT("받지 않음")
		: State == Protocol::QUEST_STATE_ACTIVE ? TEXT("진행 중")
		: TEXT("완료함");

	const FString Goal = Row->Kind == EQuestKind::Collect
		? FString::Printf(TEXT("%s 모으기"), *ItemName(DataManager, Row->TargetId))
		: FString(TEXT("몬스터 처치"));
	FString Progress;
	if (State != Protocol::QUEST_STATE_NONE) Progress = FString::Printf(TEXT("  %d / %d"), Quest->GetProgress(QuestId), Row->Count);
	else if (Row->Kind == EQuestKind::Collect) Progress = FString::Printf(TEXT("  %d개"), Row->Count);
	else Progress = FString::Printf(TEXT("  %d마리"), Row->Count);

	FString Reward = FString::Printf(TEXT("경험치 %lld · 골드 %lld"), Row->RewardExp, Row->RewardGold);
	if (Row->RewardItemId != 0) Reward += FString::Printf(TEXT(" · %s ×%d"), *ItemName(DataManager, Row->RewardItemId), Row->RewardItemCount);

	return FString::Printf(TEXT("%s   권장 Lv.%d\n\n%s\n\n목표: %s%s\n보상: %s\n상태: %s"),
		*Row->DisplayName, Row->RecommendLevel, *Row->Description, *Goal, *Progress, *Reward, StateText);
}

void UQuestWidget::OnRowClicked(int32 Index)
{
	if (!RowQuestIds.IsValidIndex(Index)) return;
	if (UJMQuestManager* Quest = GetQuestManager()) Quest->SelectQuest(RowQuestIds[Index]);
}

void UQuestWidget::OnActionClicked()
{
	if (UJMQuestManager* Quest = GetQuestManager()) Quest->RequestSelected();
}

void UQuestWidget::OnCloseClicked()
{
	if (UJMQuestManager* Quest = GetQuestManager()) Quest->Close();
}
