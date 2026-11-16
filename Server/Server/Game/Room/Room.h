#pragma once
#include "pch.h"
#include "Job/JobQueue.h"
#include "Network/ServerService.h"
#include "Job/Timer.h"
#include "Game/World/GameMap.h"
#include "Data/ServerData.h"

constexpr int MAX_ROOM_CAPACITY = 100;
constexpr const char* ROOM_MAP_NAME = "ParagonSample"; // 모든 방이 쓰는 맵(맵 리소스 파일 이름 = WorldTable.MapName)
constexpr int MAX_CHAT_LEN = 100;               // 채팅 한 줄 최대 글자 수(UTF-16 문자 기준)
constexpr long long CHAT_MIN_INTERVAL_MS = 500;  // 같은 플레이어의 채팅 최소 간격(도배 방지)

class Session;
class SendBuffer;
class GameObject;
class Character;
class Player;
class Monster;
struct SkillData;

class Room : public enable_shared_from_this<Room>
{
public:
	Room(shared_ptr<Timer> timer, HANDLE iocpHandle);

	~Room();

	void InitRoom();

	template<typename... Arguments>
	void PushJob(void(Room::* memFunc)(Arguments...), Arguments... args)
	{

		_jobQueue->Push(shared_from_this(), memFunc, std::forward<Arguments>(args)...);
	}

	template<typename... Arguments>
	void ReserveJob(DWORD ectime, void(Room::* memFunc)(Arguments...), Arguments... args)
	{
		_timer->Reserve(ectime, _jobQueue, shared_from_this(), memFunc, std::forward<Arguments>(args)...);
	}
	void Update(long long elapsedTime);
	// 공용
	bool AddObject(shared_ptr<GameObject> object);
	bool RemoveObject(int objectId);
	shared_ptr<GameObject> GetGameObject(int objectId) const
	{
		auto it = _objects.find(objectId);
		if (it == _objects.end()) {
			return nullptr;
		}
		return it->second;
	}
	void UpdateView(shared_ptr<Character> subjectChar, const ViewUpdate& result);

	// Player
	void PlayerEnterRoom(shared_ptr<Player> player);	
	void PlayerLeaveRoom(shared_ptr<Player> player);
	// 클라이언트가 예측한 위치를 검증한다(서버 권위 이동, S1). 통과하면 저장·방송, 아니면 본인에게 보정 위치를 보낸다.
	void PlayerMove(shared_ptr<Player> player, Protocol::PositionInfo position, uint32_t correctionSeq);
	void Broadcast(shared_ptr<SendBuffer> sendBuffer);
	void BroadcastAOI(shared_ptr<class Character> viewableObj, shared_ptr<SendBuffer> sendBuffer, bool sendToSelf = false);

