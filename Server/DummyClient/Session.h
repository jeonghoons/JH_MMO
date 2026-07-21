#pragma once
#include "RecvBuffer.h"

class Session {
public:
    Session(int sessionId);
    ~Session();

    bool Connect(const char* ip, short port, HANDLE iocpHandle);
    void Disconnect();
    void Send(uint16_t packet_type, const google::protobuf::Message& packet);
    void RegisterRecv();

    int GetSessionId() const { return _sessionId; }
    int GetId() const { return _id; }
    void SetId(int id) { _id = id; }
    bool IsConnected() const { return _connected; }
    void SetConnected(bool state) { _connected = state; }

    int x = 0;
    int y = 0;
    std::chrono::high_resolution_clock::time_point last_move_time;

    SOCKET GetSocket() { return _socket; }
    RecvBuffer& GetRecvBuffer() { return _recvBuffer; }
    OverlappedEx& GetRecvOverlapped() { return _recvOver; }

private:
    int _sessionId;
    int _id;
    std::atomic_bool _connected;
    SOCKET _socket;

    RecvBuffer _recvBuffer;
    OverlappedEx _recvOver;
};

