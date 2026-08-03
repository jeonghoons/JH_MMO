#include "NetworkManager.h"
#include "NetworkSession.h"
#include "Sockets.h"
#include "Common/TcpSocketBuilder.h"
#include "SocketSubsystem.h"

void UNetworkManager::Tick(float DeltaTime)
{
	if (Packet_Session == nullptr)
		return;

	Packet_Session->HandleRecvPackets();
}

void UNetworkManager::ConnectToServer()
{
	FSocket* socket = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->CreateSocket(TEXT("Stream"), TEXT("Client Socket"));

	FIPv4Address Ip;
	FIPv4Address::Parse(TEXT("127.0.0.1"), Ip);

	TSharedRef<FInternetAddr> internetAddr = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->CreateInternetAddr();
	internetAddr->SetIp(Ip.Value);
	internetAddr->SetPort(8888);

	bool Connected = socket->Connect(*internetAddr);
	if (Connected) 
	{
		GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Green, FString::Printf(TEXT("Connection Success")));

		Packet_Session = MakeShared<NetworkSession>(socket, this);
		Packet_Session->Run();
	}
	else 
	{
		GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Red, FString::Printf(TEXT("Connection Failed")));
	}
}

void UNetworkManager::DisconnectFromServer()
{
	if (Packet_Session == nullptr)
		return;
	
	Packet_Session->Disconnect();
	Packet_Session = nullptr;
}

void UNetworkManager::SendPacket(TSharedPtr<class SendBuffer> sendBuffer)
{
	if (Packet_Session == nullptr)
		return;

	Packet_Session->SendPacket(sendBuffer);
}

void UNetworkManager::EnterGame(FString MapName)
{
	
}


