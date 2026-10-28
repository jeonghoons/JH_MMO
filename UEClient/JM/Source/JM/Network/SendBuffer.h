#pragma once
#include "CoreMinimal.h"

// 서버와 같은 패킷 최대 크기(헤더 포함). 이 값을 넘는 패킷은 보내지도 받지도 않는다.
constexpr int32 MaxPacketSize = 8192;

struct FPacketHeader
{
	FPacketHeader() : PacketSize(0), PacketId(0) {}

	FPacketHeader(uint16 PacketSize, uint16 PacketId) : PacketSize(PacketSize), PacketId(PacketId) {}

	friend FArchive& operator<<(FArchive& Ar, FPacketHeader& Header)
	{
		Ar << Header.PacketSize;
		Ar << Header.PacketId;
		return Ar;
	}

	uint16 PacketSize;
	uint16 PacketId;
};

class SendBuffer : public TSharedFromThis<SendBuffer>
{
public:
	SendBuffer(int32 bufferSize);
	~SendBuffer();

public:
	BYTE* Buffer() { return _buffer.GetData(); }
	int32 WritePos() { return _writePos; }
	int32 GetSize() const { return static_cast<int32>(_buffer.Num()); }

	void Commit(uint32 writeSize);


	template<typename T>
	static TSharedPtr<SendBuffer> MakeSendBuffer(const T& pkt, uint16 pktId)
	{
		const size_t dataSize = pkt.ByteSizeLong();
		const size_t packetSize = dataSize + sizeof(FPacketHeader);

		// uint16으로 잘리거나 서버 한도를 넘는 패킷은 만들지 않는다. 호출하는 쪽은 nullptr을 확인해야 한다.
		if (packetSize > static_cast<size_t>(MaxPacketSize))
		{
			UE_LOG(LogTemp, Error, TEXT("Packet too large : %d (PacketId : %d)"), static_cast<int32>(packetSize), pktId);
			return nullptr;
		}

		TSharedPtr<SendBuffer> sendBuffer = MakeShared<SendBuffer>(static_cast<int32>(packetSize));

		FPacketHeader* header = reinterpret_cast<FPacketHeader*>(sendBuffer->Buffer());
		header->PacketSize = static_cast<uint16>(packetSize);
		header->PacketId = pktId;

		if (pkt.SerializeToArray(sendBuffer->Buffer() + sizeof(FPacketHeader), static_cast<int>(dataSize)) == false)
		{
			UE_LOG(LogTemp, Error, TEXT("Packet serialize failed (PacketId : %d)"), pktId);
			return nullptr;
		}
		sendBuffer->Commit(static_cast<uint32>(packetSize));

		return sendBuffer;
	}

private:
	TArray<BYTE>	_buffer;
	int32		_writePos = 0;

};
