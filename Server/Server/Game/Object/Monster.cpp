#include "pch.h"
#include "Game/Object/Monster.h"
#include "Game/Object/Player.h"
#include "Game/Room/Room.h"
#include "Game/World/NavmeshManager.h"
#include "Data/ServerData.h"

Monster::Monster(int templateId) : Character(Protocol::ObjectType::OBJECT_TYPE_MONSTER)
{
	// 몬스터 종류는 template_id(MonsterTable ID)로 알린다. PlayerType은 플레이어 체형 전용이라 NONE.
	_objectInfo.set_player_type(Protocol::PLAYER_TYPE_NONE);
	_objectInfo.set_template_id(templateId);

	// MonsterTable 행이 스탯(고정 공격력 포함)과 기본 공격 스킬을 정한다. 로더가 SkillId의 존재를, Room이 템플릿의 존재를 보장한다.
	if (const MonsterData* monsterData = DataManager::GetMonsterData(templateId)) {
		_statInfo.InitFromData(_objectInfo.mutable_stat(), monsterData->stat);
		_skillId = monsterData->skillId;
		if (const SkillData* skill = DataManager::GetSkillData(_skillId)) {
			_attackRange = skill->radius;
		}
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

	const float prevX = _objectInfo.position().x(), prevY = _objectInfo.position().y();
	if (_hasPath) {
		FollowPath(deltaTime);
	}

	// 실제 위치 이동은 여기(MovableObject::ApplyMovement)에서 일어난다. FollowPath는 방향만 정한다.
	Character::Update(deltaTime);

	// 이번 틱에 위치가 바뀌었으면 이동이 끝난 뒤의 위치로 시야(AOI)를 갱신한다.
	// (이동 전에 갱신하면 AOI가 한 틱 늦은 위치를 갖고, 마지막 걸음은 상태가 IDLE로 바뀌어 빠진다 — A1.3c 시야 감사로 발견)
	if (_objectInfo.position().x() != prevX || _objectInfo.position().y() != prevY)
	{
		if (auto room = GetCurrentRoom()) {
			room->NPCMove(std::static_pointer_cast<Monster>(shared_from_this()));
		}
	}
}

void Monster::OnDamaged(int damage, int attackerId, std::shared_ptr<Character> attacker)
{
	Character::OnDamaged(damage, attackerId, attacker);

	ChangeState(MonsterState::HIT);

	// 공격자가 이미 사라졌으면 어그로 대상은 바꾸지 않는다.
	if (attacker && attacker->GetType() == Protocol::ObjectType::OBJECT_TYPE_PLAYER) {
		_targetPlayer = std::static_pointer_cast<Player>(attacker);
		ChangeState(MonsterState::TRACE);
	}
}

int Monster::GetHitStunMs() const
{
	const MonsterData* data = DataManager::GetMonsterData(_objectInfo.template_id());
	return data ? data->hitStunMs : Character::GetHitStunMs();
}

void Monster::OnDead(std::shared_ptr<Character> attacker)
{
	Character::OnDead(attacker);
	StopMove();
	ChangeState(MonsterState::NONE);

	// 사망 연출 시간이 지나면 월드에서 제거하고, 그 시점부터 리스폰을 예약한다. (Room::RemoveMonster)
	if (auto room = GetCurrentRoom()) {
		const MonsterData* data = DataManager::GetMonsterData(_objectInfo.template_id());
		const int removeDelayMs = data ? data->deathRemoveDelayMs : 3000;
		room->ReserveJob(removeDelayMs, &Room::RemoveMonster, GetId());
	}
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
		// 배회 범위는 MonsterTable.WanderRadiusCm (임시 — 행동트리 개선 때 재검토)
		const MonsterData* data = DataManager::GetMonsterData(_objectInfo.template_id());
		const float wanderRadius = data ? static_cast<float>(data->wanderRadiusCm) : 500.f;
		Protocol::PositionInfo randomDest = room->GetGameMap().GetRandomPosNear(_objectInfo.position(), wanderRadius);

		std::vector<Protocol::PositionInfo> newPath;
		if (room->GetNavManager()->FindPath(_objectInfo.position(), randomDest, newPath))
		{
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

	// 공격 간격은 공용 행동 잠금(Character::BeginCast, 애니메이션 1회 ÷ AttackSpeed)이 정한다.
	if (!IsActing())
	{
		_objectInfo.mutable_position()->set_yaw(XMConvertToDegrees(atan2f(diffY, diffX)));


		if (shared_ptr<Room> room = GetCurrentRoom()) {
			room->CastSkill(static_pointer_cast<Character>(shared_from_this()), 0, target->GetId(), _objectInfo.position().yaw(), 0.f, 0.f);
		}

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