	void PlayerChat(shared_ptr<Player> player, wstring msg);
	void PlayerPing(shared_ptr<Player> player, long long clientTime);
	// 장착 요청(MC3.2·A3.3)을 검증한다. itemDbId = 내 인벤토리의 행(0 = 해제). 통과하면 DB 트랜잭션을 보내고(StartItemOp),
	// 성공 결과가 돌아온 뒤(OnItemOpDone) 메모리에 반영해 SC_CHANGE_EQUIP을 AOI 전체(본인 포함)에 보낸다.
	// 거부·DB 실패는 SC_CHANGE_EQUIP_RESULT를 본인에게만.
	void PlayerChangeEquip(shared_ptr<Player> player, Protocol::EquipPart part, int64_t itemDbId);
	// 개발 지급 명령(A3.3, [Debug] DevCommands). itemId 0 = 골드. 다른 아이템 작업과 같은 경로를 탄다.
	void PlayerDevGive(shared_ptr<Player> player, int itemId, int64_t count);
	// 인벤토리 칸 옮기기(A6.1): 대상 칸이 비어 있으면 옮기고, 다른 아이템이 있으면 맞바꾼다. 칸 위치도 DB가 권위라 itemOp 경로를 탄다.
	void PlayerMoveItem(shared_ptr<Player> player, int64_t itemDbId, int toSlot);
	// 아이템 작업의 DB 결과(A3.3, D-153). 성공이고 플레이어가 아직 방에 있으면 메모리·패킷에 반영한다.
	// 방을 나갔으면 반영을 건너뛴다(DB에는 이미 들어갔고 다음 로그인 때 읽는다). 끝에서 NotifyClosedIfDone을 부른다.
	void OnItemOpDone(shared_ptr<Player> player, DB_ItemOp op, DB_ItemOpResult result);
	// 진행 상황(레벨·경험치·HP)을 DB에 저장한다(A2.5). 골드는 아이템 트랜잭션에서 증감한다(A3.3). DB에 있는 값과 다를 때만 보낸다. 더미 봇은 저장하지 않는다
	// (전원이 dummy 계정 한 행을 쓴다). 주기(UpdateSaves)와 퇴장 때 부르고, 방을 나간 플레이어도 받는다.
	void RequestProgressSave(shared_ptr<Player> player);
	void OnProgressSaved(shared_ptr<Player> player, DB_PlayerProgress saved, bool success);
	// 1초에 한 번: 주기 저장 시각(ServerConfig Database.PlayerSaveIntervalSec)이 된 플레이어를 저장한다.
	void UpdateSaves(long long nowMs);
	// 중복 접속 차단(A2.7): 끊긴 플레이어가 방에 없고 DB에 가 있는 작업이 0건이면 로비에 알려 접속 목록에서 빼게 한다(플레이어당 한 번).
	// 플레이어가 방을 떠나는 경로와 그 플레이어의 DB 작업 결과를 받는 곳 **모두**에서 불러야 한다 — 빠지면 그 캐릭터는
	// 다음 로그인이 LoginCloseForceSec만큼 막힌다. 새 저장 종류·아이템 작업을 추가하면 여기 조건과 결과 처리 끝에 함께 넣는다.
	void NotifyClosedIfDone(const shared_ptr<Player>& player);
	// 스킬 조준 켜기/끄기(MC4.2d-1, DECISIONS 2026-11-09). 켜기 성공 = AOI 전체(본인 포함) 방송, 거부 = 본인에게만 false + seq,
	// 끄기 = 조준 중일 때만 해제하고 본인 제외 방송(본인 클라이언트는 이미 껐다).
	void PlayerSkillAim(shared_ptr<Player> player, int slot, bool active, uint32_t seq);
	// 부활 요청(A2.4, D-149). 죽어 있고 WorldTable.ReviveDelaySec가 지났을 때만 받는다(그 밖은 조용히 버림).
	// 입장 때 배치된 자리로 옮기고 HP를 채운 뒤 SC_REVIVE를 AOI 전체(본인 포함)에 보낸다.
	void PlayerRevive(shared_ptr<Player> player);

	// 슬롯(0 = 기본 공격)의 스킬을 시전한다. 사거리/대상/방향은 서버가 검증·보정하며, 실패하면 아무 일도 일어나지 않는다.
	void CastSkill(shared_ptr<Character> caster, int slot, int targetId, float aimYaw, float aimX, float aimY);
	// damage: 시전 순간에 확정한 피해(BaseDamage + 시전자 공격력)
	void ExecuteSkillHit(int attackerId, int skillId, int targetId, float aimYaw, float aimX, float aimY, int damage);
	void ApplyDelayedDamage(int targetId, int attackerId, int damage);

	// 지면 장판 (HitShape::Zone)
	void CreateZone(shared_ptr<Character> attacker, const SkillData& skill, float x, float y, int damage);
	// Npc
	void NpcEnterRoom(shared_ptr<Monster> monster);
	void SpawnMonster(int spawnIndex);   // 스폰 마커 반경 안의 임의 위치에 몬스터 한 마리를 만든다.
	void RemoveMonster(int monsterId);   // 사망한 몬스터를 월드에서 제거하고 리스폰을 예약한다.
	void NPCMove(shared_ptr<Monster> monster);
	int MonsterIdGenerator()
	{
		static atomic<int> _midGenerator = 100000;
		return ++_midGenerator;
	}
	NavmeshManager* GetNavManager() { return _gameMap.GetNavManager(); }
	const GameMap& GetGameMap() const { return _gameMap; }
	shared_ptr<Player> Id2Player(int pId);
	shared_ptr<Monster> Id2Monster(int mId);

