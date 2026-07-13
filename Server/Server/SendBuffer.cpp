#include "pch.h"
#include "SendBuffer.h"

SendBuffer::SendBuffer(int bufferSize)
{
	_buffer.resize(bufferSize);
}

SendBuffer::~SendBuffer()
{
	_buffer.clear();
}

void SendBuffer::CopyData(void* data, int len)
{
	memcpy(_buffer.data(), data, len);
	_writePos = len;
}
