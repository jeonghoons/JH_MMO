#include "pch.h"
#include "Game/Lobby/AuthLobby.h"
#include "Network/Session.h"
#include "Game/Object/Player.h"

AuthLobby::AuthLobby(HANDLE iocpHandle) : _jobQueue(make_shared<JobQueue>(iocpHandle))
{
}

AuthLobby::~AuthLobby() = default;

void AuthLobby::OnLoginSuccess(shared_ptr<GameSession> session, DB_PlayerInfo info, DB_PlayerData data)
{
    if (session->IsConnected() == false) return;

    // DB 응답 전에 로그인 요청이 연달아 오면 성공이 두 번 올 수 있다. 로비 잡은 한 스레드씩 실행되므로 여기서 두 번째를 거른다.
    if (session->_currPlayer != nullptr)
    {
        OnLoginFailed(session, Protocol::LOGIN_RESULT_ALREADY_LOGGED_IN);
        return;
    }

    int objectId = session->GetId();
    shared_ptr<Player> player = make_shared<Player>(session, (Protocol::PlayerType)info.playerType);

    player->SetId(objectId);

    wstring playerName(info.playerName);
    player->SetName(playerName);
    if (playerName == L"dummy")
        player->isDummy = true;

    session->_currPlayer = player;

    Protocol::SC_LOGIN_INFO_PACKET logInPacket;
    logInPacket.mutable_object_info()->CopyFrom(player->GetInfo());
    logInPacket.set_result(Protocol::LOGIN_RESULT_SUCCESS);
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

void AuthLobby::OnLoginFailed(shared_ptr<GameSession> session, Protocol::LoginResult result)
{
	if (session->IsConnected() == false) return;

	
	cout << "Login Failed: " << Protocol::LoginResult_Name(result) << endl;

	// 실패도 응답해야 클라이언트가 로그인 화면에서 결과를 보여줄 수 있다.
	Protocol::SC_LOGIN_INFO_PACKET pkt;
	pkt.set_result(result);
	session->Send(PacketSerializer::MakeSendBuffer(pkt, Protocol::SC_LOGIN));
}

void AuthLobby::OnSignUpResult(shared_ptr<GameSession> session, bool success)
{
	if (session->IsConnected() == false) return;

	Protocol::SC_SIGNUP_PACKET pkt;
	pkt.set_result(success);
	session->Send(PacketSerializer::MakeSendBuffer(pkt, Protocol::SC_SIGNUP));
}
