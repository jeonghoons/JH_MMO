#include "pch.h"
#include "JobQueue.h"

HANDLE JobQueue::GetHandle()
{
	return HANDLE();
}

void JobQueue::Dispatch(IocpEvent* iocpEvent, int numBytes)
{
	iocpEvent->_owner = nullptr;

	if (iocpEvent->_type == EventType::Job)
		ExecuteJobs();
}
