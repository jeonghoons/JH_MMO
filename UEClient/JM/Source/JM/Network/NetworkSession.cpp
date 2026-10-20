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
	// 핸들러 안에서 Disconnect가 불려도 루프가 끝날 때까지 세션이 해제되지 않게 붙잡아 둔다.
	TSharedRef<NetworkSession> Self = AsShared();

	TArray<uint8> Packet;
	while (RecvPacketQueue.Dequeue(Packet))
	{
		FPacketHandler::ProcessPacket(Self, Packet.GetData(), Packet.Num());

		// 핸들러가 연결을 정리했으면 남은 패킷은 처리하지 않는다.
		if (Socket == nullptr)
			break;
	}
}

void NetworkSession::SendPacket(TSharedPtr<SendBuffer> sendBuffer)
{
	if (sendBuffer.IsValid() == false)
		return;

	SendPacketQueue.Enqueue(sendBuffer);
}

void NetworkSession::Disconnect()
{
	// 순서: 종료 요청 -> 소켓 Close(블로킹된 Recv/Send를 깨움) -> 스레드 종료 대기 -> 소켓 해제.
	// 스레드가 살아 있는 동안에는 워커 객체와 소켓을 해제하지 않는다.
	if (RecvWorkerThread.IsValid()) RecvWorkerThread->Stop();
	if (SendWorkerThread.IsValid()) SendWorkerThread->Stop();

	if (Socket)
	{
		Socket->Close();
	}

	if (RecvWorkerThread.IsValid()) RecvWorkerThread->Destroy();
	if (SendWorkerThread.IsValid()) SendWorkerThread->Destroy();

	RecvWorkerThread = nullptr;
	SendWorkerThread = nullptr;

	if (Socket)
	{
		ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->DestroySocket(Socket);
		Socket = nullptr;
	}

	RecvPacketQueue.Empty();
	SendPacketQueue.Empty();
}
