#pragma once
#include "pch.h"
#include "JobQueue.h"
#include "ServerService.h"
#include "Timer.h"
#include "GameMap.h"

constexpr int MAX_ROOM_CAPACITY = 1000;
constexpr int MAX_CHAT_LEN = 100;

class Session;
class SendBuffer;
class GameObject;
class Character;
class Player;
class Monster;

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
	// °ø¿ë
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
	optional<Protocol::PositionInfo> GetObjectPosition(int objectId) const;
	void UpdateView(shared_ptr<Character> subjectChar, const ViewUpdate& result);

	// Player
	void PlayerEnterRoom(shared_ptr<Player> player);	
	void PlayerLeaveRoom(shared_ptr<Player> player);
	void PlayerMove(shared_ptr<Player> player, Protocol::PositionInfo position, bool force);
	void Broadcast(shared_ptr<SendBuffer> sendBuffer);
	void BroadcastAOI(shared_ptr<class Character> viewableObj, shared_ptr<SendBuffer> sendBuffer, bool sendToSelf = false);

	void PlayerChat(shared_ptr<Player> player, wstring msg);

	void CharacterAttack(shared_ptr<Character> attacter, int skillId, int targetId);
	void ExecuteSkillHit(int attackerId, int skillId, int targetId);
	void ApplyDelayedDamage(int targetId, int attackerId, int damage);
	// Npc
	void NpcEnterRoom(shared_ptr<Monster> monster);
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

	int NumPlayers() { return static_cast<int>(_players.size()); }

private:

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

public:

	RWLock				_lock;

	unordered_map<int, shared_ptr<Room>> _rooms;

	HANDLE _iocpHandle;
};

