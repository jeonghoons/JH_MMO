#include "ChatWidget.h"
#include "Components/EditableText.h"
#include "Components/ScrollBox.h"
#include "Components/TextBlock.h"
#include "Fonts/SlateFontInfo.h"
#include "Network/NetworkManager.h"
#include "UI/JMUIManager.h"

void UChatWidget::FocusChatInput(APlayerController* PC)
{
	if (ChatInput && PC)
	{
		// 1. 키보드 입력을 이 텍스트 박스로 가져옵니다.
		ChatInput->SetKeyboardFocus();

		// 2. 입력 모드를 UI 전용(또는 GameAndUI)으로 바꿔서 WASD로 캐릭터가 움직이지 않게 합니다.
		FInputModeUIOnly InputMode;
		InputMode.SetWidgetToFocus(ChatInput->TakeWidget());
		PC->SetInputMode(InputMode);
		PC->bShowMouseCursor = true;
	}
}

void UChatWidget::AddChatMessage(const FString& SenderName, const FString& Message)
{
	if (!ChatHistoryBox) return;

	// 오래된 줄부터 지워 기록이 끝없이 쌓이지 않게 한다.
	while (MaxHistoryLines > 0 && ChatHistoryBox->GetChildrenCount() >= MaxHistoryLines)
	{
		ChatHistoryBox->RemoveChildAt(0);
	}

	FString FormattedString = FString::Printf(TEXT("%s: %s"), *SenderName, *Message);

	UTextBlock* NewChatBlock = NewObject<UTextBlock>(ChatHistoryBox);
	if (NewChatBlock)
	{
		NewChatBlock->SetText(FText::FromString(FormattedString));

		// 1. 폰트 정보 가져오기
		FSlateFontInfo FontInfo = NewChatBlock->GetFont();

		// 2. 글씨 크기 키우기 (기존 14 -> 18 또는 20으로 조절)
		FontInfo.Size = 18;

		// 3. 글씨 윤곽선(Outline) 설정
		FontInfo.OutlineSettings.OutlineSize = 1; // 윤곽선 두께 (1~2 추천)
		FontInfo.OutlineSettings.OutlineColor = FLinearColor::Black; // 윤곽선 색상

		// 4. 변경된 폰트 정보 및 글씨 색상 적용
		NewChatBlock->SetFont(FontInfo);
		NewChatBlock->SetColorAndOpacity(FSlateColor(FLinearColor::White)); // 글씨 속 색상

		// (선택 사항) 가독성을 극대화하기 위해 글씨 뒤에 옅은 그림자 추가
		NewChatBlock->SetShadowOffset(FVector2D(1.0f, 1.0f));
		NewChatBlock->SetShadowColorAndOpacity(FLinearColor(0.0f, 0.0f, 0.0f, 0.8f)); // 반투명 검은색 그림자

		// 스크롤 박스에 추가 및 맨 아래로 스크롤
		ChatHistoryBox->AddChild(NewChatBlock);
		ChatHistoryBox->ScrollToEnd();
	}
}

void UChatWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (ChatInput)
	{
		// 텍스트 박스에서 엔터를 치거나 포커스를 잃었을 때 OnChatTextCommitted 함수가 실행되도록 연결
		ChatInput->OnTextCommitted.AddDynamic(this, &UChatWidget::OnChatTextCommitted);
	}
}

void UChatWidget::OnChatTextCommitted(const FText& Text, ETextCommit::Type CommitMethod)
{
	// 1. 엔터키를 쳤을 때만 전송 로직 수행
	if (CommitMethod == ETextCommit::OnEnter)
	{
		const FString ChatString = Text.ToString().TrimStartAndEnd();

		// 내가 보낸 채팅도 서버가 방송한 SC_CHAT으로 기록에 표시된다.
		if (!ChatString.IsEmpty())
		{
			if (UNetworkManager* NetManager = UNetworkManager::Get(this))
			{
				NetManager->SendChat(ChatString);
			}
		}
		ChatInput->SetText(FText::GetEmpty());
	}

	// 2. 입력이 어떤 식으로든 종료되었다면 게임 입력으로 복귀 (if문 밖으로 분리)
	if (CommitMethod == ETextCommit::OnEnter ||
		CommitMethod == ETextCommit::OnUserMovedFocus ||
		CommitMethod == ETextCommit::OnCleared)
	{
		FSlateApplication::Get().SetAllUserFocusToGameViewport();

		// 시스템 메시지 창 등이 떠 있으면 커서를 유지하도록 입력 모드는 UIManager가 정한다.
		UGameInstance* GameInstance = GetGameInstance();
		if (UJMUIManager* UIManager = GameInstance ? GameInstance->GetSubsystem<UJMUIManager>() : nullptr)
		{
			UIManager->UpdateInputMode();
		}
	}
}
