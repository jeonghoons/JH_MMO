#pragma once
#include "pch.h"
#include "Job/JobQueue.h"
#include "Network/ServerService.h"
#include "Job/Timer.h"
#include "Game/World/GameMap.h"

constexpr int MAX_ROOM_CAPACITY = 100;
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

	// 슬롯(0 = 기본 공격)의 스킬을 시전한다. 사거리/대상/방향은 서버가 검증·보정하며, 실패하면 아무 일도 일어나지 않는다.
	void CastSkill(shared_ptr<Character> caster, int slot, int targetId, float aimYaw, float aimX, float aimY);
	void ExecuteSkillHit(int attackerId, int skillId, int targetId, float aimYaw, float aimX, float aimY);
	void ApplyDelayedDamage(int targetId, int attackerId, int damage);

	// 지면 장판 (HitShape::Zone)
	void CreateZone(shared_ptr<Character> attacker, const SkillData& skill, float x, float y);
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

	atomic<int> _playerCount = 0;

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

