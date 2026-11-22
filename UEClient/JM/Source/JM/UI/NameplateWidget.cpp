#include "UI/NameplateWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Fonts/SlateFontInfo.h"
#include "Game/JMQuestManager.h"

namespace
{
	UTextBlock* MakeLine(UWidgetTree* Tree, UVerticalBox* Box, int32 FontSize, int32 Outline, const FLinearColor& Color)
	{
		UTextBlock* Text = Tree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
		FSlateFontInfo FontInfo = Text->GetFont();
		FontInfo.Size = FontSize;
		// 밝은 배경(나무 벽·하늘)에서도 읽히게 검은 외곽선
		FontInfo.OutlineSettings.OutlineSize = Outline;
		FontInfo.OutlineSettings.OutlineColor = FLinearColor::Black;
		Text->SetFont(FontInfo);
		Text->SetColorAndOpacity(FSlateColor(Color));
		Text->SetJustification(ETextJustify::Center);
		if (UVerticalBoxSlot* BoxSlot = Box->AddChildToVerticalBox(Text)) BoxSlot->SetHorizontalAlignment(HAlign_Center);
		return Text;
	}
}

bool UNameplateWidget::Initialize()
{
	const bool bFirst = Super::Initialize();
	// WBP 없이 쓰면 트리가 비어 있으므로 여기서 만든다.
	if (bFirst && WidgetTree && WidgetTree->RootWidget == nullptr)
	{
		UVerticalBox* Box = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
		WidgetTree->RootWidget = Box;
		TxtMarker = MakeLine(WidgetTree, Box, 26, 2, FLinearColor(1.f, 0.82f, 0.15f));
		TxtName = MakeLine(WidgetTree, Box, 13, 1, FLinearColor(0.85f, 0.95f, 1.f));
	}
	if (bFirst) SetVisibility(ESlateVisibility::HitTestInvisible);
	return bFirst;
}

void UNameplateWidget::NativeConstruct()
{
	Super::NativeConstruct();
	if (UJMQuestManager* Quest = GetGameInstance() ? GetGameInstance()->GetSubsystem<UJMQuestManager>() : nullptr)
	{
		QuestChangedHandle = Quest->OnQuestChanged.AddUObject(this, &UNameplateWidget::RefreshMarker);
	}
	RefreshMarker();
}

void UNameplateWidget::NativeDestruct()
{
	if (UJMQuestManager* Quest = GetGameInstance() ? GetGameInstance()->GetSubsystem<UJMQuestManager>() : nullptr)
	{
		Quest->OnQuestChanged.Remove(QuestChangedHandle);
	}
	Super::NativeDestruct();
}

void UNameplateWidget::Setup(const FString& Name, int32 InNpcTemplateId)
{
	NpcTemplateId = InNpcTemplateId;
	if (TxtName) TxtName->SetText(FText::FromString(Name));
	RefreshMarker();
}

void UNameplateWidget::RefreshMarker()
{
	if (TxtMarker == nullptr) return;
	const UJMQuestManager* Quest = GetGameInstance() ? GetGameInstance()->GetSubsystem<UJMQuestManager>() : nullptr;
	const FString Marker = (Quest && NpcTemplateId != 0) ? Quest->GetNpcMarker(NpcTemplateId) : FString();
	if (Marker != ShownMarker)
	{
		UE_LOG(LogTemp, Log, TEXT("Nameplate: npc template=%d marker='%s'"), NpcTemplateId, *Marker);
		ShownMarker = Marker;
	}
	TxtMarker->SetText(FText::FromString(Marker));
	// 표식이 없어도 줄 높이는 남겨 이름 위치가 흔들리지 않게 한다.
	TxtMarker->SetVisibility(Marker.IsEmpty() ? ESlateVisibility::Hidden : ESlateVisibility::HitTestInvisible);
}
