#include "pch.h"
#include "Timer.h"
#include "DatabaseWorker.h"

void Timer::Run()
{
    using namespace std::chrono;
    mutex& queueMutex = _timerQueue.GetMutex();
    condition_variable& queueCV = _timerQueue.GetCV();

    while (true)
    {
        TimePoint now = steady_clock::now();
        TimePoint executeTime = now + milliseconds(1000);
        TimerItem nextItem;

        if (_timerQueue.Peek(nextItem))
        {
            executeTime = nextItem._executeTime;
        }

        unique_lock<mutex> lock(queueMutex);
        queueCV.wait_until(lock, executeTime, [this] {
            return !_timerQueue.Is_empty();
            });

        vector<TimerItem> readyJobs;

        while (!_timerQueue.GetQueueUnsafe().empty())
        {
            TimerItem item = _timerQueue.GetQueueUnsafe().top(); 

            if (item._executeTime <= now)
            {
                _timerQueue.GetQueueUnsafe().pop();
                readyJobs.push_back(std::move(item));
            }
            else
            {
                break;
            }
        }

        
        for (TimerItem& jobItem : readyJobs)
        {
            if (shared_ptr<JobQueue> jobQueue = jobItem._jobQueue.lock())
            {
                jobQueue->Push(std::move(jobItem._job));
            }
        }
    }
}


