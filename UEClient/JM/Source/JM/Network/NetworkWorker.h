#pragma once
#include "CoreMinimal.h"



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
	virtual void Exit() override;

	void Destroy() { Running = false; }

private:
	bool ReceivePacket(TArray<uint8>& OutPacket);
	bool ReceiveDesiredBytes(uint8* Results, int32 Size);


protected:
	FRunnableThread* Thread = nullptr;
	bool Running = true;
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
	virtual void Exit() override;

	bool SendPacket(TSharedPtr<SendBuffer> sendBuffer);

	void Destroy() { Running = false; }

private:

	bool SendDesiredBytes(uint8* buffer, int32 size);


protected:
	FRunnableThread* Thread = nullptr;
	bool Running = true;
	FSocket* Socket;
	TWeakPtr<NetworkSession> Session;
};
