#pragma once
#include "CoreMinimal.h"

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

	void CopyData(void* data, int32 len);
	void Commit(uint32 writeSize);


	template<typename T>
	static TSharedPtr<SendBuffer> MakeSendBuffer(const T& pkt, uint16 pktId)
	{
		const uint16 dataSize = static_cast<uint16>(pkt.ByteSizeLong());
		const uint16 packetSize = dataSize + sizeof(FPacketHeader);

		TSharedPtr<SendBuffer> sendBuffer = MakeShared<SendBuffer>(packetSize);

		FPacketHeader* header = reinterpret_cast<FPacketHeader*>(sendBuffer->Buffer());
		header->PacketSize = packetSize;
		header->PacketId = pktId;

		pkt.SerializeToArray(sendBuffer->Buffer() + sizeof(FPacketHeader), dataSize);
		sendBuffer->Commit(packetSize);

		return sendBuffer;
	}

private:
	TArray<BYTE>	_buffer;
	int32		_writePos = 0;

};
