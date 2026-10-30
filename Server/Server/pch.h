#pragma once
// GameServer 미리 컴파일된 헤더. 코어(CorePch: 표준·Winsock·ODBC·코어 유틸) 위에 Protocol과 게임 공용 헤더를 얹는다.

#include "CorePch.h"

#ifdef _DEBUG
#pragma comment(lib, "libprotobufd.lib")
#else
#pragma comment(lib, "libprotobuf.lib")
#endif

#include <DirectXMath.h>

using namespace DirectX;

#pragma warning(push)
#pragma warning(disable: 26495) // 초기화 안 됨(C26495) 경고 끄기
#pragma warning(disable: 4251)  // dll-interface 경고 끄기
#pragma warning(disable: 4100)  // 참조되지 않은 매개변수 경고 끄기
#pragma warning(disable: 4946) // reinterpret_cast 경고 끄기
#include "Protocol/Protocol.pb.h"
#pragma warning(pop)

#include "Session/GameSession.h"
#include "Game/Room/Room.h"
#include "Job/Timer.h"
#include "DB/DatabaseWorker.h"
#include "Packet/PacketSerializer.h"
#include "Game/Lobby/AuthLobby.h"
#include "Utils/MathUtils.h"
#include "Data/ServerData.h"


constexpr uint16_t PORT_NUM = 8888;


extern unique_ptr<RoomManager> GRoomManager;
extern shared_ptr<Timer> GTimer;
extern shared_ptr<DatabaseWorker> GDBWorker;
extern shared_ptr<AuthLobby> GLobby;
