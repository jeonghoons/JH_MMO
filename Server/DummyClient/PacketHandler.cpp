#include "pch.h"
#include "Session.h"
#include "PacketHandler.h" 

extern std::array<Session*, MAX_CLIENTS> g_sessions;
extern std::array<int, MAX_CLIENTS> client_map;

void PacketHandler::Process(Session* session, BYTE* buffer, size_t len) {
    PacketHeader* header = reinterpret_cast<PacketHeader*>(buffer);
    BYTE* payload = buffer + sizeof(PacketHeader);
    uint16_t payload_size = header->size - sizeof(PacketHeader);

    switch (header->type) {
    case Protocol::SC_MOVE_OBJECT: {
        Protocol::SC_MOVE_PACKET pkt;
        if (pkt.ParseFromArray(payload, payload_size)) {
            const Protocol::ObjectInfo& info = pkt.object_info();

            // [중요] 배열 접근 전 반드시 범위 초과 검사! (몬스터/NPC 무시)
            if (info.id() < 0 || info.id() >= MAX_CLIENTS) return;

            int index = client_map[info.id()];
            if (index >= 0 && index < MAX_CLIENTS) {
                g_sessions[index]->x = static_cast<int>(info.position().x());
                g_sessions[index]->y = static_cast<int>(info.position().y());

                if (session->GetId() == info.id()) {
                    auto now = std::chrono::high_resolution_clock::now();
                    auto d_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - session->last_move_time).count();
                    if (global_delay < d_ms) global_delay++;
                    else if (global_delay > d_ms) global_delay--;
                }
            }
        }
        break;
    }
    case Protocol::SC_ADD_OBJECT: {
        Protocol::SC_ADD_OBJECT_PACKET pkt;
        if (pkt.ParseFromArray(payload, payload_size)) {
            const Protocol::ObjectInfo& info = pkt.object_info();

            // [중요] 배열 접근 전 반드시 범위 초과 검사!
            if (info.id() < 0 || info.id() >= MAX_CLIENTS) return;

            int index = client_map[info.id()];
            if (index >= 0 && index < MAX_CLIENTS) {
                if (!g_sessions[index]->IsConnected()) {
                    g_sessions[index]->SetConnected(true);
                    active_clients++;
                }
                g_sessions[index]->x = static_cast<int>(info.position().x());
                g_sessions[index]->y = static_cast<int>(info.position().y());
            }
        }
        break;
    }
    case Protocol::SC_LOGIN: {
        Protocol::SC_LOGIN_INFO_PACKET pkt;
        if (pkt.ParseFromArray(payload, payload_size)) {
            const Protocol::ObjectInfo& info = pkt.object_info();

            // [중요] 내 아이디가 잘못 날아올 경우를 대비한 방어 코드
            if (info.id() < 0 || info.id() >= MAX_CLIENTS) return;

            client_map[info.id()] = session->GetSessionId();

            session->SetId(info.id());
            session->x = static_cast<int>(info.position().x());
            session->y = static_cast<int>(info.position().y());

            Protocol::CS_ENTER_ROOM_PACKET enter_pkt;
            enter_pkt.set_result(true);
            session->Send(Protocol::CS_ENTER_ROOM, enter_pkt);
        }
        break;
    }
    default:
        break;
    }
}