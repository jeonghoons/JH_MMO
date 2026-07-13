#pragma once
#include "pch.h"
#include <shared_mutex>

class RWLock {
public:
    
    class ReadGuard {
    public:
        ReadGuard(RWLock& lock) : _lock(lock) {
            _lock._mutex.lock_shared();
        }
        ~ReadGuard() {
            _lock._mutex.unlock_shared(); 
        }
    private:
        RWLock& _lock;
    };

    class WriteGuard {
    public:
        WriteGuard(RWLock& lock) : _lock(lock) {
            _lock._mutex.lock();  
        }
        ~WriteGuard() {
            _lock._mutex.unlock(); 
        }
    private:
        RWLock& _lock;
    };

private:
    std::shared_mutex _mutex; // ³»ºÎ mutex
};


/*----------------
    LockGuards
-----------------*/



