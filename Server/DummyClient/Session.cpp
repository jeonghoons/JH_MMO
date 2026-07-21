#include "pch.h"
#include "Session.h"
#include "SendBuffer.h"

Session::Session(int sessionId)
    : _sessionId(sessionId), _id(-1), _connected(false), _socket(INVALID_SOCKET), _recvBuffer(MAX_BUFF_SIZE)
{
    _recvOver.event_type = OP_RECV;
    _recvOver.sendBuffer = nullptr;
}

Session::~Session() {
    Disconnect();
}

bool Session::Connect(const char* ip, short port, HANDLE iocpHandle) {
    _socket = WSASocketW(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0, WSA_FLAG_OVERLAPPED);

    SOCKADDR_IN ServerAddr;
    ZeroMemory(&ServerAddr, sizeof(SOCKADDR_IN));
    ServerAddr.sin_family = AF_INET;
    ServerAddr.sin_port = htons(port);
    ServerAddr.sin_addr.s_addr = inet_addr(ip);

    int result = WSAConnect(_socket, (sockaddr*)&ServerAddr, sizeof(ServerAddr), NULL, NULL, NULL, NULL);
    if (result != 0) return false;

    CreateIoCompletionPort(reinterpret_cast<HANDLE>(_socket), iocpHandle, _sessionId, 0);

    // 수신 버퍼 초기화 및 첫 수신 예약
    _recvBuffer.CleanCheck();
    RegisterRecv();
    return true;
}

void Session::Disconnect() {
    bool expected = true;
    if (_connected.compare_exchange_strong(expected, false)) {
        closesocket(_socket);
        _socket = INVALID_SOCKET;
        active_clients--;
    }
}

void Session::Send(uint16_t packet_type, const google::protobuf::Message& packet) {
    if (_socket == INVALID_SOCKET) return;

    uint16_t payload_size = static_cast<uint16_t>(packet.ByteSizeLong());
    uint16_t total_size = sizeof(PacketHeader) + payload_size;

    // SendBuffer 동적 할당 (완료 통지 후 삭제)
    SendBuffer* sendBuffer = new SendBuffer(total_size);

    PacketHeader header;
    header.size = total_size;
    header.type = packet_type;

    memcpy(sendBuffer->Buffer(), &header, sizeof(PacketHeader));
    packet.SerializeToArray(sendBuffer->Buffer() + sizeof(PacketHeader), payload_size);
    sendBuffer->Commit(total_size);

    OverlappedEx* over = new OverlappedEx;
    ZeroMemory(&over->over, sizeof(over->over));
    over->event_type = OP_SEND;
    over->sendBuffer = sendBuffer; // 메모리 해제를 위해 포인터 저장
    over->wsabuf.buf = reinterpret_cast<CHAR*>(sendBuffer->Buffer());
    over->wsabuf.len = sendBuffer->WritePos();

    int ret = WSASend(_socket, &over->wsabuf, 1, NULL, 0, &over->over, NULL);
    if (ret == SOCKET_ERROR) {
        if (WSAGetLastError() != WSA_IO_PENDING) {
            delete sendBuffer;
            delete over;
        }
    }
}

void Session::RegisterRecv() {
    if (_socket == INVALID_SOCKET) return;

    _recvBuffer.CleanCheck();

    ZeroMemory(&_recvOver.over, sizeof(_recvOver.over));
    _recvOver.wsabuf.buf = reinterpret_cast<CHAR*>(_recvBuffer.WritePos());
    _recvOver.wsabuf.len = static_cast<ULONG>(_recvBuffer.FreeSize());

    DWORD recv_flag = 0;
    int ret = WSARecv(_socket, &_recvOver.wsabuf, 1, NULL, &recv_flag, &_recvOver.over, NULL);
    if (ret == SOCKET_ERROR) {
        if (WSAGetLastError() != WSA_IO_PENDING) {
            Disconnect();
        }
    }
}