#include "pch.h"
#include "Game/Lobby/AuthLobby.h"
#include "Network/Session.h"
#include "Network/ServerService.h"
#include "Game/Object/Player.h"
#include "DB/DatabaseWorker.h"
#include "Utils/Metrics.h"

extern shared_ptr<DatabaseWorker> GDBWorker;

namespace
{
    // 더미 봇 외형(A5.1, D-294): 모든 더미가 DB의 공용 dummy 캐릭터 하나라 체형·장비가 같으므로, 메모리에서만 무작위로 바꾼다(DB에는 쓰지 않음 —
    // 더미는 장착·아이템 요청이 거부되고 진행 상황도 저장하지 않는다). 시드는 세션 id라 같은 순서로 들어오면 같은 차림이 된다.
    // 후보는 ItemTable에서 슬롯·무기 종류로 모은다(하드코딩 없음).
    void DressDummy(Player& player, int seed)
    {
        static const struct Candidates
        {
            vector<int> armor[5];         // Head~Boots
            vector<int> weapon[3];        // Sword, Bow, TwoHandSword
            vector<int> offhand;
            Candidates()
            {
                for (const auto& [itemId, d] : DataManager::GetItemTable())
                {
                    const int slot = static_cast<int>(d.slot);
                    if (slot >= static_cast<int>(ItemSlot::Head) && slot <= static_cast<int>(ItemSlot::Boots)) armor[slot - 1].push_back(itemId);
                    else if (d.slot == ItemSlot::Offhand) offhand.push_back(itemId);
                    else if (d.slot == ItemSlot::Weapon)
                    {
                        if (d.weaponType == WeaponType::Sword) weapon[0].push_back(itemId);
                        else if (d.weaponType == WeaponType::Bow) weapon[1].push_back(itemId);
                        else if (d.weaponType == WeaponType::TwoHandSword) weapon[2].push_back(itemId);
                    }
                }
            }
        } candidates;

        mt19937 rng(static_cast<uint32_t>(seed) * 2654435761u + 12345u);
        auto pick = [&rng](const vector<int>& list) { return list.empty() ? 0 : list[uniform_int_distribution<size_t>(0, list.size() - 1)(rng)]; };
        auto chance = [&rng](int percent) { return static_cast<int>(rng() % 100) < percent; };

        player.SetPlayerType(chance(50) ? Protocol::PLAYER_TYPE_FEMALE : Protocol::PLAYER_TYPE_MALE);

        // 방어구 5칸: 칸마다 티어 무작위, 약 15%는 빈칸
        for (int i = 0; i < 5; ++i)
            player.SetEquippedItem(static_cast<Protocol::EquipPart>(i + 1), chance(15) ? 0 : pick(candidates.armor[i]));

        // 무기: 한손검·활·양손검 1/3씩. 방패는 한손검일 때만 50%
        const int weaponKind = static_cast<int>(rng() % 3);
        player.SetEquippedItem(Protocol::EQUIP_PART_WEAPON, pick(candidates.weapon[weaponKind]));
        player.SetEquippedItem(Protocol::EQUIP_PART_OFFHAND, (weaponKind == 0 && chance(50)) ? pick(candidates.offhand) : 0);

        player.RecalcStat();
        player.GetStat().FillHpMp();

        const auto& eq = player.GetInfo().equip_items();
        LOG_DEBUG("Lobby", "더미 {} 외형: 체형 {} 장비 {},{},{},{},{},{},{}", seed, static_cast<int>(player.GetInfo().player_type()),
            eq.Get(0), eq.Get(1), eq.Get(2), eq.Get(3), eq.Get(4), eq.Get(5), eq.Get(6));
    }
}

AuthLobby::AuthLobby(HANDLE iocpHandle) : _jobQueue(make_shared<JobQueue>(iocpHandle))
{
}

AuthLobby::~AuthLobby() = default;

