#include "Player/JMPlayerController.h"
#include "Protocol/Protocol.pb.h"
#include "Network/SendBuffer.h"
#include "Network/NetworkManager.h"

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
}
