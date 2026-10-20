#include "NetworkManager.h"
#include "NetworkSession.h"
#include "Sockets.h"
#include "Common/TcpSocketBuilder.h"
#include "SocketSubsystem.h"
#include "Kismet/GameplayStatics.h"
#include "FPacketHandler.h"
#include "Game/JMObjectManager.h"
#include "SendBuffer.h"

void UNetworkManager::Deinitialize()
{
	// PIE 종료/게임 종료 시 스레드와 소켓이 남지 않게 정리한다.
	DisconnectFromServer();

	Super::Deinitialize();
}

void UNetworkManager::Tick(float DeltaTime)
{
	// 핸들러가 Packet_Session을 비워도 이번 Tick 동안은 세션이 살아 있게 복사본을 쓴다.
	TSharedPtr<NetworkSession> Session = Packet_Session;
	if (Session == nullptr)
		return;

	Session->HandleRecvPackets();

	// 워커가 연결 끊김을 알렸으면 받아 둔 패킷을 처리한 뒤 세션을 정리한다. 끊긴 뒤의 화면 처리는 추후 구현.
	if (Session->IsConnectionLost() && Packet_Session == Session)
	{
		UE_LOG(LogTemp, Warning, TEXT("Connection to server lost"));
		DisconnectFromServer();
	}
}

void UNetworkManager::ConnectToServer()
{
	if (Packet_Session != nullptr)
	{
		if (Packet_Session->IsConnectionLost() == false)
		{
			UE_LOG(LogTemp, Warning, TEXT("ConnectToServer: already connected"));
			return;
		}
		DisconnectFromServer();
	}

	FSocket* socket = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->CreateSocket(TEXT("Stream"), TEXT("Client Socket"));

	FIPv4Address Ip;
	FIPv4Address::Parse(TEXT("127.0.0.1"), Ip);

	TSharedRef<FInternetAddr> internetAddr = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->CreateInternetAddr();
	internetAddr->SetIp(Ip.Value);
	internetAddr->SetPort(8888);

	bool Connected = socket->Connect(*internetAddr);
	if (Connected)
	{
		if (GEngine) GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Green, FString::Printf(TEXT("Connection Success")));

		Packet_Session = MakeShared<NetworkSession>(socket, this);
		FPacketHandler::Init();
		Packet_Session->Run();
	}
	else
	{
		if (GEngine) GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Red, FString::Printf(TEXT("Connection Failed")));

		// 연결에 실패한 소켓은 세션이 소유하지 않으므로 여기서 해제한다.
		socket->Close();
		ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->DestroySocket(socket);
	}
}

void UNetworkManager::DisconnectFromServer()
{
	if (Packet_Session == nullptr)
		return;

	// 워커 스레드를 종료하고 소켓을 해제한 뒤 세션을 놓는다.
	Packet_Session->Disconnect();
	Packet_Session = nullptr;
	NetworkId = -1;
}

void UNetworkManager::SendPacket(TSharedPtr<class SendBuffer> sendBuffer)
{
	if (Packet_Session == nullptr || sendBuffer.IsValid() == false)
		return;

	Packet_Session->SendPacket(sendBuffer);
}

void UNetworkManager::EnterGame()
{
	SwitchGameMapLevel(FName("ParagonSample"));
}

void UNetworkManager::SwitchGameMapLevel(FName LevelToLoad)
{
	if (UJMObjectManager* ObjectManager = GetGameInstance()->GetSubsystem<UJMObjectManager>())
	{
		ObjectManager->ClearObjects();
	}

	UWorld* World = GetWorld();
	if (World == nullptr) return;
	UGameplayStatics::OpenLevel(World, LevelToLoad);
}

void UNetworkManager::SendAttackPacket()
{
	if (Packet_Session == nullptr) return;

	Protocol::CS_ATTACK_PACKET attackPkt;
	TSharedPtr<SendBuffer> sendBuffer = SendBuffer::MakeSendBuffer(attackPkt, Protocol::CS_ATTACK);
	SendPacket(sendBuffer);
}



