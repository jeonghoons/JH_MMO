#include "NetworkSession.h"
#include "NetworkWorker.h"
#include "FPacketHandler.h"
#include "NetworkManager.h"
#include "Sockets.h"
#include "SocketSubsystem.h"
#include "HAL/IConsoleManager.h"

namespace
{
#if !UE_BUILD_SHIPPING
	// 개발용 지연(D-214): 같은 PC에서는 왕복 지연이 거의 0이라 선반영·되돌리기를 확인할 수 없어서 둔다.
	TAutoConsoleVariable<int32> CVarNetLagMs(TEXT("jm.NetLagMs"), 0, TEXT("Dev: simulated round-trip network lag in ms (half on send, half on receive). 0 = off"));
	TAutoConsoleVariable<int32> CVarNetLagJitterMs(TEXT("jm.NetLagJitterMs"), 0, TEXT("Dev: random +/- jitter in ms added to each delayed packet (order is kept)"));
#endif

	// 이 패킷을 늦출 시간(초). 지연이 꺼져 있으면 0
	double NextLagDelaySec()
	{
#if !UE_BUILD_SHIPPING
		const int32 LagMs = CVarNetLagMs.GetValueOnGameThread();
		const int32 JitterMs = CVarNetLagJitterMs.GetValueOnGameThread();
		if (LagMs <= 0 && JitterMs <= 0) return 0.0;
		const double Ms = LagMs * 0.5 + (JitterMs > 0 ? FMath::FRandRange(-static_cast<float>(JitterMs), static_cast<float>(JitterMs)) : 0.f);
		return FMath::Max(Ms, 0.0) / 1000.0;
#else
		return 0.0;
#endif
	}
}

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
		// 개발용 지연: 늦출 패킷이 있거나 이미 늦추는 중이면(순서 유지) 대기열 뒤에 둔다.
		const double Delay = NextLagDelaySec();
		if (Delay > 0.0 || DelayedRecvs.Num() > 0)
		{
			LastRecvDue = FMath::Max(LastRecvDue, FPlatformTime::Seconds() + Delay);
			DelayedRecvs.Emplace(LastRecvDue, MoveTemp(Packet));
			continue;
		}

		FPacketHandler::ProcessPacket(Self, Packet.GetData(), Packet.Num());

		// 핸들러가 연결을 정리했으면 남은 패킷은 처리하지 않는다.
		if (Socket == nullptr)
			return;
	}

	const double Now = FPlatformTime::Seconds();
	int32 Done = 0;
	while (Done < DelayedRecvs.Num() && DelayedRecvs[Done].Key <= Now)
	{
		TArray<uint8> Due = MoveTemp(DelayedRecvs[Done].Value);
		++Done;
		FPacketHandler::ProcessPacket(Self, Due.GetData(), Due.Num());
		if (Socket == nullptr)
		{
			DelayedRecvs.Empty();
			return;
		}
	}
	if (Done > 0) DelayedRecvs.RemoveAt(0, Done);
}

void NetworkSession::FlushDelayedSends()
{
	const double Now = FPlatformTime::Seconds();
	int32 Done = 0;
	while (Done < DelayedSends.Num() && DelayedSends[Done].Key <= Now)
	{
		SendPacketQueue.Enqueue(DelayedSends[Done].Value);
		++Done;
	}
	if (Done > 0) DelayedSends.RemoveAt(0, Done);
}

void NetworkSession::SendPacket(TSharedPtr<SendBuffer> sendBuffer)
{
	if (sendBuffer.IsValid() == false)
		return;

	// 개발용 지연: 늦출 패킷이 있거나 이미 늦추는 중이면(순서 유지) 대기열에 두고 UNetworkManager::Tick이 내보낸다.
	const double Delay = IsInGameThread() ? NextLagDelaySec() : 0.0;
	if (Delay > 0.0 || (IsInGameThread() && DelayedSends.Num() > 0))
	{
		LastSendDue = FMath::Max(LastSendDue, FPlatformTime::Seconds() + Delay);
		DelayedSends.Emplace(LastSendDue, sendBuffer);
		return;
	}

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
	DelayedSends.Empty();
	DelayedRecvs.Empty();
}
