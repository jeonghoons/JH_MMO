#include "SendBuffer.h"

SendBuffer::SendBuffer(int32 bufferSize)
{
	_buffer.SetNum(bufferSize);
}

SendBuffer::~SendBuffer()
{

}

void SendBuffer::CopyData(void* data, int32 len)
{
	memcpy(_buffer.GetData(), data, len);
	_writePos = len;
}

void SendBuffer::Commit(uint32 writeSize)
{
	_writePos = writeSize;
}