void AuthLobby::OnLoginSuccess(shared_ptr<GameSession> session, DB_PlayerInfo info, DB_PlayerData data, string accountId, string accountPw)
{
    if (session->IsConnected() == false) return;

    // DB 응답 전에 로그인 요청이 연달아 오면 성공이 두 번 올 수 있다. 로비 잡은 한 스레드씩 실행되므로 여기서 두 번째를 거른다.
    if (session->GetPlayer() != nullptr)
    {
        OnLoginFailed(session, Protocol::LOGIN_RESULT_ALREADY_LOGGED_IN);
        return;
    }

    // 체형은 MALE/FEMALE뿐이다(옛 4·5는 MC3.3 마이그레이션에서 1로 바꿨다). 다른 값이면 DB가 잘못된 것이다.
    if (info.playerType != Protocol::PLAYER_TYPE_MALE && info.playerType != Protocol::PLAYER_TYPE_FEMALE)
    {
        LOG_ERROR("Lobby", "PlayerId {} 의 PlayerType {} 은 체형이 아닙니다 (migrations/0002_2026-11-07_mc33_equip.sql 실행 여부 확인)", info.playerUID, info.playerType);
        OnLoginFailed(session, Protocol::LOGIN_RESULT_SERVER_ERROR);
        return;
    }

    // 중복 접속 차단(A2.7, D-158): 이 캐릭터가 접속 목록에 있으면 기존 접속을 끊고, 그 접속의 마지막 저장이 끝날 때까지 기다린다.
    // 방금 읽은 DB 값은 그 저장 전의 값일 수 있으므로 버리고, 목록에서 빠진 뒤 DB 조회부터 다시 한다.
    wstring playerName(info.playerName);
    const bool isDummy = (playerName == L"dummy");   // 더미 봇은 전원이 한 캐릭터라 제외
    // 일반 로그인으로 dummy 계정을 직접 넣어도 우회가 되므로 여기서도 막는다(A5.1, D-291)
    if (isDummy && false == GServerConfig.allowDummyLogin)
    {
        OnLoginFailed(session, Protocol::LOGIN_RESULT_NO_ACCOUNT);
        return;
    }
    if (false == isDummy)
    {
        const auto it = _online.find(info.playerUID);
        if (it != _online.end())
        {
            HoldLogin(info.playerUID, it->second, session, accountId, accountPw);
            return;
        }
    }

    int objectId = session->GetId();
    shared_ptr<Player> player = make_shared<Player>(session, (Protocol::PlayerType)info.playerType);

    player->SetId(objectId);
    // PlayerUID·장비(MC3.3)·레벨·HP(A2.5). DB 레벨이 PlayerStatTable에 없으면 거부한다.
    if (false == player->InitFromDb(info, data))
    {
        OnLoginFailed(session, Protocol::LOGIN_RESULT_SERVER_ERROR);
        return;
    }

    player->SetName(playerName);
    player->isDummy = isDummy;
    // 부하 시험 계정(A5.2, D-297): 이름이 [Debug] BotAccountPrefix로 시작하면 배치만 더미처럼(DummyRoom). 외형·아이템·저장은 DB 그대로
    if (false == isDummy && false == GServerConfig.botAccountPrefix.empty())
    {
        const wstring prefix(GServerConfig.botAccountPrefix.begin(), GServerConfig.botAccountPrefix.end());
        player->isLoadBot = playerName.rfind(prefix, 0) == 0;
    }
    if (isDummy)
        DressDummy(*player, objectId);

    if (false == isDummy)
    {
        OnlineEntry entry;
        entry.sessionId = session->GetId();
        entry.session = session;
        _online.emplace(info.playerUID, std::move(entry));
    }

    // 접속자 명부 등록·채널 배정(DG3a). 플레이어를 세션에 넣기 전에 서비스 큐에 넣어야 한다 — 이 플레이어의 입장 요청·정리 신호는
    // 세션에 플레이어가 보인 뒤에만 서비스 큐에 들어가므로, 등록이 항상 그보다 먼저 실행된다.
    GWorld->PushJob(&WorldService::RegisterPlayer, player);

    session->SetPlayer(player);

    Protocol::SC_LOGIN_INFO_PACKET logInPacket;
    logInPacket.mutable_object_info()->CopyFrom(player->GetInfo());
    logInPacket.set_result(Protocol::LOGIN_RESULT_SUCCESS);
    logInPacket.set_map_name(GServerConfig.mapName);   // 클라이언트가 열 레벨(D-185)
    uint16_t dateSize = logInPacket.ByteSizeLong();
    uint16_t packetSize = sizeof(PacketHeader) + dateSize;

    shared_ptr<SendBuffer> sendBuffer = make_shared<SendBuffer>(packetSize);

    PacketHeader* header = reinterpret_cast<PacketHeader*>(sendBuffer->Buffer());
    header->size = packetSize;
    header->id = Protocol::SC_LOGIN;

    logInPacket.SerializeToArray(&header[1], dateSize);
    sendBuffer->Commit(packetSize);

    session->Send(sendBuffer);

    LOG_DEBUG("Lobby", "[{}] - 로그인 성공 및 객체 생성 완료", Utils::WStringToUTF8(playerName));

    // 위의 IsConnected 확인 뒤에 끊겼다면 OnDisconnected는 _currPlayer를 못 보고 지나갔을 수 있다.
    // _currPlayer를 넣은 뒤에 한 번 더 확인해, 어느 쪽이든 한 번은 정리(접속 목록에서 빼는 신호)가 되게 한다.
    if (session->IsConnected() == false)
        session->HandlePlayerGone();
}

