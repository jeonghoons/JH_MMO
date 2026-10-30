#pragma once
#include <condition_variable>

template <typename T>
class ConcurrentPQ {
public:
	void Push(T item)
	{
		lock_guard<std::mutex> lock(_mutex);
		_queue.push(std::move(item));
		_cv.notify_one();
	}

	bool Pop(T& value)
	{
		unique_lock<mutex> lock(_mutex);

		if (_queue.empty()) {
			return false;
		}

		value = std::move(const_cast<T&>(_queue.top()));
		_queue.pop();

		return true;
	}

	bool Peek(T& value) const
	{
		lock_guard<mutex> lock(_mutex);

		if (_queue.empty()) {
			return false;
		}

		value = _queue.top();

		return true;
	}

	bool Is_empty() const {
		return _queue.empty();
	}

	mutex& GetMutex() { return _mutex; }
	condition_variable& GetCV() { return _cv; }
	priority_queue<T>& GetQueueUnsafe() { return _queue; }

private:
	priority_queue<T> _queue;
	mutable mutex _mutex;
	condition_variable _cv;
};

template <typename T>
class ConcurrentQ
{
public:
	void Push(T item)
	{
		{
			lock_guard<std::mutex> lock(_mutex);
			_queue.push(std::move(item));
		}
		_cv.notify_one(); 
	}

	
	void WaitPop(T& value)
	{
		unique_lock<mutex> lock(_mutex);


		_cv.wait(lock, [this] { return !_queue.empty(); });

		value = std::move(_queue.front()); 
		_queue.pop();
	}

	bool Is_empty() const {
		lock_guard<mutex> lock(_mutex);
		return _queue.empty();
	}

private:
	queue<T> _queue;
	mutable mutex _mutex;
	condition_variable _cv;
};

