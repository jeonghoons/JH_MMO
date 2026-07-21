#include "pch.h"
#include "Room.h"
#include "Session.h"
#include "PacketHandler.h"
#include "Player.h"
#include "Monster.h"
#include "CombatProcessor.h"
#include "ServerData.h"

Room::Room(shared_ptr<Timer> timer, HANDLE iocpHandle) : _timer(timer), _jobQueue(make_shared<JobQueue>(iocpHandle))
{
}
Room::~Room() = default;

void Room::InitRoom()
{
	if (!_gameMap.LoadMapData("ParagonSample"))
	{
		cout << "[Room] 맵 초기화(LoadMapData) 중 일부 문제가 발생했습니다." << endl;
	}
	_gameMap.Init(weak_from_this());

	const auto& spawnPoints = _gameMap.GetSpawnPoints();
	if (spawnPoints.empty())
	{
		cout << "[Room] 스폰 포인트가 없어 몬스터를 생성할 수 없습니다!" << endl;
	}
	else
	{
		// for(int k = 0; k < 100; ++k){
		for (int i = 1; i < spawnPoints.size(); ++i) {
			shared_ptr<Monster> monster = make_shared<Monster>();
			monster->SetId(MonsterIdGenerator());

			Protocol::PositionInfo spawnPos;
			spawnPos.set_x(spawnPoints[i].X);
			spawnPos.set_y(spawnPoints[i].Y);
			spawnPos.set_z(spawnPoints[i].Z);
			spawnPos.set_yaw(spawnPoints[i].Yaw);
			monster->SetPosition(spawnPos);
			NpcEnterRoom(monster);
		}
		
	}
	
	long long currentTick = chrono::duration_cast<chrono::milliseconds>(chrono::steady_clock::now().time_since_epoch()).count();
	Update(currentTick);
}

void Room::Update(long long elapsedTime)
{
	// cout << "Update Room" << endl;
	long long currentTick = chrono::duration_cast<chrono::milliseconds>(chrono::steady_clock::now().time_since_epoch()).count();
	ReserveJob(100, &Room::Update, currentTick);

	float dt = (currentTick - elapsedTime) / 1000.f;
	// if (dt <= 0.f || dt > 0.5f) dt = 0.1f;

	for (const auto& object : _objects) {
		object.second->Update(dt);
	}
}

bool Room::AddObject(shared_ptr<GameObject> object)
{
	int objectId = object->GetId();

	auto [it, success] = _objects.emplace(objectId, object);
	if (!success) {
		cout << "Error" << endl;
		return false;
	}
	object->SetOwnerRoom(shared_from_this());

	const auto& spawnPoints = _gameMap.GetSpawnPoints();
	if (object->GetType() == Protocol::ObjectType::OBJECT_TYPE_PLAYER) {
		Protocol::PositionInfo spawnPos;
		spawnPos.set_x(spawnPoints[0].X);
		spawnPos.set_y(spawnPoints[0].Y);
		spawnPos.set_z(spawnPoints[0].Z);
		object->SetPosition(spawnPos);
				
		shared_ptr<Player> dummyPlayer = static_pointer_cast<Player>(object);
		if (dummyPlayer->isDummy) {
			dummyPlayer->SetPosition(_gameMap.GetNavManager()->GetRandomPosition());
		}
	}

	Protocol::PositionInfo currentPos = object->GetPosition();

	if (_gameMap.IsOutOfBounds(currentPos))
	{
		cout << "[경고] 오브젝트 스폰 위치가 맵(NavMesh) 바깥이거나 바닥이 없습니다! ID: " << objectId;
		cout << ", " << currentPos.x() << ", " << currentPos.y() << ", " << currentPos.z() << endl;
		
	}

	object->SetPosition(currentPos);

	return true;
}

bool Room::RemoveObject(int objectId)
{
	return _objects.erase(objectId) > 0;
}