void AuthLobby::HoldLogin(int64_t playerUID, OnlineEntry& entry, shared_ptr<GameSession> session, string accountId, string accountPw)
{
    // 대기 중인 연결이 로그인 요청을 또 보냈다: 계속 기다린다.
    if (entry.waiter == session) return;

    // 대기는 캐릭터당 1건. 먼저 기다리던 쪽을 거부하고 나중에 온 쪽을 대기시킨다.
    if (entry.waiter != nullptr)
    {
        Metrics::LoginBusy.fetch_add(1);
        OnLoginFailed(entry.waiter, Protocol::LOGIN_RESULT_BUSY);
    }

    if (false == entry.closing)
    {
        entry.closing = true;
        Metrics::LoginKicks.fetch_add(1);

        // 기존 접속을 끊는다. 이미 끊겨서 저장만 남은 접속(빠른 재접속)이면 보낼 곳이 없다.
        shared_ptr<GameSession> old = entry.session.lock();
        if (old != nullptr && old->IsConnected())
        {
            Protocol::SC_KICK_PACKET kick;
            kick.set_reason(Protocol::KICK_REASON_DUPLICATE_LOGIN);
            old->Send(PacketSerializer::MakeSendBuffer(kick, Protocol::SC_KICK));
            old->ForceClose(L"Kick: duplicate login");
        }
        LOG_DEBUG("Lobby", "PlayerId {} 중복 접속: 세션 {} 을 끊고 세션 {} 의 로그인을 대기시킵니다", playerUID, entry.sessionId, session->GetId());

        // 안전장치: 끝났다는 신호가 오지 않아도 이 시간이 지나면 목록에서 뺀다.
        ReserveJob(static_cast<DWORD>(GServerConfig.loginCloseForceSec) * 1000, &AuthLobby::OnCloseForce, playerUID, entry.sessionId);
    }

    entry.waiter = session;
    entry.waiterId = std::move(accountId);
    entry.waiterPw = std::move(accountPw);
    entry.waitSerial = ++_waitSerial;
    ReserveJob(static_cast<DWORD>(GServerConfig.loginWaitTimeoutSec) * 1000, &AuthLobby::OnLoginWaitTimeout, playerUID, entry.waitSerial);
}

void AuthLobby::StartIdleCheck(weak_ptr<ServerService> service)
{
    if (GServerConfig.idleTimeoutSec <= 0) return;

    _service = std::move(service);
    ReserveJob(IDLE_CHECK_INTERVAL_MS, &AuthLobby::CheckIdleSessions);
}

