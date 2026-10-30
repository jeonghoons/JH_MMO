#include "UI/SystemMessageWidget.h"
#include "Components/TextBlock.h"
#include "Components/Button.h"
#include "Components/Border.h"
#include "UI/JMUIManager.h"

void USystemMessageWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (BtnOk)
	{
		BtnOk->OnClicked.AddUniqueDynamic(this, &USystemMessageWidget::OnOkClicked);
	}
}

void USystemMessageWidget::Setup(const FText& Message, bool bWaiting)
{
	if (TxtMessage)
	{
		TxtMessage->SetText(Message);
	}
	if (BtnOk)
	{
		BtnOk->SetVisibility(bWaiting ? ESlateVisibility::Collapsed : ESlateVisibility::Visible);
	}
	if (Dim)
	{
		Dim->SetBrushColor(bWaiting ? WaitingDimColor : NoticeDimColor);
	}
}

void USystemMessageWidget::OnOkClicked()
{
	UGameInstance* GameInstance = GetGameInstance();
	if (UJMUIManager* UIManager = GameInstance ? GameInstance->GetSubsystem<UJMUIManager>() : nullptr)
	{
		UIManager->CloseSystemMessage();
	}
}
