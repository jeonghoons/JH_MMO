#include "pch.h"
#include "Timer.h"
#include "DatabaseWorker.h"

void Timer::Run()
{
    using namespace std::chrono;
    mutex& queueMutex = _timerQueue.GetMutex();
    condition_variable& queueCV = _timerQueue.GetCV();
    priority_queue<TimerItem>& queue = _timerQueue.GetQueueUnsafe();

    unique_lock<mutex> lock(queueMutex);

    while (true)
    {
        // 예약이 없으면 Push의 notify까지 잠든다.
        if (queue.empty())
        {
            queueCV.wait(lock);
            continue;
        }

        // 가장 이른 예약 시각까지 잠든다. 더 이른 예약이 Push되면 깨어나서 다시 계산한다.
        TimePoint nextTime = queue.top()._executeTime;
        if (steady_clock::now() < nextTime)
        {
            queueCV.wait_until(lock, nextTime);
            continue;
        }

        vector<TimerItem> readyJobs;
        const TimePoint now = steady_clock::now();
        while (!queue.empty() && queue.top()._executeTime <= now)
        {
            readyJobs.push_back(queue.top());
            queue.pop();
        }

        // JobQueue::Push가 IOCP에 포스트하므로 락을 풀고 실행한다.
        lock.unlock();
        for (TimerItem& jobItem : readyJobs)
        {
            if (shared_ptr<JobQueue> jobQueue = jobItem._jobQueue.lock())
            {
                jobQueue->Push(std::move(jobItem._job));
            }
        }
        lock.lock();
    }
}