void AuthLobby::CheckIdleSessions()
{
    // 끊긴 세션은 평소의 Disconnect 경로(GameSession::OnDisconnected → 방 퇴장·저장 → 접속 목록 정리)를 그대로 탄다.
    if (shared_ptr<ServerService> service = _service.lock())
        service->CloseIdleSessions(static_cast<long long>(GServerConfig.idleTimeoutSec) * 1000);

    ReserveJob(IDLE_CHECK_INTERVAL_MS, &AuthLobby::CheckIdleSessions);
}

void AuthLobby::OnPlayerClosed(int64_t playerUID, int sessionId)
{
    const auto it = _online.find(playerUID);
    if (it == _online.end() || it->second.sessionId != sessionId) return;   // 이미 뺐거나 새 접속의 항목

    RemoveOnline(playerUID);
}

void AuthLobby::RemoveOnline(int64_t playerUID)
{
    const auto it = _online.find(playerUID);
    if (it == _online.end()) return;

    shared_ptr<GameSession> waiter = std::move(it->second.waiter);
    string waiterId = std::move(it->second.waiterId);
    string waiterPw = std::move(it->second.waiterPw);
    _online.erase(it);

    // 기다리던 로그인: 이전 접속의 저장이 끝났으므로 DB 조회부터 다시 한다. 결과는 OnLoginSuccess로 돌아온다.
    if (waiter != nullptr && waiter->IsConnected())
    {
        LOG_DEBUG("Lobby", "PlayerId {} 이전 접속 정리 끝: 세션 {} 의 로그인을 다시 진행합니다", playerUID, waiter->GetId());
        GDBWorker->PushDBJob(&DatabaseWorker::TryLogin, waiter, waiterId, waiterPw);
    }
}

void AuthLobby::OnLoginWaitTimeout(int64_t playerUID, uint64_t waitSerial)
{
    const auto it = _online.find(playerUID);
    if (it == _online.end()) return;

    OnlineEntry& entry = it->second;
    if (entry.waiter == nullptr || entry.waitSerial != waitSerial) return;   // 이미 진행됐거나 다른 대기로 바뀜

    // 목록은 그대로 둔다(이전 접속의 저장이 아직 끝나지 않았다). 클라이언트가 잠시 후 다시 로그인한다.
    LOG_WARN("Lobby", "PlayerId {} 로그인 대기 {}초 초과: 세션 {} 에 BUSY 응답 (이전 세션 {} 정리 중)",
        playerUID, GServerConfig.loginWaitTimeoutSec, entry.waiter->GetId(), entry.sessionId);
    Metrics::LoginBusy.fetch_add(1);
    OnLoginFailed(entry.waiter, Protocol::LOGIN_RESULT_BUSY);
    entry.waiter = nullptr;
    entry.waiterId.clear();
    entry.waiterPw.clear();
}

void AuthLobby::OnCloseForce(int64_t playerUID, int sessionId)
{
    const auto it = _online.find(playerUID);
    if (it == _online.end() || it->second.sessionId != sessionId || false == it->second.closing) return;

    // 여기 오면 끝났다는 신호를 내는 경로가 하나 빠진 것이다(또는 DB가 이 시간 넘게 멈춤).
    LOG_ERROR("Lobby", "PlayerId {} 의 이전 접속(세션 {})이 {}초 안에 정리되지 않아 접속 목록에서 강제로 뺍니다",
        playerUID, sessionId, GServerConfig.loginCloseForceSec);
    Metrics::LoginCloseForced.fetch_add(1);
    RemoveOnline(playerUID);
}

void AuthLobby::OnLoginFailed(shared_ptr<GameSession> session, Protocol::LoginResult result)
{
	if (session->IsConnected() == false) return;

	
	LOG_DEBUG("Lobby", "Login Failed: {}", Protocol::LoginResult_Name(result));

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
