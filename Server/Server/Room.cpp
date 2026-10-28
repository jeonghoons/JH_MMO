#include "pch.h"
#include "Room.h"
#include "Session.h"
#include "PacketHandler.h"
#include "Player.h"
#include "Monster.h"
#include "CombatProcessor.h"
#include "ServerData.h"
#include "Metrics.h"

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

	const auto& monsterSpawns = _gameMap.GetMonsterSpawns();
	if (monsterSpawns.empty())
	{
		cout << "[Room] 스폰 포인트가 없어 몬스터를 생성할 수 없습니다!" << endl;
	}
	else
	{
		for (int i = 0; i < static_cast<int>(monsterSpawns.size()); ++i) {
			for (int n = 0; n < max(1, monsterSpawns[i].MaxCount); ++n)
				SpawnMonster(i);
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
	// 예정 간격(100ms)보다 얼마나 늦게 틱이 돌았는지 기록한다. 첫 틱(InitRoom에서 직접 호출)은 제외한다.
	if (elapsedTime != currentTick)
		Metrics::TickLate.Add(max<long long>(0, (currentTick - elapsedTime - 100) * 1000));

	// 서버가 멈췄다 재개되는 등 틱 간격이 비정상이면 한 번에 큰 이동이 일어나지 않게 기본 간격으로 대체한다.
	if (dt <= 0.f || dt > 0.5f) dt = 0.1f;

	const auto bodyStart = chrono::steady_clock::now();
	for (const auto& object : _objects) {
		object.second->Update(dt);
	}
	UpdateZones();
	Metrics::TickBody.Add(chrono::duration_cast<chrono::microseconds>(chrono::steady_clock::now() - bodyStart).count());
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

	const auto& playerStarts = _gameMap.GetPlayerStarts();
	if (object->GetType() == Protocol::ObjectType::OBJECT_TYPE_PLAYER && !playerStarts.empty()) {
		Protocol::PositionInfo spawnPos;
		spawnPos.set_x(playerStarts[0].Pos.X);
		spawnPos.set_y(playerStarts[0].Pos.Y);
		spawnPos.set_z(playerStarts[0].Pos.Z);
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
		ReleaseSlot(); // EnterPlayer에서 예약한 자리를 반환한다.
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

	if (_players.erase(playerId) > 0)
		ReleaseSlot();

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

void Room::PlayerPing(shared_ptr<Player> player, long long clientTime)
{
	if (shared_ptr<Session> session = player->GetSession())
		session->Send(PacketSerializer::MAKE_SC_PONG(clientTime));
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

void Room::CastSkill(shared_ptr<Character> caster, int slot, int targetId, float aimYaw, float aimX, float aimY)
{
	if (caster == nullptr) return;
	if (GetGameObject(caster->GetId()) == nullptr) return;   // 이 방에 없는 시전자

	const SkillData* skill = caster->FindCastableSkill(slot);
	if (skill == nullptr) return;

	const Protocol::PositionInfo& pos = caster->GetPosition();
	const bool casterIsPlayer = (caster->GetType() == Protocol::ObjectType::OBJECT_TYPE_PLAYER);
	constexpr float RAD_TO_DEG = 180.0f / MathUtils::PI;

	float yaw = aimYaw;
	float ax = aimX;
	float ay = aimY;

	if (skill->targeting == SkillTargeting::Targeted)
	{
		// 클라이언트가 보낸 대상 id는 신뢰하지 않고, 존재/생존/적대 관계/사거리를 서버에서 다시 확인한다.
		shared_ptr<GameObject> targetObj = GetGameObject(targetId);
		if (targetObj == nullptr) return;

		const Protocol::ObjectType wantedType = casterIsPlayer ? Protocol::ObjectType::OBJECT_TYPE_MONSTER : Protocol::ObjectType::OBJECT_TYPE_PLAYER;
		if (targetObj->GetType() != wantedType) return;
		if (static_pointer_cast<Character>(targetObj)->GetStat().IsDead()) return;

		const float dx = targetObj->GetPosition().x() - pos.x();
		const float dy = targetObj->GetPosition().y() - pos.y();
		const float dist = std::sqrt(dx * dx + dy * dy);
		if (dist > skill->castRange * 1.1f) return;   // 이동 지연을 감안한 10% 여유

		yaw = std::atan2(dy, dx) * RAD_TO_DEG;
	}
	else
	{
		if (!std::isfinite(yaw)) return;

		if (skill->hitShape == HitShape::Zone)
		{
			if (!std::isfinite(ax) || !std::isfinite(ay)) return;

			// 사거리를 넘는 지점은 사거리 끝으로 보정한다.
			float dx = ax - pos.x();
			float dy = ay - pos.y();
			const float dist = std::sqrt(dx * dx + dy * dy);
			if (dist > skill->castRange && dist > 0.f)
			{
				const float scale = skill->castRange / dist;
				dx *= scale;
				dy *= scale;
				ax = pos.x() + dx;
				ay = pos.y() + dy;
			}
			if (dist > 0.f) yaw = std::atan2(dy, dx) * RAD_TO_DEG;
		}
	}

	// 여기부터 시전이 확정된다.
	caster->GetMutablePosition()->set_yaw(yaw);
	const Character::CastTiming timing = caster->BeginCast(*skill);
	if (casterIsPlayer)
		Metrics::AttackAccepted.fetch_add(1);

	BroadcastAOI(caster, PacketSerializer::MAKE_SC_ATTACK(caster->GetId(), skill->skillId, targetId, yaw, ax, ay, static_cast<int>(timing.cooldownMs)), true);

	ReserveJob(static_cast<DWORD>(timing.hitDelayMs), &Room::ExecuteSkillHit, caster->GetId(), skill->skillId, targetId, yaw, ax, ay);
}

void Room::ExecuteSkillHit(int attackerId, int skillId, int targetId, float aimYaw, float aimX, float aimY)
{
	auto attackerObj = GetGameObject(attackerId);
	if (attackerObj == nullptr) return;

	auto attackerChar = std::static_pointer_cast<Character>(attackerObj);

	CombatProcessor::ProcessSkillHit(shared_from_this(), attackerChar, skillId, targetId, aimYaw, aimX, aimY);
}

void Room::CreateZone(shared_ptr<Character> attacker, const SkillData& skill, float x, float y)
{
	SkillZone zone;
	zone.attackerId = attacker->GetId();
	zone.x = x;
	zone.y = y;
	zone.radius = skill.radius;
	zone.damage = skill.baseDamage;
	zone.tickMs = max(1, skill.tickMs);
	zone.endTime = chrono::steady_clock::now() + chrono::milliseconds(skill.durationMs);
	_zones.push_back(std::move(zone));
	Metrics::ZonesCreated.fetch_add(1);
}

void Room::UpdateZones()
{
	if (_zones.empty()) return;

	const auto now = chrono::steady_clock::now();

	for (size_t i = 0; i < _zones.size();)
	{
		SkillZone& zone = _zones[i];

		shared_ptr<GameObject> attackerObj = GetGameObject(zone.attackerId);
		if (now >= zone.endTime || attackerObj == nullptr)
		{
			_zones[i] = std::move(_zones.back());
			_zones.pop_back();
			continue;
		}

		shared_ptr<Character> attacker = static_pointer_cast<Character>(attackerObj);
		const float radiusSq = zone.radius * zone.radius;

		// 시전자가 플레이어면 몬스터를, 몬스터면 플레이어를 맞힌다. 장판 안에 들어온 순간 바로 피해를 주고 이후 tickMs마다 반복한다.
		auto hitTargets = [&](auto& container)
		{
			for (auto& [id, target] : container)
			{
				if (target->GetStat().IsDead()) continue;

				const float dx = target->GetPosition().x() - zone.x;
				const float dy = target->GetPosition().y() - zone.y;
				if (dx * dx + dy * dy > radiusSq) continue;

				auto& nextHit = zone.nextHit[id];
				if (now < nextHit) continue;

				nextHit = now + chrono::milliseconds(zone.tickMs);
				Metrics::ZoneHits.fetch_add(1);
				target->OnDamaged(zone.damage, zone.attackerId, attacker);
			}
		};

		if (attacker->GetType() == Protocol::ObjectType::OBJECT_TYPE_PLAYER) hitTargets(_monsters);
		else hitTargets(_players);

		++i;
	}
}
void Room::ApplyDelayedDamage(int targetId, int attackerId, int damage)
{
	auto targetObj = GetGameObject(targetId);
	// 이미 발사된 투사체는 공격자가 날아가는 동안 사라졌어도(접속 종료 등) 피해를 준다. 이때 attackerObj는 nullptr다.
	auto attackerObj = GetGameObject(attackerId);

	if (targetObj == nullptr) return;

	auto targetChar = std::static_pointer_cast<Character>(targetObj);

	if (!targetChar->GetStat().IsDead())
	{
		std::cout << "[Room] Target[" << targetId << "] Dmg: " << damage <<  std::endl;
		targetChar->OnDamaged(damage, attackerId, static_pointer_cast<Character>(attackerObj));
	}
}

void Room::NpcEnterRoom(shared_ptr<Monster> monster)
{
	if (false == AddObject(monster)) {
		return;
	}

	_monsters.emplace(monster->GetId(), monster);
	Metrics::Monsters.fetch_add(1);

	ViewUpdate result = _gameMap.EnterMap(monster->GetId(), monster->GetPosition());
	if (!result.entered.empty()) {
		UpdateView(monster, result);
	}
}

void Room::SpawnMonster(int spawnIndex)
{
	const auto& spawns = _gameMap.GetMonsterSpawns();
	if (spawnIndex < 0 || spawnIndex >= static_cast<int>(spawns.size())) return;

	const MonsterSpawnPoint& spawn = spawns[spawnIndex];

	shared_ptr<Monster> monster = make_shared<Monster>((Protocol::PlayerType)spawn.MonsterTemplateId);
	monster->SetId(MonsterIdGenerator());
	monster->SetSpawnIndex(spawnIndex);

	Protocol::PositionInfo spawnPos;
	spawnPos.set_x(spawn.Pos.X);
	spawnPos.set_y(spawn.Pos.Y);
	spawnPos.set_z(spawn.Pos.Z);
	spawnPos.set_yaw(spawn.Pos.Yaw);

	// 마커 반경 안의 임의 위치. 내비메시 위로 보정할 수 없으면 마커 위치를 그대로 쓴다.
	if (spawn.SpawnRadius > 0.f)
	{
		const float angle = (rand() / static_cast<float>(RAND_MAX)) * 2.f * MathUtils::PI;
		const float dist = sqrtf(rand() / static_cast<float>(RAND_MAX)) * spawn.SpawnRadius;

		Protocol::PositionInfo candidate = spawnPos;
		candidate.set_x(spawn.Pos.X + cosf(angle) * dist);
		candidate.set_y(spawn.Pos.Y + sinf(angle) * dist);

		if (NavmeshManager* nav = GetNavManager()) {
			if (nav->AdjustPositionToNavMesh(candidate))
				spawnPos = candidate;
		}
	}

	monster->SetPosition(spawnPos);
	NpcEnterRoom(monster);
}

void Room::RemoveMonster(int monsterId)
{
	shared_ptr<Monster> monster = Id2Monster(monsterId);
	if (monster == nullptr) return;

	const int spawnIndex = monster->GetSpawnIndex();
	const int templateId = (int)monster->GetInfo().player_type();

	// 시야에 있던 플레이어들에게 SC_REMOVE_OBJECT가 전달된다.
	ViewUpdate result = _gameMap.LeaveMap(monsterId);
	if (!result.leaved.empty()) {
		UpdateView(monster, result);
	}

	RemoveObject(monsterId);
	_monsters.erase(monsterId);
	Metrics::Monsters.fetch_sub(1);

	// 사망 시점부터 RespawnSec가 지난 뒤 같은 마커에서 새로 만든다. (이미 deathRemoveDelay만큼 지났으므로 그만큼 뺀다)
	const MonsterData* data = DataManager::GetMonsterData(templateId);
	const int delayMs = data ? max(0, data->respawnMs - data->deathRemoveDelayMs) : 7000;
	ReserveJob(delayMs, &Room::SpawnMonster, spawnIndex);
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
	// 맵·몬스터 로딩이 오래 걸리므로 매니저 락을 잡지 않고 먼저 초기화한 뒤 등록한다.
	room->InitRoom();

	{
		RWLock::WriteGuard lock(_lock);
		_rooms.insert({ id, room });
	}

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

bool RoomManager::TryEnterExisting(shared_ptr<Player> player)
{
	RWLock::ReadGuard lock(_lock);
	for (auto& [id, room] : _rooms) {
		if (room->TryReserveSlot()) {
			player->SetOwnerRoom(room);
			room->PushJob(&Room::PlayerEnterRoom, player);
			return true;
		}
	}
	return false;
}

void RoomManager::EnterPlayer(shared_ptr<Player> player)
{
	if (player == nullptr) return;

	if (TryEnterExisting(player))
		return;

	// 방이 없으면 하나만 만든다. 락을 기다리는 동안 다른 스레드가 만든 방이 있으면 그 방을 쓴다.
	lock_guard<mutex> createLock(_createMutex);
	if (TryEnterExisting(player))
		return;

	shared_ptr<Room> newRoom = CreateRoom();
	newRoom->TryReserveSlot();
	player->SetOwnerRoom(newRoom);
	newRoom->PushJob(&Room::PlayerEnterRoom, player);
}


int RoomManager::IdGenerator()
{
	static atomic<int> _idGenerator = 0;
	return ++_idGenerator;
}
