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
};
