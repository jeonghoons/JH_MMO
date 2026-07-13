#pragma once
#include "Job.h"
#include "IocpEvent.h"
#include <concurrent_queue.h>

class JobQueue : public IocpObject
{
public:
	JobQueue(HANDLE iocpHandle) : _iocpHandle(iocpHandle) {}

	virtual HANDLE GetHandle() override;
	virtual void Dispatch(class IocpEvent* iocpEvent, int numBytes = 0) override;

	void Push(shared_ptr<Job> job)
	{
		_jobQueue.push(job);
		RegisterJobs();
	}

	template<typename T, typename... Arguments>
	void Push(shared_ptr<T> owner, void(T::* memFunc)(Arguments...), Arguments&&... args)
	{
		shared_ptr<Job> job = make_shared<Job>(owner, memFunc, std::forward<Arguments>(args)...);
		_jobQueue.push(job);
		RegisterJobs();
	}	

	void ExecuteJobs()
	{
		while (true) {
			shared_ptr<Job> job;
			while (_jobQueue.try_pop(job)) {
				job->Execute();
			}

			bool expected = true;
			if (_isProcessing.compare_exchange_strong(expected, false))
				break;
		}
	}

private:
	
	void RegisterJobs()
	{
		bool expected = false;
		if (_isProcessing.compare_exchange_strong(expected, true)){
			_jobEvent.Init();
			_jobEvent._owner = shared_from_this();
			PostQueuedCompletionStatus(_iocpHandle, 0, 0, &_jobEvent);
		}
				
	}

private:
	HANDLE						_iocpHandle;
	JobEvent						_jobEvent;
	concurrency::concurrent_queue<shared_ptr<Job>> _jobQueue;
	atomic<bool> _isProcessing = false;
};


