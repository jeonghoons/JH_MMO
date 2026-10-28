#include "SendBuffer.h"

SendBuffer::SendBuffer(int32 bufferSize)
{
	_buffer.SetNum(bufferSize);
}

SendBuffer::~SendBuffer()
{

}

void SendBuffer::Commit(uint32 writeSize)
{
	_writePos = writeSize;
}