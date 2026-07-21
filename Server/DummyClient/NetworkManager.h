#pragma once

class NetworkManager {
public:
    NetworkManager();
    ~NetworkManager();

    void Initialize();
    void Shutdown();
    void GetPointData(int* size, float** points);

private:
    void WorkerThread();
    void DummyBehaviorThread();
    void AdjustClientCount();

    HANDLE _hiocp;
    std::vector<std::thread> _workerThreads;
    std::thread _dummyThread;

    std::atomic_int _numConnections;
    std::atomic_int _clientToClose;
    std::chrono::high_resolution_clock::time_point _lastConnectTime;
};

