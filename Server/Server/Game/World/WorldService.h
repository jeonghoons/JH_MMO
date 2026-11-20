#pragma once
#include "Job/JobQueue.h"
#include "Job/Timer.h"

extern shared_ptr<Timer> GTimer;

class Room;
class Player;
class GameSession;

// 전역 서비스(DG3a, D-215 — DESIGN_PARTY_DUNGEON §3): 접속자 명부, 채널과 그 Room, 채널 자동 배정.
// AuthLobby와 같은 꼴이다 — 자체 잡 큐를 갖고, 아래 상태는 모두 이 서비스의 잡 안에서만 읽고 쓴다(밖에서는 PushJob).
// 중복 접속 차단 목록(AuthLobby::_online, A2.7)과는 따로 둔다(D-222): 이 명부는 더미 봇을 포함하고 채널·있는 Room을 안다.
//
// 흐름
//   로그인: AuthLobby::OnLoginSuccess → RegisterPlayer(채널 배정, 인원 +1 — 그 채널이 없으면 Room까지 만든다)
//   입장:   CS_ENTER_ROOM → EnterWorld(명부의 채널 → 그 채널의 Room 잡으로 PlayerEnterRoom)
//   정리:   Room::NotifyClosedIfDone / GameSession::HandlePlayerGone → OnPlayerClosed(명부 제거, 인원 -1 → 로비의 _online 정리로 이어 줌)
// 잡 큐는 넣은 순서대로 실행하므로 한 플레이어의 RegisterPlayer는 항상 그 플레이어의 EnterWorld·OnPlayerClosed보다 먼저 실행된다
// (로비가 플레이어를 세션에 넣기 전에 RegisterPlayer를 넣는다). Room 생성(맵 로딩)도 이 큐 안에서 하므로 그동안의 입장 요청은 순서대로 기다린다.
class WorldService : public enable_shared_from_this<WorldService>
{
public:
	WorldService(HANDLE iocpHandle);
	~WorldService();

	template<typename... Arguments>
	void PushJob(void(WorldService::* memFunc)(Arguments...), Arguments... args)
	{
		_jobQueue->Push(shared_from_this(), memFunc, std::forward<Arguments>(args)...);
	}

	template<typename... Arguments>
	void ReserveJob(DWORD milliseconds, void(WorldService::* memFunc)(Arguments...), Arguments... args)
	{
		GTimer->Reserve(milliseconds, _jobQueue, shared_from_this(), memFunc, std::forward<Arguments>(args)...);
	}

	// 서버 시작 때 한 번(워커 스레드를 만들기 전, 메인 스레드에서 직접): 채널 1과 그 Room을 만든다.
	// 맵 리소스 오류는 시작 로그에 남고 서버는 뜬다(기존 동작). WorldTable 행이 없을 때만 false.
	bool Init();

public:
	// ---- 아래는 서비스 잡으로만 부른다 ----

	// 로그인 성공: 명부에 넣고 채널을 배정한다(Player::channel). 규칙 = 인원이 ChannelCapacity 미만인 가장 낮은 채널, 모두 찼으면 새 채널.
	// 새 채널의 Room을 만들지 못하면 ERROR를 남기고 인원이 가장 적은 기존 채널에 정원을 넘겨 넣는다(D-222).
	void RegisterPlayer(shared_ptr<Player> player);

	// 입장 요청: 명부에 있는(= 로그인했고 아직 정리되지 않은) 플레이어를 그 채널의 Room에 넣는다. 두 번째 요청은 버린다.
	void EnterWorld(shared_ptr<Player> player);

	// 접속이 끝나 정리됐다(방에 없음 + 세션 끊김 + DB 작업 0건, 또는 방에 들어가기 전에 끊김): 명부에서 빼고 채널 인원을 줄인다.
	// 같은 신호가 두 번 오거나 명부에 없는 세션이어도 된다(세션 id는 다시 쓰이지 않는다). 더미 봇이 아니면 로비에 이어서 알린다.
	void OnPlayerClosed(int sessionId, int64_t playerUID, bool isDummy);

	// Room 이동 ④(DG3b, D-216): 그 채널에서 구역 destArea의 Room을 찾아 출발 Room 잡(Room::OnMoveResolved)으로 알려 준다. 없으면 nullptr.
	// player는 넘겨주기만 한다 — 이 서비스는 그 필드를 읽지 않는다(필드는 소유 Room 잡의 것).
	void RequestMove(shared_ptr<Player> player, shared_ptr<Room> source, int channel, string destArea);
	// Room 이동 ⑧: 도착 Room이 알린다 — 명부의 "지금 있는 Room"을 갱신한다.
	void OnPlayerMoved(int sessionId, shared_ptr<Room> room);

private:
	struct RosterEntry
	{
		int64_t playerUID = 0;
		wstring name;
		weak_ptr<GameSession> session;
		int channel = 0;
		weak_ptr<Room> room;      // 지금 있는 Room(입장 전에는 비어 있음). Room 이동(DG3b)에서 도착 Room이 갱신한다
		bool isDummy = false;
		bool entered = false;     // 입장 요청을 처리했다(두 번째 요청을 거른다)
		int partyId = 0;          // DG2
	};

	struct Channel
	{
		int id = 0;                // 1부터
		shared_ptr<Room> town;     // 마을 Room. 구역으로 나뉘지 않은 레벨이면 레벨 전체를 맡는 하나뿐인 Room
		shared_ptr<Room> field;    // 사냥터 Room(구역으로 나뉜 레벨만 — DG3b)
		int population = 0;        // 이 채널에 배정된 접속자 수(명부 항목 수)
	};

	// 새 채널과 그 Room(구역으로 나뉜 레벨이면 마을·사냥터 둘)을 만든다. 실패하면 nullptr(채널 목록은 그대로).
	Channel* CreateChannel(bool allowBroken);
	Channel* PickChannel();
	void PublishMetrics();

private:
	shared_ptr<JobQueue> _jobQueue;
	HANDLE _iocpHandle;

	vector<Channel> _channels;                          // [i] = 채널 i+1. 한번 만든 채널은 서버가 꺼질 때까지 둔다
	bool _levelSplit = false;                           // 레벨의 마커에 town·field 구역이 있다 → 채널마다 마을·사냥터 Room을 따로 만든다
	int _dummySeq = 0;                                  // [Debug] DummyRoom=split 의 번갈아 넣기 순번
	bool _channelCreateFailed = false;                  // 새 채널 생성에 실패했다 — 로그인마다 다시 시도하지 않는다
	int _nextRoomId = 0;

	// 접속자 명부. 키 = 세션 id(= 오브젝트 id) — 더미 봇은 전원이 한 PlayerUID라 UID를 키로 쓸 수 없다.
	unordered_map<int, RosterEntry> _roster;
	// 보조 색인(더미 봇 제외): 파티 초대·귓속말 등에서 쓴다(DG2)
	unordered_map<int64_t, int> _uidIndex;              // PlayerUID → 세션 id
	unordered_map<wstring, int> _nameIndex;             // 캐릭터 이름 → 세션 id
};
