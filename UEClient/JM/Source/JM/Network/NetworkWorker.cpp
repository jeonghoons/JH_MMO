#include "NetworkWorker.h"
#include "NetworkSession.h"
#include "Sockets.h"
#include "SendBuffer.h"
#include "Protocol/Protocol.pb.h"

namespace
{
	// 소켓 대기 한 번의 최대 시간. Running 플래그를 이 주기로 확인한다.
	const FTimespan SocketWaitTime = FTimespan::FromMilliseconds(100);

	// 스레드 자신이 Destroy를 부르면 자기 자신을 기다리게 되므로 Kill을 건너뛴다.
	void StopAndJoin(FRunnableThread*& Thread)
	{
		if (Thread == nullptr)
			return;

		if (Thread->GetThreadID() != FPlatformTLS::GetCurrentThreadId())
		{
			Thread->Kill(true);
			delete Thread;
		}
		Thread = nullptr;
	}
}

RecvWorker::RecvWorker(FSocket* Socket, TSharedPtr<NetworkSession> Session) : Socket(Socket), Session(Session)
{
	Thread = FRunnableThread::Create(this, TEXT("RecvWorker Thread"));
}

RecvWorker::~RecvWorker()
{
	Destroy();
}

bool RecvWorker::Init()
{
	return true;
}

uint32 RecvWorker::Run()
{
	while (Running)
	{
		// 읽을 데이터가 올 때까지 기다린다. 타임아웃이면 연결 오류만 확인하고 다시 기다린다.
		if (Socket->Wait(ESocketWaitConditions::WaitForRead, SocketWaitTime) == false)
		{
			if (Socket->GetConnectionState() == SCS_ConnectionError)
			{
				NotifyConnectionLost();
				break;
			}
			continue;
		}

		TArray<uint8> Packet;
		if (ReceivePacket(Packet) == false)
		{
			// Destroy로 멈춘 경우가 아니면 연결이 끊긴 것이다.
			if (Running)
			{
				NotifyConnectionLost();
			}
			break;
		}

		if (TSharedPtr<NetworkSession> session = Session.Pin())
		{
			session->RecvPacketQueue.Enqueue(MoveTemp(Packet));
		}
	}

	return 0;
}

void RecvWorker::Exit()
{
}

void RecvWorker::Destroy()
{
	Running = false;
	StopAndJoin(Thread);
}

void RecvWorker::NotifyConnectionLost()
{
	Running = false;
	if (TSharedPtr<NetworkSession> session = Session.Pin())
	{
		session->MarkConnectionLost();
	}
}

bool RecvWorker::ReceivePacket(TArray<uint8>& OutPacket)
{
	const int32 HeaderSize = sizeof(FPacketHeader);
	OutPacket.SetNumUninitialized(HeaderSize);

	if (ReceiveDesiredBytes(OutPacket.GetData(), HeaderSize) == false)
		return false;

	FPacketHeader Header;
	{
		FMemoryReader Reader(OutPacket);
		Reader << Header;
		UE_LOG(LogTemp, Verbose, TEXT("Recv PacketID : %d, PacketSize : %d"), Header.PacketId, Header.PacketSize);
	}

	// 서버와 같은 기준(헤더 이상, 최대 MaxPacketSize)을 벗어나면 스트림이 깨진 것으로 보고 연결을 끊는다.
	if (Header.PacketSize < HeaderSize || Header.PacketSize > MaxPacketSize)
	{
		UE_LOG(LogTemp, Error, TEXT("Invalid PacketSize : %d (PacketId : %d)"), Header.PacketSize, Header.PacketId);
		return false;
	}

	const int32 PayloadSize = Header.PacketSize - HeaderSize;
	if (PayloadSize == 0)
		return true;

	OutPacket.AddUninitialized(PayloadSize);
	return ReceiveDesiredBytes(OutPacket.GetData() + HeaderSize, PayloadSize);
}