void Room::PlayerEnterRoom(shared_ptr<Player> player)
{
	if (false == AddObject(player)) {
		return;
	}

	_players.emplace(player->GetId(), player);
	
	cout << "Client[" << player->GetId() << "] Enter Room" << endl;

	shared_ptr<SendBuffer> objectAddBuffer = PacketSerializer::MAKE_SC_ADD_OBJECT(player);
	if (auto session = player->GetSession()) {
		session->Send(objectAddBuffer);
	}

	if (auto session = player->GetSession()) {
		session->Send(PacketSerializer::MAKE_SC_ADD_OBJECT(player));
	}

	// 맵 등록 및 시야 동기화
	ViewUpdate result = _gameMap.EnterMap(player->GetId(), player->GetPosition());
	if (!result.entered.empty()) {
		UpdateView(player, result);
	}
}

void Room::PlayerLeaveRoom(shared_ptr<Player> player)
{
	cout << "Player[" << player->GetId() << "] Leave" << endl;

	int playerId = player->GetId();

	ViewUpdate result = _gameMap.LeaveMap(playerId);
	if (!result.leaved.empty()) {
		UpdateView(player, result);
	}


	if (false == RemoveObject(playerId)) {
		cout << "Error" << endl;
	}

	_players.erase(playerId);

}

void Room::PlayerMove(shared_ptr<Player> player, Protocol::PositionInfo position, bool force)
{
	if (nullptr == Id2Player(player->GetId()))
		return;

	Protocol::PositionInfo currentPos = player->GetPosition();

	if (player->isDummy) {
		
		player->Move({ currentPos.x(), currentPos.y(), currentPos.z()});
	}

	if (!_gameMap.CanMove(currentPos, position))
	{
		if (shared_ptr<Session> session = player->GetSession()) {
			shared_ptr<SendBuffer> correctionBuffer = PacketSerializer::MAKE_SC_MOVE_OBJECT(player);
			session->Send(correctionBuffer);
		}
		// cout << "비정상 이동 차단" << endl;
		return;
	}

	player->SetPosition(position);

	shared_ptr<SendBuffer> playerMoveBuffer = PacketSerializer::MAKE_SC_MOVE_OBJECT(player);
	BroadcastAOI(static_pointer_cast<Character>(player), playerMoveBuffer);

	ViewUpdate result = _gameMap.UpdateMap(player->GetId(), player->GetPosition());

	if (!result.entered.empty() || !result.leaved.empty()) {
		UpdateView(player, result);
	}

	// printf("Player[%d] - (%f, %f) Yaw: %f)\n", player->GetId(), position.x, position.y, position.yaw);
}

void Room::Broadcast(shared_ptr<SendBuffer> sendBuffer)
{
	for (const auto& p : _players)
	{
		if (auto session = p.second->GetSession())
			session->Send(sendBuffer);
	}
}

void Room::BroadcastAOI(shared_ptr<Character> viewableObj, shared_ptr<SendBuffer> sendBuffer, bool sendToSelf)
{
	if (sendToSelf && viewableObj->GetType() == Protocol::ObjectType::OBJECT_TYPE_PLAYER) {
		shared_ptr<Player> player = static_pointer_cast<Player>(viewableObj);
		if (shared_ptr<Session> session = player->GetSession())
			session->Send(sendBuffer);
	}


	vector<int>& currentView = viewableObj->_viewList;

	for (const int& id : currentView) {
		shared_ptr<Player> target_player = Id2Player(id);
		if (target_player == nullptr) continue;
		if (shared_ptr<Session> session = target_player->GetSession()) {
			session->Send(sendBuffer);
		}
	}
}

void Room::PlayerChat(shared_ptr<Player> player, wstring msg)
{
	int chatLen = msg.length();
	if (chatLen >= MAX_CHAT_LEN) chatLen = MAX_CHAT_LEN - 1;
	unsigned short packetSize = sizeof(PacketHeader) + sizeof(int) + ((chatLen + 1) * sizeof(wchar_t));
	
	Protocol::SC_CHAT_PACKET packet;

	shared_ptr<SendBuffer> chatBuffer = make_shared<SendBuffer>(packetSize);
	
	// Broadcast(chatBuffer);
}

