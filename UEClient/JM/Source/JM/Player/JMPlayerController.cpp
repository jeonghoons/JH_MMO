#include "Player/JMPlayerController.h"
#include "Network/NetworkManager.h"
#include "UI/JMUIManager.h"
#include "UI/WidgetNames.h"
#include "Blueprint/UserWidget.h"
#include "Player/GameplayInputMode.h"
#include "UI/ChatWidget.h"

void AJMPlayerController::BeginPlay()
{
	Super::BeginPlay();
	ApplyCombatInputMode(this);

	if (UNetworkManager* NetManager = UNetworkManager::Get(this))
	{
		NetManager->SendEnterRoom();
	}

	UJMUIManager* UIManager = GetGameInstance()->GetSubsystem<UJMUIManager>();
	if (UIManager)
	{
		// 블루프린트에서 할당해 둔 클래스들을 이름표와 함께 던져줍니다. 시작부터 보일 UI는 bStartVisible = true.
		// 크로스헤어는 화면 중앙 조준 기준이라 다른 UI보다 위(Z-Order 100)에 둡니다.
		UIManager->RegisterWidget(this, WidgetNames::Crosshair, CrosshairWidgetClass, false, 100, true);
		UIManager->RegisterWidget(this, WidgetNames::Chatting, ChatWidgetClass, false, 0, true);
	}
}

void AJMPlayerController::OnChatKeyPressed()
{
	UJMUIManager* UIManager = GetGameInstance()->GetSubsystem<UJMUIManager>();
	if (UIManager)
	{
		// 매니저에서 채팅 위젯을 꺼내온 뒤, 우리가 만든 UCChatWidget으로 형변환(Cast)합니다.
		if (UChatWidget* ChatWidget = Cast<UChatWidget>(UIManager->GetWidget(WidgetNames::Chatting)))
		{
			// 포커스를 주는 함수를 실행합니다.
			ChatWidget->FocusChatInput(this);
		}
	}
}

void AJMPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();

	if (InputComponent)
	{
		InputComponent->BindKey(EKeys::LeftAlt, IE_Pressed, this, &AJMPlayerController::OnFreeCursorPressed);
		InputComponent->BindKey(EKeys::LeftAlt, IE_Released, this, &AJMPlayerController::OnFreeCursorReleased);
	}
}

void AJMPlayerController::OnFreeCursorPressed()
{
	ApplyFreeCursorInputMode(this);
}

void AJMPlayerController::OnFreeCursorReleased()
{
	// If a window (inventory etc.) is open the UI manager keeps the cursor; otherwise it returns to combat mode.
	if (UJMUIManager* UIManager = GetGameInstance()->GetSubsystem<UJMUIManager>())
	{
		UIManager->UpdateInputMode();
	}
	else
	{
		ApplyCombatInputMode(this);
	}
}