bool RecvWorker::ReceiveDesiredBytes(uint8* Results, int32 Size)
{
	// 요청한 바이트를 다 채울 때까지 읽는다. 중간에 데이터가 끊겨도 이미 읽은 부분을 버리지 않는다.
	while (Size > 0 && Running)
	{
		if (Socket->Wait(ESocketWaitConditions::WaitForRead, SocketWaitTime) == false)
		{
			if (Socket->GetConnectionState() == SCS_ConnectionError)
				return false;
			continue;
		}

		int32 NumRead = 0;
		// 오류이거나 0바이트면 상대가 연결을 닫은 것이다.
		if (Socket->Recv(Results, Size, NumRead) == false || NumRead <= 0)
			return false;

		check(NumRead <= Size);
		Results += NumRead;
		Size -= NumRead;
	}

	return Size == 0;
}

SendWorker::SendWorker(FSocket* Socket, TSharedPtr<class NetworkSession> Session) : Socket(Socket), Session(Session)
{
	// 스레드를 만들기 전에 정해 둔다(스레드가 읽기만 함).
	HeartbeatIntervalSec = Session.IsValid() ? Session->HeartbeatIntervalSec : 0.f;
	Thread = FRunnableThread::Create(this, TEXT("SendWorker Thread"));
}

SendWorker::~SendWorker()
{
	Destroy();
}

bool SendWorker::Init()
{
	return true;
}

uint32 SendWorker::Run()
{
	// 하트비트(A5.0, D-220 → DG3c-2에서 게임 스레드 Tick에서 이 스레드로 옮김): 게임 스레드가 맵을 읽느라 멈춰도(레벨 전환)
	// 서버의 유휴 타임아웃에 걸리지 않게 여기서 보낸다. 송신 큐는 생산자 하나(게임 스레드)·소비자 하나(이 스레드) 구조라
	// 큐에 넣지 않고 패킷 사이에 직접 보낸다(패킷 하나를 끝까지 보낸 뒤에만 끼어들므로 스트림이 섞이지 않는다).
	double LastHeartbeatSec = FPlatformTime::Seconds();

	while (Running)
	{
		TSharedPtr<SendBuffer> sendBuffer;
		bool bSent = false;

		if (HeartbeatIntervalSec > 0.f)
		{
			const double Now = FPlatformTime::Seconds();
			if (Now - LastHeartbeatSec >= HeartbeatIntervalSec)
			{
				LastHeartbeatSec = Now;
				Protocol::CS_PING_PACKET Ping;
				Ping.set_client_time(FDateTime::UtcNow().ToUnixTimestamp() * 1000);
				if (SendPacket(SendBuffer::MakeSendBuffer(Ping, Protocol::CS_PING)) == false)
				{
					if (Running)
					{
						NotifyConnectionLost();
					}
					break;
				}
			}
		}

		if (TSharedPtr<NetworkSession> SessionPtr = Session.Pin())
		{
			if (SessionPtr->SendPacketQueue.Dequeue(sendBuffer))
			{
				if (SendPacket(sendBuffer) == false)
				{
					if (Running)
					{
						NotifyConnectionLost();
					}
					break;
				}
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

void SendWorker::Destroy()
{
	Running = false;
	StopAndJoin(Thread);
}

void SendWorker::NotifyConnectionLost()
{
	Running = false;
	if (TSharedPtr<NetworkSession> session = Session.Pin())
	{
		session->MarkConnectionLost();
	}
}

bool SendWorker::SendPacket(TSharedPtr<SendBuffer> sendBuffer)
{
	if (sendBuffer.IsValid() == false)
		return true;

	return SendDesiredBytes(sendBuffer->Buffer(), sendBuffer->WritePos());
}

bool SendWorker::SendDesiredBytes(uint8* buffer, int32 size)
{
	while (size > 0)
	{
		int32 bytesSent = 0;
		if (Socket->Send(buffer, size, bytesSent) == false || bytesSent <= 0) {
			return false;
		}

		UE_LOG(LogTemp, Verbose, TEXT("Send %d Bytes"), bytesSent);

		size -= bytesSent;
		buffer += bytesSent;
	}

	return true;
}
