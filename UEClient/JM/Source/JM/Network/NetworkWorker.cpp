#include "NetworkWorker.h"
#include "NetworkSession.h"
#include "Sockets.h"
#include "SendBuffer.h"

RecvWorker::RecvWorker(FSocket* Socket, TSharedPtr<NetworkSession> Session) : Socket(Socket), Session(Session)
{
	Thread = FRunnableThread::Create(this, TEXT("RecvWorker Thread"));
}

RecvWorker::~RecvWorker()
{
}

bool RecvWorker::Init()
{
	return true;
}

uint32 RecvWorker::Run()
{
	while (Running)
	{
		TArray<uint8> Packet;

		if (ReceivePacket(Packet))
		{
			if (TSharedPtr<NetworkSession> session = Session.Pin())
			{
				session->RecvPacketQueue.Enqueue(MoveTemp(Packet));
			}
		}
		else
		{
			FPlatformProcess::Sleep(0.001f);
		}
	}

	return 0;
}

void RecvWorker::Exit()
{
}

bool RecvWorker::ReceivePacket(TArray<uint8>& OutPacket)
{
	const int32 HeaderSize = sizeof(FPacketHeader);
	TArray<uint8> HeaderBuffer;
	HeaderBuffer.AddZeroed(HeaderSize);

	if (ReceiveDesiredBytes(HeaderBuffer.GetData(), HeaderSize) == false)
		return false;

	FPacketHeader Header;
	{
		FMemoryReader Reader(HeaderBuffer);
		Reader << Header;
		UE_LOG(LogTemp, Log, TEXT("Recv PacketID : %d, PacketSize : %d"), Header.PacketId, Header.PacketSize);
	}

	OutPacket = HeaderBuffer;

	TArray<uint8> PayloadBuffer;
	const int32 PayloadSize = Header.PacketSize - HeaderSize;
	if (PayloadSize == 0)
		return true;
	OutPacket.AddZeroed(PayloadSize);

	if (ReceiveDesiredBytes(&OutPacket[HeaderSize], PayloadSize))
		return true;

	return false;
}

bool RecvWorker::ReceiveDesiredBytes(uint8* Results, int32 Size)
{
	uint32 PendingDataSize;
	if (Socket->HasPendingData(PendingDataSize) == false || PendingDataSize <= 0)
		return false;

	int32 Offset = 0;

	while (Size > 0)
	{
		int32 NumRead = 0;
		Socket->Recv(Results + Offset, Size, NumRead);
		check(NumRead <= Size);

		if (NumRead <= 0)
			return false;

		Offset += NumRead;
		Size -= NumRead;
	}

	return true;
}

SendWorker::SendWorker(FSocket* Socket, TSharedPtr<class NetworkSession> Session) : Socket(Socket), Session(Session)
{
	Thread = FRunnableThread::Create(this, TEXT("SendWorker Thread"));
}

SendWorker::~SendWorker()
{
}

bool SendWorker::Init()
{
	// GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Red, FString::Printf(TEXT("Recv Thread Init")));

	return true;
}

uint32 SendWorker::Run()
{
	while (Running)
	{
		TSharedPtr<SendBuffer> sendBuffer;
		bool bSent = false;

		if (TSharedPtr<NetworkSession> SessionPtr = Session.Pin())
		{
			if (SessionPtr->SendPacketQueue.Dequeue(sendBuffer))
			{
				SendPacket(sendBuffer);
				bSent = true;
			}
		}

		if (!bSent)
		{
			FPlatformProcess::Sleep(0.001f);
		}
	}

	return 0;
}

void SendWorker::Exit()
{
}

bool SendWorker::SendPacket(TSharedPtr<SendBuffer> sendBuffer)
{
	if (SendDesiredBytes(sendBuffer->Buffer(), sendBuffer->WritePos()) == false)
		return false;

	return true;
}

bool SendWorker::SendDesiredBytes(uint8* buffer, int32 size)
{
	while (size > 0)
	{
		int32 bytesSent = 0;
		if (Socket->Send(buffer, size, bytesSent) == false) {
			return false;
		}

		UE_LOG(LogTemp, Log, TEXT("Send %d Bytes"), bytesSent);

		size -= bytesSent;
		buffer += bytesSent;
	}

	return true;
}