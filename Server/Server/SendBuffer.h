#pragma once
#include <concurrent_queue.h>

class SendBuffer
{
public:
	SendBuffer(int bufferSize);
	~SendBuffer();

	BYTE* Buffer() { return _buffer.data(); }
	int WritePos() { return _writePos; }
	int Capacity() { return static_cast<int>(_buffer.size()); }

	void CopyData(void* data, int len);

private:
	vector<BYTE> _buffer;
	int		_writePos{};
};

class SendBufferQueue
{
public:
	void Push(shared_ptr<SendBuffer> sendBuffer)
	{
		_queue.push(sendBuffer);
	}
	
	bool Try_pop(shared_ptr<SendBuffer>& buffer)
	{
		return _queue.try_pop(buffer);
	}

	bool isEmpty() { return _queue.empty(); }
	
public:
	
	concurrency::concurrent_queue<shared_ptr<SendBuffer>> _queue;
};

