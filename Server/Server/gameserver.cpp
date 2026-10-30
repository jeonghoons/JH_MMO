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

int main()
{
	if (DataManager::Init() == false) {
		printf("DataTable 로드에 실패해 서버를 시작할 수 없습니다.\n");
		exit(-1);
	}

	shared_ptr<ServerService> service = make_shared<ServerService>(
		NetAddress(L"127.0.0.1", PORT_NUM),
		make_shared<IocpCore>(),
		[]() { return make_shared<GameSession>(); }
	);

	if (service->Start() == false) {
		cout << "Can't Start" << endl;
		exit(-1);
	}
	else
		cout << "Service Start" << endl;

	PacketHandler::Init();
	GTimer = make_shared<Timer>();
	GDBWorker = make_shared<DatabaseWorker>(service->GetIocpInstance()->GetHandle(), 4);
	GRoomManager = make_unique<RoomManager>(service->GetIocpInstance()->GetHandle());
	GRoomManager->CreateRoom();
	GLobby = make_shared<AuthLobby>(service->GetIocpInstance()->GetHandle());
	Metrics::StartReporter();
	
	vector<thread> threads;
	int num_threads = thread::hardware_concurrency() / 2;
	for (int i = 0; i < num_threads; ++i) {
		threads.emplace_back(worker_thread, service);
	}

	for (thread& t : threads) {
		if(t.joinable()) 
			t.join();
	}

}