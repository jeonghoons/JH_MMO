#include "pch.h"
#include "RecvBuffer.h"

RecvBuffer::RecvBuffer(int bufferSize) : _bufferSize(bufferSize)
{
	_buffer.resize(_bufferSize);
}

RecvBuffer::~RecvBuffer()
{
	_buffer.clear();
}

bool RecvBuffer::OnRead(int numOfBytes)
{
	if (numOfBytes > DataSize())
		return false;

	_readPos += numOfBytes;

	return true;
}

bool RecvBuffer::OnWrite(int numOfBytes)
{
	if (numOfBytes > FreeSize())
		return false;

	_writePos += numOfBytes;

	return true;
}

void RecvBuffer::CleanCheck()
{
	if (DataSize() == 0) {
		_readPos = _writePos = 0;
	}
	else {
		memcpy(&_buffer[0], &_buffer[_readPos], DataSize());
		_readPos = 0;
		_writePos = DataSize();
	}
}


