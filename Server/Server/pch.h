#pragma once

#define WIN32_LEAN_AND_MEAN // 거의 사용되지 않는 내용을 Windows 헤더에서 제외합니다.
#define NOMINMAX

#include <iostream> 
#include <WS2tcpip.h>
#include <MSWSock.h>
#include <Windows.h>

#ifdef _DEBUG
#pragma comment(lib, "libprotobufd.lib")
#else 
#pragma comment(lib, "libprotobuf.lib")
#endif 

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "MSWSock.lib")

using namespace std;

#include <memory>
#include <vector>
#include <set>
#include <unordered_set>
#include <queue>
#include <chrono>
#include <unordered_map>
#include <thread>
#include <mutex>
#include <map>

#include <random>
#include <algorithm>

#include <sql.h>
#include <sqlext.h>

#include <DirectXMath.h>

using namespace DirectX;

extern thread_local unsigned int Lthreadid;

#pragma warning(push)
#pragma warning(disable: 26495) // 초기화 안 됨(C26495) 경고 끄기
#pragma warning(disable: 4251)  // dll-interface 경고 끄기
#pragma warning(disable: 4100)  // 참조되지 않은 매개변수 경고 끄기
#pragma warning(disable: 4946) // reinterpret_cast 경고 끄기
#include "Protocol/Protocol.pb.h"
#pragma warning(pop)

#include "RWLock.h"
#include "Room.h"
#include "Timer.h"
#include "DatabaseWorker.h"
#include "PacketSerializer.h"
#include "AuthLobby.h"
#include "Utils.h"
#include "ServerData.h"

constexpr uint16_t PORT_NUM = 8888;


extern unique_ptr<RoomManager> GRoomManager;
extern shared_ptr<Timer> GTimer;
extern shared_ptr<DatabaseWorker> GDBWorker;
extern shared_ptr<AuthLobby> GLobby;



