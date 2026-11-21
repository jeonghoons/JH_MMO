#include "pch.h"
#include "thread"
#include "Network/ServerService.h"
#include "Network/IocpCore.h"
#include "Game/Room/Room.h"
#include "Packet/PacketHandler.h"
#include "Utils/Metrics.h"
#include "Session/GameSession.h"
#include <timeapi.h>
#pragma comment(lib, "winmm.lib")

shared_ptr<Timer> GTimer = nullptr;
shared_ptr<DatabaseWorker> GDBWorker = nullptr;
unique_ptr<RoomManager> GRoomManager = nullptr;
shared_ptr<AuthLobby> GLobby = nullptr;
shared_ptr<WorldService> GWorld = nullptr;
shared_ptr<AuctionService> GAuction = nullptr;



void worker_thread(shared_ptr<ServerService> service)
{
	while (true)
	{
		service->GetIocpInstance()->Dispatch();
	}
}

// 콘솔 창 닫기·Ctrl+C로 끝낼 때 큐에 남은 로그를 쓴다. FALSE를 돌려 기본 처리(프로세스 종료)로 넘긴다.
BOOL WINAPI OnConsoleCtrl(DWORD)
{
	Logger::Shutdown();
	return FALSE;
}

int main(int argc, char* argv[])
{
	// 설정을 읽기 전의 오류는 로거가 콘솔에 바로 쓴다(Init 전).
	string configErr;
	if (GServerConfig.Load(argc, argv, configErr) == false) {
		LOG_ERROR("Config", "{}", configErr);
		LOG_ERROR("Config", "설정 오류로 서버를 시작할 수 없습니다.");
		exit(-1);
	}

	string logErr;
	if (Logger::Init(GServerConfig.logLevel, GServerConfig.logDir, "GameServer", logErr) == false) {
		LOG_ERROR("Logger", "{}", logErr);
		LOG_ERROR("Logger", "로거를 시작할 수 없어 서버를 시작할 수 없습니다.");
		exit(-1);
	}
	::SetConsoleCtrlHandler(OnConsoleCtrl, TRUE);
	if (!Logger::FilePath().empty())
		LOG_INFO("Logger", "로그 파일: {}", Logger::FilePath());
	GServerConfig.Print();

	if (DataManager::Init() == false) {
		LOG_ERROR("Data", "DataTable 로드에 실패해 서버를 시작할 수 없습니다.");
		exit(-1);
	}
	if (DataManager::GetWorldData(GServerConfig.mapName) == nullptr) {
		LOG_ERROR("Data", "WorldTable에 맵 '{}' 행이 없어 서버를 시작할 수 없습니다. (ServerConfig [World] MapName)", GServerConfig.mapName);
		exit(-1);
	}
	// 시험용: 드랍 롤 빈도만 확인하고 끝낸다 (--roll-drops <kills>)
	if (GServerConfig.rollDropsKills > 0) {
		DataManager::LogDropRollTest(GServerConfig.rollDropsKills);
		Logger::Shutdown();
		return 0;
	}

	shared_ptr<ServerService> service = make_shared<ServerService>(
		NetAddress(Utils::UTF8ToWString(GServerConfig.bindIp), (USHORT)GServerConfig.port),
		make_shared<IocpCore>(),
		[]() { return make_shared<GameSession>(); }
	);

	if (service->Start() == false) {
		LOG_ERROR("Server", "{}:{} 포트를 열 수 없습니다(이미 사용 중이거나 주소가 잘못됨).", GServerConfig.bindIp, GServerConfig.port);
		exit(-1);
	}
	LOG_INFO("Server", "{}:{} 에서 접속 대기 시작", GServerConfig.bindIp, GServerConfig.port);

	// 시작 실패 시 exit()가 전역 소멸자에서 끝나지 않는 스레드(GTimer, DB 작업 스레드)를 join하며 멈추지 않도록,
	// 실패할 수 있는 단계(포트 → DB 연결)를 스레드를 만드는 단계보다 먼저 한다.
	GDBWorker = make_shared<DatabaseWorker>(service->GetIocpInstance()->GetHandle(), GServerConfig.dbConnections,
		Utils::UTF8ToWString(GServerConfig.dbConnectionString));

	PacketHandler::Init();

	// 타이머 해상도: 기본(약 15.6ms)에서는 Timer의 예약 작업이 그만큼 늦게 깬다. 프로세스가 끝나면 시스템이 되돌리므로 따로 풀지 않는다.
	if (GServerConfig.timerResolutionMs > 0) {
		if (::timeBeginPeriod(static_cast<UINT>(GServerConfig.timerResolutionMs)) != TIMERR_NOERROR)
			LOG_WARN("Server", "타이머 해상도 {}ms를 설정하지 못했습니다 — 시스템 기본으로 동작합니다", GServerConfig.timerResolutionMs);
	}

	GTimer = make_shared<Timer>();
	GRoomManager = make_unique<RoomManager>();
	// 전역 서비스(DG3a): 채널 1과 그 Room을 여기서 만든다 — 맵 로드 오류를 시작 로그에서 본다. 채널 2부터는 필요할 때 서비스 잡에서 만든다.
	GWorld = make_shared<WorldService>(service->GetIocpInstance()->GetHandle());
	if (GWorld->Init() == false)
		LOG_ERROR("World", "채널 1을 만들지 못했습니다 — 로그인한 플레이어가 들어갈 Room이 없습니다. (ServerConfig [World] MapName = {})", GServerConfig.mapName);
	GLobby = make_shared<AuthLobby>(service->GetIocpInstance()->GetHandle());
	// 경매장 서비스(A4.3): DatabaseWorker가 시작 때 읽어 둔 판매 중인 등록으로 목록을 만들고 만료 검사를 시작한다.
	GAuction = make_shared<AuctionService>(service->GetIocpInstance()->GetHandle());
	GAuction->Init(GDBWorker->TakeInitialAuctionListings());
	// 연결 정리(A5.0): 받지 않는 클라이언트의 송신 큐 상한(D-221)과 유휴 세션 검사(D-220)
	Session::SetSendQueueLimit(GServerConfig.sendQueueLimit);
	GLobby->StartIdleCheck(service);
	Metrics::StartReporter();
	
	vector<thread> threads;
	int num_threads = GServerConfig.ResolvedWorkerThreads();
	for (int i = 0; i < num_threads; ++i) {
		threads.emplace_back(worker_thread, service);
	}

	for (thread& t : threads) {
		if(t.joinable()) 
			t.join();
	}

}