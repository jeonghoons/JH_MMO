#include "Player/JMPlayerController.h"
#include "Network/NetworkManager.h"
#include "UI/JMUIManager.h"
#include "UI/WidgetNames.h"
#include "Blueprint/UserWidget.h"
#include "Player/GameplayInputMode.h"
#include "UI/ChatWidget.h"
#include "Game/DataManager.h"

void AJMPlayerController::BeginPlay()
{
	Super::BeginPlay();
	ApplyCombatInputMode(this);

	UJMUIManager* UIManager = GetGameInstance()->GetSubsystem<UJMUIManager>();
	if (UIManager)
	{
		// 블루프린트에서 할당해 둔 클래스들을 이름표와 함께 던져줍니다. 시작부터 보일 UI는 bStartVisible = true.
		// 크로스헤어는 화면 중앙 조준 기준이라 다른 UI보다 위(Z-Order 100)에 둡니다.
		UIManager->RegisterWidget(this, WidgetNames::Crosshair, CrosshairWidgetClass, false, 100, true);
		UIManager->RegisterWidget(this, WidgetNames::Chatting, ChatWidgetClass, false, 0, true);

		// 내 캐릭터가 스폰되면 UJMObjectManager::HandleSpawn이 닫는다.
		UIManager->SetGameplayInput(true);
		UIManager->ShowSystemMessage(FText::FromString(TEXT("방에 입장하는 중...")), ESystemMessageType::Waiting);
	}

	// 영웅·스킬 에셋을 먼저 로드한 뒤 입장한다. 스폰된 캐릭터가 메시 없이(투명하게) 보이지 않게 하기 위함이다.
	TWeakObjectPtr<AJMPlayerController> WeakThis(this);
	auto SendEnterRoom = [WeakThis]()
	{
		if (UNetworkManager* NetManager = WeakThis.IsValid() ? UNetworkManager::Get(WeakThis.Get()) : nullptr)
		{
			NetManager->SendEnterRoom();
		}
	};

	if (UDataManager* DataManager = GetGameInstance()->GetSubsystem<UDataManager>())
	{
		DataManager->PreloadGameAssets(FSimpleDelegate::CreateLambda(SendEnterRoom));
	}
	else
	{
		SendEnterRoom();
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