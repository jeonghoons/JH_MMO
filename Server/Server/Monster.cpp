#include "pch.h"
#include "Monster.h"
#include "Player.h"
#include "Room.h"
#include "NavmeshManager.h"
#include "ServerData.h"

Monster::Monster() : Character(Protocol::ObjectType::OBJECT_TYPE_MONSTER)
{
	_objectInfo.set_player_type(Protocol::PLAYER_TYPE_MONSTER);
	
	const CharacterData* statData = DataManager::GetCharacterData((int)_objectInfo.player_type());
	if (statData) {
		Protocol::StatInfo* stat = _objectInfo.mutable_stat();
		stat->set_max_hp(statData->maxHp);
		stat->set_hp(statData->hp);
		stat->set_attack_damage(statData->attackDamage);
		stat->set_attack_speed(statData->attackSpeed);
		stat->set_move_speed(statData->moveSpeed);
		_statInfo.Init(stat);
	}
	
	int myBasicAttackId = (int)_objectInfo.player_type() * 100 + 1;
	if (const SkillData* skill = DataManager::GetSkillData(myBasicAttackId)) {
		_attackRange = skill->radius;
	}

	_maxSpeed = _statInfo.GetMoveSpeed();

	_aiDecisionTimer.Reset(0);
}

Monster::~Monster()
{
}

void Monster::Update(float deltaTime)
{
	if (_statInfo.IsDead()) return;

	if (!_wakeUp) return;

	if (_aiDecisionTimer.IsReady())
	{
		_aiDecisionTimer.Reset(500);
		UpdateAI();
	}

	if (_hasPath) {
		FollowPath(deltaTime);
	}

	if (_objectInfo.position().state() == Protocol::MoveState::MOVE_STATE_RUN)
	{
		if (auto room = GetCurrentRoom()) {
			room->NPCMove(std::static_pointer_cast<Monster>(shared_from_this()));
		}
	}

	Character::Update(deltaTime);
}

void Monster::OnDamaged(int damage, std::shared_ptr<Character> attacker)
{
	Character::OnDamaged(damage, attacker);

	ChangeState(MonsterState::HIT);

	if (attacker->GetType() == Protocol::ObjectType::OBJECT_TYPE_PLAYER) {
		_targetPlayer = std::static_pointer_cast<Player>(attacker);
		ChangeState(MonsterState::TRACE);
	}
}

void Monster::OnDead(std::shared_ptr<Character> attacker)
{
	Character::OnDead(attacker);
	StopMove();
	ChangeState(MonsterState::NONE);
}

void Monster::UpdateAI()
{
	switch (_monsterState)
	{
	case MonsterState::NONE:   UpdateNone();   break;
	case MonsterState::PATROL: UpdatePatrol(); break;
	case MonsterState::TRACE:  UpdateTrace();  break;
	case MonsterState::ATTACK: UpdateAttack(); break;
	case MonsterState::HIT:    UpdateHit();    break;
	}
}

void Monster::ChangeState(MonsterState newState)
{
	if (_monsterState == newState) return;

	StopMove();
	// std::cout << "NPC[" << GetId() << "] State Changed: " << _monsterState << " -> " << newState << std::endl;
	_monsterState = newState;

	if (_monsterState == MonsterState::TRACE) {
		_pathSearchTimer.Reset(0);
	}
	else if (_monsterState == MonsterState::PATROL) {
		_patrolTimer.Reset(1000);
	}
}

void Monster::UpdateNone()
{

}

void Monster::UpdatePatrol()
{
	auto room = GetCurrentRoom();
	if (room == nullptr) return;

	std::shared_ptr<Player> closestPlayer = nullptr;
	float currentMinDistSq = FLT_MAX;
	float traceRangeSq = _traceRange * _traceRange;

	for (int objectId : _viewList)
	{
		auto object = room->GetGameObject(objectId);
		if (!object || object->GetType() != Protocol::ObjectType::OBJECT_TYPE_PLAYER) continue;

		shared_ptr<Player> player = std::static_pointer_cast<Player>(object);

		float diffX = player->GetPosition().x() - GetPosition().x();
		float diffY = player->GetPosition().y() - GetPosition().y();
		float distSq = diffX * diffX + diffY * diffY;

		if (distSq < currentMinDistSq && distSq <= traceRangeSq && !player->GetStat().IsDead())
		{
			currentMinDistSq = distSq;
			closestPlayer = player;
		}
	}

	if (closestPlayer)
	{
		_targetPlayer = closestPlayer;
		ChangeState(MonsterState::TRACE);
		return;
	}

	if (!_hasPath && _patrolTimer.IsReady())
	{
		Protocol::PositionInfo randomDest = room->GetGameMap().GetRandomPosInCell(_objectInfo.position());

		std::vector<Protocol::PositionInfo> newPath;
		if (room->GetNavManager()->FindPath(_objectInfo.position(), randomDest, newPath))
		{
			/*cout << "NPC[" << _objectInfo.id << "] : " << GetPosition().x << ", " << GetPosition().y << ", " << GetPosition().z
				<< " -> " << randomDest.x << ", " << randomDest.y << "," << randomDest.z << endl;*/
			SetPath(newPath);
			long long waitTime = 1000;
			_patrolTimer.Reset(waitTime);
		}
		else
		{
			_patrolTimer.Reset(500);
		}
	}
}

