#pragma once
#include "CoreMinimal.h"
#include <atomic>

class FSocket;
class SendBuffer;
class UNetworkManager;

class JM_API NetworkSession : public TSharedFromThis<NetworkSession>
{
public:
	NetworkSession(FSocket* Socket, UNetworkManager* ownerNetwork);
	~NetworkSession();

	void Run();
	void HandleRecvPackets();
	void SendPacket(TSharedPtr<SendBuffer> sendBuffer);
	// 개발용 지연(D-214, 출시 빌드 제외): 콘솔 변수 jm.NetLagMs(왕복 지연)·jm.NetLagJitterMs(패킷마다 ± 흔들림)가 0보다 크면
	// 보내는 패킷과 받은 패킷을 각각 절반씩 늦춘다(순서는 유지). 게임 스레드에서만 부른다. 지연이 0이면 아무 일도 하지 않는다.
	void FlushDelayedSends();
	void Disconnect();

	// 워커 스레드가 연결 끊김을 감지하면 호출한다. 정리는 게임 스레드(UNetworkManager::Tick)가 한다.
	void MarkConnectionLost() { bConnectionLost = true; }
	bool IsConnectionLost() const { return bConnectionLost; }

public:
	FSocket* Socket;

	TSharedPtr<class RecvWorker> RecvWorkerThread;
	TSharedPtr<class SendWorker> SendWorkerThread;

	TQueue<TArray<uint8>> RecvPacketQueue;
	TQueue<TSharedPtr<SendBuffer>> SendPacketQueue;

	TWeakObjectPtr<UNetworkManager> OwnerNetwork;

private:
	std::atomic<bool> bConnectionLost{ false };

	// 지연 중인 패킷(게임 스레드 전용). Key = 내보낼 시각(FPlatformTime::Seconds)
	TArray<TPair<double, TSharedPtr<SendBuffer>>> DelayedSends;
	TArray<TPair<double, TArray<uint8>>> DelayedRecvs;
	double LastSendDue = 0.0;
	double LastRecvDue = 0.0;
};