	// 입장 예약(정원 확인 + 자리 확보)을 원자적으로 한다. 입장 처리(PlayerEnterRoom)는 JobQueue에서 나중에 실행되므로
	// _players.size()로 정원을 판단하면 동시에 들어오는 플레이어가 정원을 넘길 수 있다.
	bool TryReserveSlot()
	{
		int count = _playerCount.load();
		while (count < MAX_ROOM_CAPACITY)
		{
			if (_playerCount.compare_exchange_weak(count, count + 1))
				return true;
		}
		return false;
	}
	void ReleaseSlot() { _playerCount.fetch_sub(1); }
	int NumPlayers() const { return _playerCount.load(); }

private:
	struct SkillZone
	{
		int attackerId = 0;
		float x = 0.f, y = 0.f;
		float radius = 0.f;
		int damage = 0;
		int tickMs = 1000;
		chrono::steady_clock::time_point endTime;
		unordered_map<int, chrono::steady_clock::time_point> nextHit;   // 대상별 다음 피해 가능 시각
	};
	vector<SkillZone> _zones;
	void UpdateZones();
	// AimMaxMs가 지난 조준을 해제한다(Update마다). 본인에게는 seq를 실어, 다른 사람에게는 seq 없이 보낸다.
	void UpdateAims(long long nowMs);

	// ---- 아이템 작업 공통(A3.3) ----
	// 이 플레이어 객체가 지금 이 방에 있는가(같은 id를 다른 플레이어가 다시 쓸 수 있으므로 포인터로 비교).
	bool IsInRoom(const shared_ptr<Player>& player) const;
	// 아이템 요청의 공통 검사: 방 안 → 더미 봇 아님 → 요청 간격 → 진행 중인 작업 없음. 통과하면 ITEM_RESULT_SUCCESS.
	Protocol::ItemResult CheckItemRequest(const shared_ptr<Player>& player);
	// itemOp를 켜고 DB 작업을 보낸다. 결과는 OnItemOpDone으로 돌아온다.
	void StartItemOp(const shared_ptr<Player>& player, DB_ItemOp op);

	atomic<int> _playerCount = 0;
	int _tickCount = 0;   // Update 횟수. 1초(10틱)마다 시야 인원 지표·시야 감사를 하는 데 쓴다

	// [Debug] 부하·검증 시험용
	bool PickDummyZonePosition(Protocol::PositionInfo& out);   // DummySpawnZone 안의 내비메시 위치
	void AuditViews();                                         // ViewAudit: 시야 목록을 실제 거리와 비교

	unordered_map<int, shared_ptr<GameObject>> _objects;
	unordered_map<int, shared_ptr<Player>> _players;
	unordered_map<int, shared_ptr<Monster>> _monsters;

	GameMap				_gameMap;

	shared_ptr<JobQueue> _jobQueue;

	shared_ptr<Timer>		_timer;
};

class RoomManager
{
public:
	RoomManager(HANDLE iocpHandle) : _iocpHandle(iocpHandle) {}
public:
	shared_ptr<Room> CreateRoom();
	void Remove(int roomId);
	void Remove(shared_ptr<Room> room);

	void EnterPlayer(shared_ptr<Player> player);

	int IdGenerator();

private:
	// 정원이 남은 기존 방에 입장 예약을 시도한다.
	bool TryEnterExisting(shared_ptr<Player> player);

public:

	RWLock				_lock;
	mutex				_createMutex; // 방 생성(맵 로딩 포함)은 한 번에 하나만, _lock 밖에서 수행한다.

	unordered_map<int, shared_ptr<Room>> _rooms;

	HANDLE _iocpHandle;
};

