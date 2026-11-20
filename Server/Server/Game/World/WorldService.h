#pragma once
#include "Job/JobQueue.h"
#include "Job/Timer.h"
#include "Protocol/Enum.pb.h"

extern shared_ptr<Timer> GTimer;

class Room;
struct MoveRequest;   // Game/Room/Room.h
struct MoveGrant;
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

	// Room 이동 ④(DG3b·DG3c, D-216·D-218): 도착 Room을 정해 출발 Room 잡(Room::OnMoveResolved)으로 알려 준다. 갈 수 없으면 dest 없이 사유를 준다.
	//   ChannelArea = 그 채널의 구역 Room, LevelPortal = 다른 레벨 포털(던전 입구 → 인스턴스를 찾거나 만듦, 던전 출구 → 자기 채널의 사냥터),
	//   Instance = 파티장을 따라 이미 정해진 인스턴스로.
	// 던전으로 보낼 때는 그 인스턴스에 자리를 잡아 둔다(들어오는 중인 사람이 있으면 닫지 않는다) — 가지 못하면 출발 Room이 OnMoveAborted로 돌려준다.
	// player는 넘겨주기만 한다 — 이 서비스는 그 필드를 읽지 않는다(필드는 소유 Room 잡의 것).
	void RequestMove(shared_ptr<Player> player, shared_ptr<Room> source, int channel, MoveRequest request);
	// 잡아 둔 인스턴스 자리를 돌려받는다(출발 Room에서 이동이 취소됨). 여러 번 와도 된다.
	void OnMoveAborted(int sessionId, int instanceId);
	// Room 이동 ⑧: 도착 Room이 알린다 — 명부의 "지금 있는 Room"을 갱신한다. 던전에서 나왔으면 인스턴스에서 빼고(비면 닫음),
	// 던전에 들어왔는데 그 사이 자격을 잃었으면(파티 탈퇴·내보내는 중) 마을로 내보낸다.
	void OnPlayerMoved(int sessionId, shared_ptr<Room> room);
	// 던전 Room이 알린다: 우두머리를 잡았다(D-218). 새 입장을 막고 DungeonClearExitSec 뒤 남은 사람을 마을로 내보낸다.
	void OnDungeonCleared(int instanceId);
	void OnInstanceClearTimeout(int instanceId);

	// ---- 파티(DG2, D-217 — DESIGN_PARTY_DUNGEON §5). 패킷 핸들러가 Room을 거치지 않고 바로 넣는다 ----
	// 초대: targetId(오브젝트 id)가 0이면 targetName으로 찾는다. 파티가 없는 사람 또는 파티장만. 거부는 본인에게 SC_PARTY_RESULT.
	void PartyInvite(shared_ptr<Player> player, int targetId, wstring targetName);
	// 받은 초대에 답한다. 수락하면 초대한 사람의 파티에 들어간다(파티가 없었으면 그 사람이 파티장인 파티가 생김).
	void PartyRespond(shared_ptr<Player> player, bool accept);
	void PartyLeave(shared_ptr<Player> player);
	// 초대 응답 시간(EconomyTable.PartyInviteTimeoutSec)이 지났다. 그 사이 답했거나 새 초대로 바뀌었으면(seq 다름) 아무것도 하지 않는다.
	void OnInviteTimeout(int targetSessionId, int seq);

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
		int partyId = 0;          // 파티 번호(0 = 없음). Room 잡이 읽는 사본은 Player::partyId (NotifyPartyId로 알림)
		int level = 0;            // 로그인 때의 레벨(파티 창 표시용 — 레벨이 오르는 기능이 생기면 갱신 통지를 넣는다)
		long long lastInviteMs = 0;   // 마지막으로 초대를 보낸 시각(steady_clock ms) — 최소 간격 검사
		int instanceId = 0;       // 자리를 잡은 던전 인스턴스(0 = 없음). 들어가는 중·안에 있는 동안 — 나오거나 접속이 끝나면 0 (DG3c)
	};

	// 파티(DG2). 저장하지 않는다. 구성원은 세션 id로 적는다(명부의 키).
	struct Party
	{
		int id = 0;
		int leader = 0;           // 파티장의 세션 id
		vector<int> members;      // 들어온 순서(파티장이 나가면 맨 앞 사람이 파티장)
		int instanceId = 0;       // 이 파티의 던전 인스턴스(0 = 없음, 파티당 하나 — DG3c)
	};
	// 응답을 기다리는 초대. 받는 사람 한 명에 1건(_invites의 키 = 받는 사람의 세션 id).
	struct PendingInvite
	{
		int from = 0;             // 보낸 사람의 세션 id
		int seq = 0;              // 시간 초과 잡이 자기 초대인지 확인하는 순번
	};

	struct Channel
	{
		int id = 0;                // 1부터
		shared_ptr<Room> town;     // 마을 Room. 구역으로 나뉘지 않은 레벨이면 레벨 전체를 맡는 하나뿐인 Room
		shared_ptr<Room> field;    // 사냥터 Room(구역으로 나뉜 레벨만 — DG3b)
		int population = 0;        // 이 채널에 배정된 접속자 수(명부 항목 수)
	};

	// 던전 인스턴스(DG3c, D-218·D-225). id = 그 Room의 roomId. 안에 있는 사람과 들어오는 중인 사람(members)이 0이 되면 바로 닫는다.
	struct Instance
	{
		int id = 0;
		shared_ptr<Room> room;
		int channel = 0;              // 만든 사람의 채널(파티는 같은 채널 사람으로만 이뤄진다)
		int ownerPartyId = 0;         // 주인 = 파티(0이 아니면) 또는 혼자인 플레이어(ownerSessionId). 둘 다 0 = 주인 없음(남은 사람이 나가면 닫힌다)
		int ownerSessionId = 0;
		bool cleared = false;         // 우두머리를 잡았다 — 새로 들어올 수 없다
		bool ejecting = false;        // 클리어 뒤 시간이 다 돼 내보내는 중 — 뒤늦게 로딩을 마친 사람도 내보낸다
		unordered_set<int> members;   // 자리를 잡은 세션(들어오는 중 + 안에 있음)
	};
	// 인스턴스를 만든다. 실패하면 nullptr과 사유(상한 = DUNGEON_FULL, 맵 오류 = NOT_AVAILABLE).
	Instance* CreateInstance(int channel, const string& levelName, Protocol::MoveResult& outResult);
	// 던전 입구 요청의 판정(DESIGN_PARTY_DUNGEON §6-2 표). 통과하면 grant에 도착 Room·인스턴스·함께 갈 파티원을 채운다.
	void ResolveDungeonEnter(int sessionId, const shared_ptr<Room>& source, const string& levelName, MoveGrant& grant);
	void JoinInstance(Instance& instance, int sessionId, RosterEntry& entry);
	// 인스턴스에서 뺀다(여러 번 불러도 됨). 비면 닫는다.
	void LeaveInstance(int sessionId, int instanceId);
	void CloseInstance(int instanceId);
	// 그 세션의 플레이어를 인스턴스 Room에서 마을로 내보내게 한다(Room::EjectPlayer). 아직 도착 전이면 도착 때 OnPlayerMoved가 다시 본다.
	void EjectFromInstance(const RosterEntry& entry, const Instance& instance);

	// 새 채널과 그 Room(구역으로 나뉜 레벨이면 마을·사냥터 둘)을 만든다. 실패하면 nullptr(채널 목록은 그대로).
	Channel* CreateChannel(bool allowBroken);
	Channel* PickChannel();
	void PublishMetrics();

	// 파티 도우미(서비스 잡 안에서만)
	void SendTo(const RosterEntry& entry, shared_ptr<class SendBuffer> buffer);
	void SendPartyResult(int sessionId, Protocol::PacketId request, Protocol::PartyResult result);
	// 파티 전원에게 지금 상태를 보낸다(구성·파티장·있는 곳이 바뀔 때마다).
	void BroadcastPartyState(const Party& party);
	// 그 플레이어의 파티 번호를 바꾸고 소유 Room 잡으로 알린다(Player::partyId — 드랍 소유권 판정용).
	void SetPartyId(int sessionId, RosterEntry& entry, int partyId);
	// 파티에 넣는다. 그 사람에게 와 있던 다른 초대는 지운다(보낸 사람에게 TARGET_IN_PARTY).
	void AddToParty(Party& party, int sessionId, RosterEntry& entry);
	// 파티에서 뺀다(탈퇴·접속 해제 공통): 파티장이면 위임, 한 명만 남으면 해산. closed = 접속이 끝남(본인에게는 알리지 않음).
	void RemoveFromParty(int sessionId, RosterEntry& entry, bool closed);
	// 그 세션이 보냈거나 받은 대기 초대를 지운다(접속 해제). 상대에게 NO_TARGET.
	void DropInvitesOf(int sessionId);

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

	// 파티(DG2)
	unordered_map<int, Party> _parties;                 // 파티 번호 → 파티
	unordered_map<int, PendingInvite> _invites;         // 받는 사람의 세션 id → 대기 초대
	int _nextPartyId = 0;
	int _inviteSeq = 0;
	// 던전 인스턴스(DG3c). 키 = 인스턴스 id(= roomId)
	unordered_map<int, Instance> _instances;
	// 시작 레벨의 포털 마커 id → 그 마커의 구역(던전 출구로 나온 사람이 갈 Room을 정한다). Init에서 채운다
	unordered_map<int, string> _worldPortalAreas;

	uint32_t _partySeq = 0;                             // Room에 알리는 파티 번호 변경의 순번(늦게 도착한 옛 알림을 거른다)
};
