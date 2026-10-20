#pragma once
#include "CoreMinimal.h"
#include <atomic>

class FSocket;
class NetworkSession;
class SendBuffer;

class JM_API RecvWorker : public FRunnable
{
public:
	RecvWorker(FSocket* Socket, TSharedPtr<NetworkSession> Session);
	~RecvWorker();

	virtual bool Init() override;
	virtual uint32 Run() override;
	virtual void Stop() override { Running = false; }
	virtual void Exit() override;

	// 종료를 요청하고 스레드가 끝날 때까지 기다린다. 소켓을 해제하기 전에 호출해야 한다.
	void Destroy();

private:
	bool ReceivePacket(TArray<uint8>& OutPacket);
	bool ReceiveDesiredBytes(uint8* Results, int32 Size);
	void NotifyConnectionLost();

protected:
	FRunnableThread* Thread = nullptr;
	std::atomic<bool> Running{ true };
	FSocket* Socket;
	TWeakPtr<NetworkSession> Session;
};

class JM_API SendWorker : public FRunnable
{
public:
	SendWorker(FSocket* Socket, TSharedPtr<NetworkSession> Session);
	~SendWorker();

	virtual bool Init() override;
	virtual uint32 Run() override;
	virtual void Stop() override { Running = false; }
	virtual void Exit() override;

	bool SendPacket(TSharedPtr<SendBuffer> sendBuffer);

	// 종료를 요청하고 스레드가 끝날 때까지 기다린다. 소켓을 해제하기 전에 호출해야 한다.
	void Destroy();

private:
	bool SendDesiredBytes(uint8* buffer, int32 size);
	void NotifyConnectionLost();

protected:
	FRunnableThread* Thread = nullptr;
	std::atomic<bool> Running{ true };
	FSocket* Socket;
	TWeakPtr<NetworkSession> Session;
};
