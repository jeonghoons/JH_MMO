#pragma once
#include "pch.h"

class RecvBuffer
{
public:
	RecvBuffer() = default;
	RecvBuffer(int bufferSize);
	~RecvBuffer();
	
	BYTE* ReadPos() { return &_buffer[_readPos]; }
	BYTE* WritePos() { return &_buffer[_writePos]; }
	int BufferSize() { return static_cast<int>(_buffer.size()); }
	int DataSize() { return _writePos - _readPos; }
	int FreeSize() { return _bufferSize - _writePos; }

	bool OnRead(int numOfBytes);
	bool OnWrite(int numOfBytes);
	void CleanCheck();
	
private:
	int		_bufferSize;
	int		_readPos = 0;
	int		_writePos = 0;
	vector<BYTE>	 _buffer;
};