void Room::CharacterAttack(shared_ptr<Character> attacter, int skillId, int targetId)
{
	if (attacter == nullptr) return;

	if (attacter->Attack(skillId) == false) return;

	if (attacter->IsHit()) return;

	const SkillData* skill = DataManager::GetSkillData(skillId);
	if (!skill) return;

	BroadcastAOI(attacter, PacketSerializer::MAKE_SC_ATTACK(attacter->GetId(), skillId, targetId));

	ReserveJob(skill->hitDelayMs, &Room::ExecuteSkillHit, attacter->GetId(), skillId, targetId);
}

void Room::ExecuteSkillHit(int attackerId, int skillId, int targetId)
{
	auto attackerObj = GetGameObject(attackerId);
	if (attackerObj == nullptr) return;

	auto attackerChar = std::static_pointer_cast<Character>(attackerObj);

	CombatProcessor::ProcessSkillHit(shared_from_this(), attackerChar, skillId);
}

void Room::ApplyDelayedDamage(int targetId, int attackerId, int damage)
{
	auto targetObj = GetGameObject(targetId);
	auto attackerObj = GetGameObject(attackerId);

	if (targetObj == nullptr) return;

	auto targetChar = std::static_pointer_cast<Character>(targetObj);

	if (!targetChar->GetStat().IsDead())
	{
		std::cout << "[Room] Target[" << targetId << "] Dmg: " << damage <<  std::endl;
		targetChar->OnDamaged(damage, static_pointer_cast<Character>(attackerObj));
	}
}

void Room::NpcEnterRoom(shared_ptr<Monster> monster)
{
	if (false == AddObject(monster)) {
		return;
	}

	_monsters.emplace(monster->GetId(), monster);

	ViewUpdate result = _gameMap.EnterMap(monster->GetId(), monster->GetPosition());
	if (!result.entered.empty()) {
		UpdateView(monster, result);
	}
}

void Room::NPCMove(shared_ptr<Monster> monster)
{
	if (GetGameObject(monster->GetId()) == nullptr)
		return;

	ViewUpdate result = _gameMap.UpdateMap(monster->GetId(), monster->GetPosition());
	if (!result.entered.empty() || !result.leaved.empty()) {
		UpdateView(monster, result);
	}
}

std::optional<Protocol::PositionInfo> Room::GetObjectPosition(int objectId) const
{
	if (shared_ptr<GameObject> object = GetGameObject(objectId)) {
		return object->GetPosition();
	}

	return nullopt;
}

