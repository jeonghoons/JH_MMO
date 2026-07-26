#pragma once
#include "CoreMinimal.h"

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

public:
	FSocket* Socket;

	TSharedPtr<class RecvWorker> RecvWorkerThread;
	TSharedPtr<class SendWorker> SendWorkerThread;

	TQueue<TArray<uint8>> RecvPacketQueue;
	TQueue<TSharedPtr<SendBuffer>> SendPacketQueue;

	TWeakObjectPtr<UNetworkManager> OwnerNetwork;
};