void Monster::UpdateTrace()
{
	shared_ptr<Player> target = _targetPlayer.lock();
	if (target == nullptr || target->GetStat().IsDead())
	{
		_targetPlayer.reset();
		ChangeState(MonsterState::PATROL);
		return;
	}

	float diffX = target->GetPosition().x() - _objectInfo.position().x();
	float diffY = target->GetPosition().y() - _objectInfo.position().y();
	float distSq = diffX * diffX + diffY * diffY;

	if (distSq > _traceRange * _traceRange)
	{
		_targetPlayer.reset();
		ChangeState(MonsterState::PATROL);
		return;
	}

	if (distSq <= _attackRange * _attackRange)
	{
		ChangeState(MonsterState::ATTACK);
		return;
	}
	float targetMovedDistSq = pow(target->GetPosition().x() - _lastTargetPos.x(), 2) + pow(target->GetPosition().y() - _lastTargetPos.y(), 2);

	if ((!_hasPath || targetMovedDistSq > 10000.f) && _pathSearchTimer.IsReady())
	{
		auto room = GetCurrentRoom();
		if (room && room->GetNavManager())
		{
			std::vector<Protocol::PositionInfo> newPath;
			if (room->GetNavManager()->FindPath(GetPosition(), target->GetPosition(), newPath))
			{
				SetPath(newPath);
				_lastTargetPos = target->GetPosition();
				_pathSearchTimer.Reset(1000); 
			}
			else
			{
				_pathSearchTimer.Reset(500);
			}
		}
	}
}

void Monster::UpdateAttack()
{
	auto target = _targetPlayer.lock();
	if (target == nullptr || target->GetStat().IsDead())
	{
		ChangeState(MonsterState::PATROL);
		return;
	}

	float diffX = target->GetPosition().x() - _objectInfo.position().x();
	float diffY = target->GetPosition().y() - _objectInfo.position().y();
	float distSq = diffX * diffX + diffY * diffY;

	// 거리가 멀어지면 다시 추적
	if (distSq > _attackRange * _attackRange)
	{
		ChangeState(MonsterState::TRACE);
		return;
	}

	if (_attackTimer.IsReady())
	{
		int myBasicAttackId = (int)_objectInfo.player_type() * 100 + 1;
		const SkillData* skill = DataManager::GetSkillData(myBasicAttackId);
		long long baseCooldownMs = skill ? skill->cooldownMs : 1000;
		float statAttackSpeed = _statInfo.GetAttackSpeed();
		long long finalCooldownMs = static_cast<long long>(baseCooldownMs / (statAttackSpeed > 0.f ? statAttackSpeed : 1.f));

		_attackTimer.Reset(finalCooldownMs);
		_objectInfo.mutable_position()->set_yaw(XMConvertToDegrees(atan2f(diffY, diffX)));


		if (shared_ptr<Room> room = GetCurrentRoom()) {
			room->CharacterAttack(static_pointer_cast<Character>(shared_from_this()), 0, target->GetId());
		}

		// std::cout << "NPC[" << GetId() << "] Attack -> Player[" << target->GetId() << "]" << std::endl;
	}
}

void Monster::UpdateHit()
{
	if (IsHit()) return;

	ChangeState(MonsterState::NONE);
}

void Monster::SetPath(const std::vector<Protocol::PositionInfo>& path)
{
	_path = path;
	_pathIndex = 0;
	if (_path.empty()) {
		StopMove();
	}
	else {
		_hasPath = true;
		_objectInfo.mutable_position()->set_state(Protocol::MOVE_STATE_RUN);
	}
}

void Monster::StopMove()
{
	if (_objectInfo.position().state() == Protocol::MoveState::MOVE_STATE_IDLE) return;

	_hasPath = false;
	MovableObject::StopMove();

	auto room = GetCurrentRoom();
	if (room != nullptr) {
		shared_ptr<SendBuffer> MoveBuffer = PacketSerializer::MAKE_SC_MOVE_OBJECT(shared_from_this());
		room->BroadcastAOI(static_pointer_cast<Character>(shared_from_this()), MoveBuffer);
	}
}

void Monster::WakeUpByPlayer(std::shared_ptr<Player> player)
{
	if (_statInfo.IsDead()) return;

	if (!_wakeUp)
	{
		_wakeUp = true;
		ChangeState(MonsterState::PATROL); // 깨어나면 패트롤(색적) 상태로 돌입
		// cout << "NPC[" << GetId() << "] Woke up by Player[" << player->GetId() << "]" << endl;
	}
}

void Monster::SleepIfNoPlayer()
{
	if (_statInfo.IsDead()) return;

	// Room 로직에 의해 _viewList에는 오직 플레이어만 남으므로, 비어있으면 유저가 없다는 뜻
	if (_viewList.empty())
	{
		_wakeUp = false;
		ChangeState(MonsterState::NONE);
		// cout << "NPC[" << GetId() << "] Goes to sleep." << endl;
	}
}

void Monster::FollowPath(float deltaTime)
{
	if (!_hasPath || _pathIndex >= _path.size()) return;

	const Protocol::PositionInfo& pos = _objectInfo.position();

	while (_pathIndex < _path.size())
	{
		Protocol::PositionInfo& targetPos = _path[_pathIndex];

		XMVECTOR vCurr = XMVectorSet(pos.x(), pos.y(), pos.z(), 0.0f);
		XMVECTOR vDest = XMVectorSet(targetPos.x(), targetPos.y(), targetPos.z(), 0.0f);
		float dist = XMVectorGetX(XMVector3Length(XMVectorSubtract(vDest, vCurr)));

		if (dist < 50.0f)
		{
			_pathIndex++;
		}
		else
		{
			XMFLOAT3 desPos = { targetPos.x(), targetPos.y(), targetPos.z()};

			if (Move(desPos))
			{
				auto room = GetCurrentRoom();
				if (room != nullptr) {
					shared_ptr<SendBuffer> MoveBuffer = PacketSerializer::MAKE_SC_MOVE_OBJECT(shared_from_this());
					room->BroadcastAOI(static_pointer_cast<Character>(shared_from_this()), MoveBuffer);
				}
			}
			return;
		}
	}
	StopMove();
}
