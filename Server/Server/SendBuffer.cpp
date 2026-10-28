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

void SendBuffer::Commit(int len)
{
	_writePos = len;
}
