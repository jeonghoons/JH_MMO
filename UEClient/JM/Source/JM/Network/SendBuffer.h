#pragma once
#include "CoreMinimal.h"

class SendBuffer : public TSharedFromThis<SendBuffer>
{
public:
	SendBuffer(int32 bufferSize);
	~SendBuffer();

	BYTE* Buffer() { return _buffer.GetData(); }
	int32 WritePos() { return _writePos; }
	int32 GetSize() const { return static_cast<int32>(_buffer.Num()); }

	void CopyData(void* data, int32 len);
	void Commit(uint32 writeSize);

private:
	TArray<BYTE>	_buffer;
	int32		_writePos = 0;

};