void Room::UpdateView(shared_ptr<Character> subjectChar, const ViewUpdate& result)
{
	int subjectId = subjectChar->GetId();

	bool isSubjectPlayer = (subjectChar->GetType() == Protocol::ObjectType::OBJECT_TYPE_PLAYER);
	bool isSubjectMonster = (subjectChar->GetType() == Protocol::ObjectType::OBJECT_TYPE_MONSTER);

	shared_ptr<Player> subjectPlayer = isSubjectPlayer ? static_pointer_cast<Player>(subjectChar) : nullptr;
	shared_ptr<Monster> subjectMonster = isSubjectMonster ? static_pointer_cast<Monster>(subjectChar) : nullptr;

	shared_ptr<SendBuffer> removeSubjectBuffer = PacketSerializer::MAKE_SC_REMOVE_OBJECT(subjectId);
	shared_ptr<SendBuffer> addSubjectBuffer = PacketSerializer::MAKE_SC_ADD_OBJECT(subjectChar);
	
	// 시야에서 나간 객체 처리
	for (int targetId : result.leaved) {
		if (targetId == subjectId) continue;
		shared_ptr<GameObject> targetObj = GetGameObject(targetId);
		if (!targetObj) continue;

		Protocol::ObjectType targetType = targetObj->GetType();
		bool isTargetPlayer = (targetType == Protocol::ObjectType::OBJECT_TYPE_PLAYER);
		bool isTargetMonster = (targetType == Protocol::ObjectType::OBJECT_TYPE_MONSTER);

		if (isSubjectPlayer || (isSubjectMonster && isTargetPlayer))
		{
			subjectChar->RemoveView(targetId);

			if (isSubjectMonster && isTargetPlayer) {
				subjectMonster->SleepIfNoPlayer();
			}
			else if (isSubjectPlayer) {
				if (shared_ptr<Session> subjectSession = subjectPlayer->GetSession()) {
					subjectSession->Send(PacketSerializer::MAKE_SC_REMOVE_OBJECT(targetId));
				}
			}
		}

		if (isTargetPlayer || isTargetMonster)
		{
			shared_ptr<Character> targetChar = static_pointer_cast<Character>(targetObj);

			if (isTargetPlayer || (isTargetMonster && isSubjectPlayer))
			{
				targetChar->RemoveView(subjectId);

				if (isTargetMonster && isSubjectPlayer) {
					static_pointer_cast<Monster>(targetChar)->SleepIfNoPlayer(); 
				}
				else if (isTargetPlayer) {
					if (shared_ptr<Session> targetSession = static_pointer_cast<Player>(targetChar)->GetSession()) {
						targetSession->Send(removeSubjectBuffer);
					}
				}
			}
		}
	}	
	// 새로 시야에 들어온 객체 처리
	for (int targetId : result.entered) {
		if (targetId == subjectId) continue;
		shared_ptr<GameObject> targetObj = GetGameObject(targetId);
		if (!targetObj) continue;

		Protocol::ObjectType targetType = targetObj->GetType();
		bool isTargetPlayer = (targetType == Protocol::ObjectType::OBJECT_TYPE_PLAYER);
		bool isTargetMonster = (targetType == Protocol::ObjectType::OBJECT_TYPE_MONSTER);

		if (isSubjectPlayer || (isSubjectMonster && isTargetPlayer))
		{
			subjectChar->_viewList.push_back(targetId);

			if (isSubjectMonster && isTargetPlayer) {
				subjectMonster->WakeUpByPlayer(static_pointer_cast<Player>(targetObj));
			}
			else if (isSubjectPlayer) {
				if (shared_ptr<Session> subjectSession = subjectPlayer->GetSession()) {
					subjectSession->Send(PacketSerializer::MAKE_SC_ADD_OBJECT(targetObj));
				}
			}
		}

		if (isTargetPlayer || isTargetMonster)
		{
			shared_ptr<Character> targetChar = static_pointer_cast<Character>(targetObj);

			if (isTargetPlayer || (isTargetMonster && isSubjectPlayer))
			{
				targetChar->_viewList.push_back(subjectId);

				if (isTargetMonster && isSubjectPlayer) {
					static_pointer_cast<Monster>(targetChar)->WakeUpByPlayer(subjectPlayer); 
				}
				else if (isTargetPlayer) {
					if (shared_ptr<Session> targetSession = static_pointer_cast<Player>(targetChar)->GetSession()) {
						targetSession->Send(addSubjectBuffer);
					}
				}
			}
		}
	}
}

shared_ptr<Player> Room::Id2Player(int pId)
{
	auto it = _players.find(pId);
	if (it == _players.end())
		return nullptr;

	return it->second;
}

shared_ptr<Monster> Room::Id2Monster(int mId)
{
	auto it = _monsters.find(mId);
	if (it == _monsters.end())
		return nullptr;

	return it->second;
}

shared_ptr<Room> RoomManager::CreateRoom()
{
	int id = IdGenerator();

	shared_ptr<Room> room = make_shared<Room>(GTimer, _iocpHandle);
	_rooms.insert({ id, room });
	room->InitRoom();

	return room;
}

void RoomManager::Remove(int roomId)
{
	RWLock::WriteGuard lock(_lock);
	_rooms.erase(roomId);
}

void RoomManager::Remove(shared_ptr<Room> room)
{
	return;
}

void RoomManager::EnterPlayer(shared_ptr<Player> player)
{
	RWLock::WriteGuard lock(_lock);
	for (auto& [id, room] : _rooms) {
		if (room->NumPlayers() < MAX_ROOM_CAPACITY) {
			player->SetOwnerRoom(room);
			room->PushJob(&Room::PlayerEnterRoom, player);
			return;
		}
	}
	shared_ptr<Room> newRoom = CreateRoom();
	newRoom->PushJob(&Room::PlayerEnterRoom, player);
}


int RoomManager::IdGenerator()
{
	static atomic<int> _idGenerator = 0;
	return ++_idGenerator;
}
