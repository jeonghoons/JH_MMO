#pragma once
#define _WINSOCK_DEPRECATED_NO_WARNINGS

#include <WinSock2.h>
#include <windows.h>
#include <atomic>
#include <chrono>
#include <iostream>
#include <vector>
#include <array>
#include <memory>

#ifdef _DEBUG
#pragma comment(lib, "libprotobufd.lib")
#else 
#pragma comment(lib, "libprotobuf.lib")
#endif 

#pragma comment (lib, "ws2_32.lib")
#pragma comment (lib, "opengl32.lib")
#pragma comment (lib, "glu32.lib")


#include "Protocol/Protocol.pb.h"
#include "SendBuffer.h"

constexpr int MAX_TEST = 50000;
constexpr int MAX_CLIENTS = MAX_TEST * 2;
constexpr int MAX_BUFF_SIZE = 4096;

#pragma pack(push, 1)
struct PacketHeader {
    unsigned short size;
    unsigned short type;
};
#pragma pack(pop)

enum OPTYPE { OP_SEND, OP_RECV };

struct OverlappedEx {
    WSAOVERLAPPED over;
    WSABUF wsabuf;
    OPTYPE event_type;
    SendBuffer* sendBuffer = nullptr;
};

extern std::atomic_int active_clients;
extern std::atomic_int global_delay;
extern HWND g_hWnd;

