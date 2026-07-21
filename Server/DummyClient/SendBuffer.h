#pragma once
#include "pch.h"

class SendBuffer
{
public:
	SendBuffer(int bufferSize);
	~SendBuffer();

	BYTE* Buffer() { return _buffer.data(); }
	int WritePos() { return _writePos; }
	int GetSize() const { return static_cast<int>(_buffer.size()); }

	void CopyData(void* data, int len);
	void Commit(int len);

private:
	std::vector<BYTE> _buffer;
	int		_writePos = 0;
};

