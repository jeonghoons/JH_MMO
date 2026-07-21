#include "pch.h"
#include "AuthLobby.h"
#include "Session.h"
#include "Player.h"

AuthLobby::AuthLobby(HANDLE iocpHandle) : _jobQueue(make_shared<JobQueue>(iocpHandle))
{
}

AuthLobby::~AuthLobby() = default;

void AuthLobby::OnLoginSuccess(shared_ptr<Session> session, DB_PlayerInfo info, DB_PlayerData data)
{
    if (session->IsConnected() == false) return;

    int objectId = session->GetId();
    shared_ptr<Player> player = make_shared<Player>(session, (Protocol::PlayerType)info.playerType);

    player->SetId(objectId);

    wstring playerName(info.playerName);
    if (playerName == L"dummy")
        player->isDummy = true;

    session->_currPlayer = player;

    Protocol::SC_LOGIN_INFO_PACKET logInPacket;
    logInPacket.mutable_object_info()->CopyFrom(player->GetInfo());
    uint16_t dateSize = logInPacket.ByteSizeLong();
    uint16_t packetSize = sizeof(PacketHeader) + dateSize;

    shared_ptr<SendBuffer> sendBuffer = make_shared<SendBuffer>(packetSize);

    PacketHeader* header = reinterpret_cast<PacketHeader*>(sendBuffer->Buffer());
    header->size = packetSize;
    header->id = Protocol::SC_LOGIN;

    logInPacket.SerializeToArray(&header[1], dateSize);
    sendBuffer->Commit(packetSize);

    session->Send(sendBuffer);

    wcout << L"[" << playerName << L"] - 로그인 성공 및 객체 생성 완료" << std::endl;
}

void AuthLobby::OnLoginFailed(shared_ptr<Session> session, string errorMsg)
{
	if (session->IsConnected() == false) return;

	
	cout << "Login Failed: " << errorMsg << endl;
}