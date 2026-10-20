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

			// 플래그를 내린 뒤 큐를 다시 확인한다. 드레인과 플래그 해제 사이에 push된 잡은
			// RegisterJobs가 플래그가 아직 true라서 이벤트를 포스트하지 않으므로 여기서 이어서 처리한다.
			_isProcessing.store(false);
			if (_jobQueue.empty())
				break;

			bool expected = false;
			if (_isProcessing.compare_exchange_strong(expected, true) == false)
				break; // 다른 스레드가 이미 재등록했다.
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


