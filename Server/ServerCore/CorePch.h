#pragma once
// ServerCore 미리 컴파일된 헤더. 표준·Winsock·ODBC 헤더와 코어 공용 유틸만 둔다(Protocol·게임 헤더 금지).

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
#include <functional>

#include <random>
#include <algorithm>

#include <sql.h>
#include <sqlext.h>

extern thread_local unsigned int Lthreadid;

#include "Utils/RWLock.h"
#include "Utils/CoreUtils.h"
