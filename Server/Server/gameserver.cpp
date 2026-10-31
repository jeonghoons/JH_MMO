#include "pch.h"
#include "thread"
#include "Network/ServerService.h"
#include "Network/IocpCore.h"
#include "Game/Room/Room.h"
#include "Packet/PacketHandler.h"
#include "Utils/Metrics.h"
#include "Session/GameSession.h"

shared_ptr<Timer> GTimer = nullptr;
shared_ptr<DatabaseWorker> GDBWorker = nullptr;
unique_ptr<RoomManager> GRoomManager = nullptr;
shared_ptr<AuthLobby> GLobby = nullptr;



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
	GTimer = make_shared<Timer>();
	GRoomManager = make_unique<RoomManager>(service->GetIocpInstance()->GetHandle());
	GRoomManager->CreateRoom();
	GLobby = make_shared<AuthLobby>(service->GetIocpInstance()->GetHandle());
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