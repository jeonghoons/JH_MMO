#include "UI/PartyWidget.h"
#include "UI/UIStyle.h"
#include "Blueprint/WidgetTree.h"
#include "Components/PanelWidget.h"
#include "Components/TextBlock.h"
#include "Components/Button.h"
#include "Fonts/SlateFontInfo.h"
#include "Game/JMPartyManager.h"

namespace
{
	void SetShown(UWidget* Widget, bool bShown)
	{
		if (Widget) Widget->SetVisibility(bShown ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}

	const TCHAR* RoomKindText(Protocol::RoomKind Kind)
	{
		switch (Kind)
		{
		case Protocol::ROOM_KIND_TOWN:		return TEXT("마을");
		case Protocol::ROOM_KIND_FIELD:		return TEXT("사냥터");
		case Protocol::ROOM_KIND_DUNGEON:	return TEXT("던전");
		default:							return TEXT("이동 중");
		}
	}
}

UJMPartyManager* UPartyWidget::GetPartyManager() const
{
	const UGameInstance* GameInstance = GetGameInstance();
	return GameInstance ? GameInstance->GetSubsystem<UJMPartyManager>() : nullptr;
}

void UPartyWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (BtnAccept) BtnAccept->OnClicked.AddUniqueDynamic(this, &UPartyWidget::OnAcceptClicked);
	if (BtnDecline) BtnDecline->OnClicked.AddUniqueDynamic(this, &UPartyWidget::OnDeclineClicked);
	if (BtnLeave) BtnLeave->OnClicked.AddUniqueDynamic(this, &UPartyWidget::OnLeaveClicked);

	if (UJMPartyManager* Party = GetPartyManager())
	{
		PartyChangedHandle = Party->OnPartyChanged.AddUObject(this, &UPartyWidget::Refresh);
	}
	Refresh();
}

void UPartyWidget::NativeDestruct()
{
	if (UJMPartyManager* Party = GetPartyManager())
	{
		Party->OnPartyChanged.Remove(PartyChangedHandle);
	}
	Super::NativeDestruct();
}

void UPartyWidget::Refresh()
{
	const UJMPartyManager* Party = GetPartyManager();
	if (Party == nullptr) return;

	// 초대가 와 있으면 초대 화면만, 아니면 파티 화면.
	const bool bInvite = Party->HasInvite();
	SetShown(PanelInvite, bInvite);
	SetShown(PanelParty, !bInvite);
	if (bInvite)
	{
		const FString From = Party->GetInviteFromName().IsEmpty() ? FString(TEXT("상대")) : Party->GetInviteFromName();
		if (TxtInvite) TxtInvite->SetText(FText::FromString(FString::Printf(TEXT("%s 님이 파티에 초대했습니다."), *From)));
		return;
	}

	const TArray<FJMPartyMember>& Members = Party->GetMembers();
	const bool bInParty = Party->IsInParty();
	SetShown(TxtEmpty, !bInParty);
	SetShown(BtnLeave, bInParty);
	if (MemberList == nullptr) return;

	// 줄은 인원만큼만 보인다. 모자라면 만들고 남으면 숨긴다.
	while (MemberRows.Num() < Members.Num())
	{
		UTextBlock* Row = WidgetTree ? WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass()) : nullptr;
		if (Row == nullptr) return;
		FSlateFontInfo FontInfo = Row->GetFont();
		JMUIStyle::ApplyFont(this, FontInfo, JMUIStyle::Body);   // 공용 글꼴(D-270)
		FontInfo.Size = MemberFontSize;
		Row->SetFont(FontInfo);
		MemberList->AddChild(Row);
		MemberRows.Add(Row);
	}

	const int32 MyId = Party->GetMyId();
	for (int32 Index = 0; Index < MemberRows.Num(); ++Index)
	{
		UTextBlock* Row = MemberRows[Index];
		if (Row == nullptr) continue;
		if (!Members.IsValidIndex(Index))
		{
			Row->SetVisibility(ESlateVisibility::Collapsed);
			continue;
		}

		const FJMPartyMember& Member = Members[Index];
		const bool bLeader = Member.ObjectId == Party->GetLeaderId();
		Row->SetText(FText::FromString(FString::Printf(TEXT("%s %s   Lv.%d   %s"),
			bLeader ? TEXT("[파티장]") : TEXT("[파티원]"), *Member.Name, Member.Level, RoomKindText(Member.Kind))));
		// 내 줄은 노란색
		Row->SetColorAndOpacity(FSlateColor(Member.ObjectId == MyId ? FLinearColor(1.f, 0.85f, 0.3f) : FLinearColor::White));
		Row->SetVisibility(ESlateVisibility::HitTestInvisible);
	}
}

void UPartyWidget::OnAcceptClicked()
{
	if (UJMPartyManager* Party = GetPartyManager()) Party->Respond(true);
}

void UPartyWidget::OnDeclineClicked()
{
	if (UJMPartyManager* Party = GetPartyManager()) Party->Respond(false);
}

void UPartyWidget::OnLeaveClicked()
{
	if (UJMPartyManager* Party = GetPartyManager()) Party->Leave();
}
