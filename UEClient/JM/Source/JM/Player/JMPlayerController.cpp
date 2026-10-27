#include "Player/JMPlayerController.h"
#include "Network/NetworkManager.h"
#include "UI/JMUIManager.h"
#include "Blueprint/UserWidget.h"
#include "Player/GameplayInputMode.h"
#include "UI/ChatWidget.h"

void AJMPlayerController::BeginPlay()
{
	Super::BeginPlay();
	ApplyCombatInputMode(this);

	// Crosshair at the screen center (aim reference). The widget class is assigned in BP_PControler.
	if (CrosshairWidgetClass)
	{
		CrosshairWidget = CreateWidget<UUserWidget>(this, CrosshairWidgetClass);
		if (CrosshairWidget)
		{
			CrosshairWidget->AddToViewport(100);
		}
	}

	if (UNetworkManager* NetManager = UNetworkManager::Get(this))
	{
		NetManager->SendEnterRoom();
	}

	UJMUIManager* UIManager = GetGameInstance()->GetSubsystem<UJMUIManager>();
	if (UIManager)
	{
		// 블루프린트에서 할당해 둔 클래스들을 이름표와 함께 던져줍니다.
		UIManager->RegisterWidget(this, FName("Chatting"), ChatWidgetClass, false);
		UIManager->RegisterWidget(this, FName("Inventory"), InventoryWidgetClass, true);
		UIManager->RegisterWidget(this, FName("MainInfo"), InfoWidgetClass, false);
		// 시작하자마자 띄워둘 UI는 한 번 켜줍니다.
		UIManager->ToggleWidget(FName("MainInfo"));
		UIManager->ToggleWidget(FName("Chatting"));
	}
}

void AJMPlayerController::OnInventoryKeyPressed()
{
	UJMUIManager* UIManager = GetGameInstance()->GetSubsystem<UJMUIManager>();
	if (UIManager)
	{
		UIManager->ToggleWidget(FName("Inventory"));
	}
}

void AJMPlayerController::OnChatKeyPressed()
{
	UJMUIManager* UIManager = GetGameInstance()->GetSubsystem<UJMUIManager>();
	if (UIManager)
	{
		// 매니저에서 채팅 위젯을 꺼내온 뒤, 우리가 만든 UCChatWidget으로 형변환(Cast)합니다.
		if (UChatWidget* ChatWidget = Cast<UChatWidget>(UIManager->GetWidget(FName("Chatting"))))
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