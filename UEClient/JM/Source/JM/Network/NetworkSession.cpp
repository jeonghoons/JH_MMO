#include "NetworkSession.h"
#include "NetworkWorker.h"
#include "FPacketHandler.h"
#include "NetworkManager.h"
#include "Sockets.h"
#include "SocketSubsystem.h"

NetworkSession::NetworkSession(class FSocket* Socket, class UNetworkManager* OwnerNetwork) : Socket(Socket), OwnerNetwork(OwnerNetwork)
{
}

NetworkSession::~NetworkSession()
{
	Disconnect();
}

void NetworkSession::Run()
{
	RecvWorkerThread = MakeShared<RecvWorker>(Socket, AsShared());
	SendWorkerThread = MakeShared<SendWorker>(Socket, AsShared());
}


void NetworkSession::HandleRecvPackets()
{
	while (true)
	{
		TArray<uint8> Packet;
		if (RecvPacketQueue.Dequeue(Packet) == false)
			break;

		FPacketHandler::ProcessPacket(AsShared(), Packet.GetData(), Packet.Num());
	}
}

void NetworkSession::SendPacket(TSharedPtr<SendBuffer> sendBuffer)
{
	SendPacketQueue.Enqueue(sendBuffer);
}

void NetworkSession::Disconnect()
{
	if (RecvWorkerThread.IsValid()) RecvWorkerThread->Destroy();
	if (SendWorkerThread.IsValid()) SendWorkerThread->Destroy();

	RecvWorkerThread = nullptr;
	SendWorkerThread = nullptr;

	if (Socket)
	{
		Socket->Close();
		ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->DestroySocket(Socket);
		Socket = nullptr;
	}
}