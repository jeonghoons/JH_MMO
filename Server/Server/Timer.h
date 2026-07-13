#pragma once
#include "JobQueue.h"
#include "ConcurrentQueue.h"
#include <concurrent_priority_queue.h>

using TimePoint = std::chrono::steady_clock::time_point;

struct TimerItem
{
    bool operator<(const TimerItem& other) const 
    {
        return _executeTime > other._executeTime;
    }
    TimerItem() = default;
    TimerItem(TimePoint time, std::shared_ptr<Job> job, std::shared_ptr<JobQueue> jobQueue)
        : _executeTime(time), _job(std::move(job)), _jobQueue(jobQueue) {
    }

    TimePoint _executeTime{};                  
    shared_ptr<Job> _job{};               
    weak_ptr<JobQueue> _jobQueue{};    

};

class Timer
{
public:
    Timer()
    {
        _thread = std::make_unique<std::thread>(&Timer::Run, this);
    }

    ~Timer()
    {
        if (_thread->joinable())
        {
            _thread->join();
        }
    }

    // 타이머 작업 예약 메소드
    template<typename T, typename... Arguments>
    void Reserve(DWORD milliseconds,shared_ptr<JobQueue> jobQueue, shared_ptr<T> owner, void(T::* memFunc)(Arguments...), Arguments&&... args)
    {
        TimePoint executeTime = std::chrono::steady_clock::now() + std::chrono::milliseconds(milliseconds);
        shared_ptr<Job> job = std::make_shared<Job>(owner, memFunc, std::forward<Arguments>(args)...);

        TimerItem item = TimerItem(executeTime, std::move(job), jobQueue);
        _timerQueue.Push(item);
    }

private:
    void Run();


private:
    // concurrency::concurrent_queue<TimerItem> _timerQueue;
    ConcurrentPQ<TimerItem> _timerQueue;
    unique_ptr<std::thread> _thread;
};




