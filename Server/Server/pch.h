#pragma once

#define WIN32_LEAN_AND_MEAN // 거의 사용되지 않는 내용을 Windows 헤더에서 제외합니다.
#define NOMINMAX

#include <iostream> 
#include <WS2tcpip.h>
#include <MSWSock.h>
#include <Windows.h>
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

#include "TestProtocol.h"
#include "RWLock.h"
#include "Room.h"
#include "Timer.h"
#include "DatabaseWorker.h"
#include "PacketSerializer.h"
#include "AuthLobby.h"
#include "Utils.h"
#include "ServerData.h"


extern unique_ptr<RoomManager> GRoomManager;
extern shared_ptr<Timer> GTimer;
extern shared_ptr<DatabaseWorker> GDBWorker;
extern shared_ptr<AuthLobby> GLobby;



