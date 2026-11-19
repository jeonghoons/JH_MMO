#pragma once
#include "Data/ServerData.h"
#include "Job/JobQueue.h"
#include "Job/Timer.h"

extern shared_ptr<Timer> GTimer;

class AuthLobby : public enable_shared_from_this<AuthLobby>
{
public:
	AuthLobby(HANDLE iocpHandle);
	~AuthLobby();

	template<typename... Arguments>
	void PushJob(void(AuthLobby::* memFunc)(Arguments...), Arguments... args)
	{
		_jobQueue->Push(shared_from_this(), memFunc, std::forward<Arguments>(args)...);
	}

	template<typename... Arguments>
	void ReserveJob(DWORD milliseconds, void(AuthLobby::* memFunc)(Arguments...), Arguments... args)
	{
		GTimer->Reserve(milliseconds, _jobQueue, shared_from_this(), memFunc, std::forward<Arguments>(args)...);
	}

public:
	// accountId·accountPw: 같은 캐릭터의 이전 접속이 정리되기를 기다린 뒤 DB 조회부터 다시 하기 위해 받는다(A2.7).
	void OnLoginSuccess(shared_ptr<GameSession> session, DB_PlayerInfo info, DB_PlayerData data, string accountId, string accountPw);
	void OnLoginFailed(shared_ptr<GameSession> session, Protocol::LoginResult result);
	void OnSignUpResult(shared_ptr<GameSession> session, bool success);

	// 끊긴 플레이어의 퇴장 처리와 진행 중이던 DB 작업이 모두 끝났다(A2.7). 접속 목록에서 빼고, 기다리던 로그인이 있으면 DB 조회부터 다시 한다.
	// sessionId가 목록 항목과 같을 때만 뺀다 — 신호가 두 번 오거나 늦게 와도 새 접속의 항목을 지우지 않는다.
	// 보내는 곳: GameSession::HandlePlayerGone(방에 들어가기 전), Room::NotifyClosedIfDone(그 밖 전부).
	void OnPlayerClosed(int64_t playerUID, int sessionId);

	// 유휴 세션 검사(A5.0, D-220)를 시작한다: IDLE_CHECK_INTERVAL_MS마다 [Network] IdleTimeoutSec 동안 수신이 없던 연결을 끊는다.
	// 서버 시작 때 한 번 부른다. IdleTimeoutSec이 0이면 아무것도 하지 않는다.
	void StartIdleCheck(weak_ptr<class ServerService> service);

private:
	// 접속 중 캐릭터 목록(A2.7, D-158). 로비 잡 안에서만 읽고 쓴다. 더미 봇(전원이 한 캐릭터)은 넣지 않는다.
	struct OnlineEntry
	{
		int sessionId = 0;               // 이 캐릭터로 로그인한 세션 (세션 id는 다시 쓰이지 않는다)
		weak_ptr<GameSession> session;
		bool closing = false;            // 새 로그인이 와서 끊었고, 퇴장·마지막 저장이 끝나기를 기다리는 중

		// 대기 중인 로그인 (캐릭터당 1건)
		shared_ptr<GameSession> waiter;
		string waiterId, waiterPw;
		uint64_t waitSerial = 0;         // 시간 초과 잡이 자기 대기인지 확인하는 번호
	};

	// 목록에 있는 캐릭터로 새 로그인이 왔다: 기존 접속을 끊고(처음 한 번) 이 로그인을 대기시킨다.
	void HoldLogin(int64_t playerUID, OnlineEntry& entry, shared_ptr<GameSession> session, string accountId, string accountPw);
	// 목록에서 빼고, 대기 중인 로그인이 있으면 DB 조회부터 다시 실행한다.
	void RemoveOnline(int64_t playerUID);

	// 예약 잡: 실행될 때 아직 같은 대기·같은 접속인지 다시 확인한다.
	void OnLoginWaitTimeout(int64_t playerUID, uint64_t waitSerial);
	void OnCloseForce(int64_t playerUID, int sessionId);

	// 예약 잡: 유휴 세션을 끊고 자신을 다시 예약한다.
	void CheckIdleSessions();

private:
	static constexpr DWORD IDLE_CHECK_INTERVAL_MS = 5000;
	weak_ptr<class ServerService> _service;
	shared_ptr<JobQueue> _jobQueue;
	unordered_map<int64_t, OnlineEntry> _online;
	uint64_t _waitSerial = 0;
};

