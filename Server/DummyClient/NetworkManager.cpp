#include "pch.h"
#include "NetworkManager.h"
#include "Session.h"
#include "PacketHandler.h"

std::array<Session*, MAX_CLIENTS> g_sessions;
std::array<int, MAX_CLIENTS> client_map;
float point_cloud[MAX_TEST * 2];

constexpr int DELAY_LIMIT = 100;
constexpr int DELAY_LIMIT2 = 150;
constexpr int ACCEPT_DELY = 50;

NetworkManager::NetworkManager() : _hiocp(INVALID_HANDLE_VALUE), _numConnections(0), _clientToClose(0) {}

NetworkManager::~NetworkManager() { Shutdown(); }

void NetworkManager::Initialize() {
    WSADATA wsadata;
    WSAStartup(MAKEWORD(2, 2), &wsadata);

    for (int i = 0; i < MAX_CLIENTS; ++i) {
        g_sessions[i] = new Session(i);
        client_map[i] = -1;
    }

    _hiocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, 0, NULL, 0);
    _lastConnectTime = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < 6; ++i) {
        _workerThreads.emplace_back(&NetworkManager::WorkerThread, this);
    }
    _dummyThread = std::thread(&NetworkManager::DummyBehaviorThread, this);
}

void NetworkManager::Shutdown() {
    if (_dummyThread.joinable()) _dummyThread.join();
    for (auto& th : _workerThreads) {
        if (th.joinable()) th.join();
    }
    for (int i = 0; i < MAX_CLIENTS; ++i) {
        delete g_sessions[i];
    }
    WSACleanup();
}

void NetworkManager::WorkerThread() {
    while (true) {
        DWORD io_size;
        ULONG_PTR key;
        OverlappedEx* over;
        BOOL ret = GetQueuedCompletionStatus(_hiocp, &io_size, &key, reinterpret_cast<LPWSAOVERLAPPED*>(&over), INFINITE);

        Session* session = g_sessions[key];
        if (ret == FALSE || io_size == 0) {
            session->Disconnect();
            if (over && over->event_type == OP_SEND) {
                if (over->sendBuffer) delete over->sendBuffer;
                delete over;
            }
            continue;
        }

        if (over->event_type == OP_RECV) {
            // Write 위치 이동
            if (session->GetRecvBuffer().OnWrite(io_size)) {
                int dataSize = session->GetRecvBuffer().DataSize();

                // 패킷 처리 루프
                while (dataSize >= sizeof(PacketHeader)) {
                    BYTE* readPos = session->GetRecvBuffer().ReadPos();
                    PacketHeader* header = reinterpret_cast<PacketHeader*>(readPos);

                    if (dataSize >= header->size) {
                        PacketHandler::Process(session, readPos, header->size);
                        session->GetRecvBuffer().OnRead(header->size); // Read 위치 이동
                        dataSize = session->GetRecvBuffer().DataSize();
                    }
                    else {
                        break; // 패킷이 아직 다 도착하지 않음
                    }
                }
                session->RegisterRecv();
            }
            else {
                session->Disconnect();
            }
        }
        else if (over->event_type == OP_SEND) {
            // 송신 완료 후 SendBuffer와 OverlappedEx 메모리 해제
            if (over->sendBuffer) delete over->sendBuffer;
            delete over;
        }
    }
}

void NetworkManager::AdjustClientCount() {
    if (active_clients >= MAX_TEST || _numConnections >= MAX_CLIENTS) return;

    auto duration = std::chrono::high_resolution_clock::now() - _lastConnectTime;
    if (std::chrono::duration_cast<std::chrono::milliseconds>(duration).count() < ACCEPT_DELY) return;

    _lastConnectTime = std::chrono::high_resolution_clock::now();
    int current_index = _numConnections++;

    if (g_sessions[current_index]->Connect("127.0.0.1", 8888, _hiocp)) {
        Protocol::CS_LOGIN_PACKET pkt;
        char accountID[20];
        sprintf_s(accountID, 20, "DUMMY_%d", current_index);
        pkt.set_account_id(accountID);
        pkt.set_account_pw("1234");
        pkt.set_is_dummy(true);
        g_sessions[current_index]->Send(Protocol::CS_LOGIN, pkt);
    }
}

void NetworkManager::DummyBehaviorThread() {
    Protocol::CS_MOVE_PACKET move_pkt;
    move_pkt.mutable_pos_info()->set_z(100.f);
    move_pkt.mutable_pos_info()->set_state(Protocol::MOVE_STATE_RUN);
    move_pkt.set_force(false);

    while (true) {
        Sleep(max(20, global_delay.load()));
        AdjustClientCount();

        auto now = std::chrono::high_resolution_clock::now();
        for (int i = 0; i < _numConnections; ++i) {
            Session* s = g_sessions[i];
            if (!s->IsConnected()) continue;
            if (s->last_move_time + std::chrono::seconds(1) > now) continue;
            s->last_move_time = now;

            Protocol::PositionInfo* pos = move_pkt.mutable_pos_info();
            pos->set_x(static_cast<float>(s->x));
            pos->set_y(static_cast<float>(s->y));
            float speed = 300.0f;
            pos->set_v_x(0.f); pos->set_v_y(0.f); pos->set_v_z(0.f);

            switch (rand() % 4) {
            case 0: pos->set_x(pos->x() - speed); pos->set_v_x(-speed); pos->set_yaw(180.0f); break;
            case 1: pos->set_x(pos->x() + speed); pos->set_v_x(speed); pos->set_yaw(0.0f); break;
            case 2: pos->set_y(pos->y() - speed); pos->set_v_y(-speed); pos->set_yaw(-90.0f); break;
            case 3: pos->set_y(pos->y() + speed); pos->set_v_y(speed); pos->set_yaw(90.0f); break;
            }
            s->Send(Protocol::CS_MOVE, move_pkt);
        }
    }
}

void NetworkManager::GetPointData(int* size, float** points) {
    int index = 0;
    for (int i = 0; i < _numConnections; ++i) {
        if (g_sessions[i]->IsConnected()) {
            point_cloud[index * 2] = static_cast<float>(g_sessions[i]->x);
            point_cloud[index * 2 + 1] = static_cast<float>(g_sessions[i]->y);
            index++;
        }
    }
    *size = index;
    *points = point_cloud;
}