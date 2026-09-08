#include "ChatWidget.h"
#include "Components/EditableText.h"
#include "Components/ScrollBox.h"
#include "Components/TextBlock.h"
#include "Fonts/SlateFontInfo.h"
#include "Network/NetworkManager.h"
#include "Network/SendBuffer.h"

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

void UChatWidget::AddChatMessage(int32 SenderId, const FString& Message)
{
	if (!ChatHistoryBox) return;

	FString FormattedString = FString::Printf(TEXT("Player[%d]: %s"), SenderId, *Message);

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
		FString ChatString = Text.ToString();

		if (!ChatString.IsEmpty())
		{
			//int chatLen = ChatString.Len();
			//if (chatLen >= MAX_CHAT_LEN) chatLen = MAX_CHAT_LEN - 1;

			//CS_CHAT_PACKET chatPacket;
			//unsigned short packetSize = sizeof(PacketHeader) + ((chatLen + 1) * sizeof(wchar_t));
			//chatPacket.header = { packetSize , CS_CHAT };
			//FCString::Strncpy(chatPacket.message, *ChatString, MAX_CHAT_LEN);

			//TSharedPtr<SendBuffer> sendBuffer = MakeShared<SendBuffer>(packetSize);
			//sendBuffer->CopyData(&chatPacket, packetSize); // <-- packetSize로 수정!

			//UMyGameInstance* GameInst = Cast<UMyGameInstance>(GetGameInstance());
			//if (GameInst && GameInst->GetSubsystem<UNetworkManager>())
			//{
			//	GameInst->GetSubsystem<UNetworkManager>()->SendPacket(sendBuffer);
			//}

			ChatInput->SetText(FText::GetEmpty());
		}
	}

	// 2. 입력이 어떤 식으로든 종료되었다면 게임 모드로 완벽히 복귀 (if문 밖으로 분리)
	if (CommitMethod == ETextCommit::OnEnter ||
		CommitMethod == ETextCommit::OnUserMovedFocus ||
		CommitMethod == ETextCommit::OnCleared)
	{
		if (APlayerController* PC = GetOwningPlayer())
		{
			FInputModeGameOnly InputMode;
			PC->SetInputMode(InputMode);
			PC->bShowMouseCursor = false;
			FSlateApplication::Get().SetAllUserFocusToGameViewport();
		}
	}
}
