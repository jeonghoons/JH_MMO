#include "Player/JMPlayerController.h"
#include "Protocol/Protocol.pb.h"
#include "Network/SendBuffer.h"
#include "Network/NetworkManager.h"
#include "UI/JMUIManager.h"
#include "UI/ChatWidget.h"

void AJMPlayerController::BeginPlay()
{
	Super::BeginPlay();
	bShowMouseCursor = false;
	SetInputMode(FInputModeGameOnly());

	Protocol::CS_ENTER_ROOM_PACKET EnterPkt;
	TSharedPtr<SendBuffer> sendBuffer = SendBuffer::MakeSendBuffer(EnterPkt, Protocol::CS_ENTER_ROOM);
	UNetworkManager* NetManager = GetGameInstance()->GetSubsystem<UNetworkManager>();
	if (NetManager)
	{
		NetManager->SendPacket(sendBuffer);
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
