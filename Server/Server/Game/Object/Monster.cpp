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
		_wanderRadius = static_cast<float>(monsterData->wanderRadiusCm);
		_aggroRange = static_cast<float>(monsterData->aggroRangeCm);
		_leashRange = static_cast<float>(monsterData->leashRangeCm);
		_returnMode = monsterData->returnMode;
		_bodyRadius = static_cast<float>(monsterData->bodyRadiusCm);
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
	if (_statInfo.IsDead()) return;

	std::shared_ptr<Player> attackerPlayer;
	if (attacker && attacker->GetType() == Protocol::ObjectType::OBJECT_TYPE_PLAYER)
		attackerPlayer = std::static_pointer_cast<Player>(attacker);

	// 안전 구역 안에서 준 피해는 받지 않는다(양방향 차단, D-195). 공격자가 이미 사라졌으면(nullptr) 그대로 적용한다.
	if (attackerPlayer) {
		auto room = GetCurrentRoom();
		if (room && room->GetGameMap().IsInSafeZone(attackerPlayer->GetPosition())) return;
	}

	// 복귀 중 피해 무시(ReturnMode 1)
	if (_monsterState == MonsterState::RETURN && _returnMode == MONSTER_RETURN_EVADE) return;

	Character::OnDamaged(damage, attackerId, attacker);

	// 이 피해로 죽었으면 OnDead가 상태를 정리했다. 죽은 몬스터의 상태를 다시 바꾸지 않는다.
	if (_statInfo.IsDead()) return;

	// 걸어서 복귀 중(ReturnMode 0): 나와 공격자가 모두 추격 제한 안일 때만 다시 쫓는다. 아니면 맞으면서 계속 돌아간다.
	if (_monsterState == MonsterState::RETURN)
	{
		const float leashSq = _leashRange * _leashRange;
		if (attackerPlayer && IsTargetable(attackerPlayer) && DistSqToHome() <= leashSq &&
			DistSqFromHome(attackerPlayer->GetPosition().x(), attackerPlayer->GetPosition().y()) <= leashSq)
		{
			_targetPlayer = attackerPlayer;
			ChangeState(MonsterState::TRACE);
		}
		return;
	}

	ChangeState(MonsterState::HIT);

	// 맞으면 선공·비선공 모두 공격자를 쫓는다(거리 무관 — 포기는 추격 제한이 정한다). 대상은 마지막으로 때린 사람.
	// 공격자가 사라졌거나 대상으로 삼을 수 없으면 HIT에 머물다 경직이 끝나면 배회로 돌아간다(UpdateHit).
	if (attackerPlayer && IsTargetable(attackerPlayer)) {
		_targetPlayer = attackerPlayer;
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

		// 처치를 인정받는 사람(막타 + 시야 안 파티원)은 한 번만 계산해 경험치와 퀘스트 처치 인정이 함께 쓴다(Q1, D-261).
		const vector<shared_ptr<Player>> receivers = room->GetKillReceivers(attacker);
		// 처치 경험치(A2.3b, D-254): 드랍 롤보다 먼저. 막타 + 시야 안 파티원이 나눈다.
		room->GrantKillExp(receivers, _objectInfo.template_id());
		// 퀘스트 처치 인정(Q1): 경험치가 0인 몬스터도 센다. 던전에서는 같은 방의 파티원 전원.
		room->CreditKill(receivers, _objectInfo.template_id());

		// 드랍 롤(A3.4, D-157): DropTable의 이 몬스터 행을 각각 굴려 사망 지점 둘레에 떨어뜨린다. 소유권은 막타(attacker).
		room->SpawnMonsterDrops(_objectInfo.template_id(), GetPosition(), attacker);

		if (_isBoss) room->OnBossDead();
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
	case MonsterState::RETURN: UpdateReturn(); break;
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
	else if (_monsterState == MonsterState::RETURN) {
		_targetPlayer.reset();
		_pathSearchTimer.Reset(0);
		// 홈까지 걷는 데 걸릴 시간의 두 배 + 5초 안에 닿지 못하면 홈으로 옮긴다(UpdateReturn).
		const float distCm = sqrtf(DistSqToHome());
		_returnTimer.Reset(static_cast<long long>(distCm / max(_maxSpeed, 1.f) * 2000.f) + 5000);
	}
}

void Monster::UpdateNone()
{

}

float Monster::DistSqFromHome(float x, float y) const
{
	if (!_hasHome) return 0.f;
	const float dx = x - _homePos.x(), dy = y - _homePos.y();
	return dx * dx + dy * dy;
}

float Monster::DistSqToHome() const
{
	return DistSqFromHome(_objectInfo.position().x(), _objectInfo.position().y());
}

bool Monster::IsTargetable(const std::shared_ptr<Player>& player) const
{
	if (player == nullptr) return false;
	// 내 시야 목록에 없으면 방을 나갔거나 시야 밖이다(Player 객체는 세션이 잡고 있어 방을 나가도 살아 있다).
	// 시야 목록(이 Room의 데이터)을 먼저 본다 — 다른 Room으로 옮긴 플레이어의 필드는 그 Room 스레드의 것이라 읽지 않는다(DG3b).
	if (std::find(_viewList.begin(), _viewList.end(), player->GetId()) == _viewList.end()) return false;
	if (player->GetStat().IsDead()) return false;
	auto room = GetCurrentRoom();
	if (room == nullptr) return false;
	return !room->GetGameMap().IsInSafeZone(player->GetPosition());
}

bool Monster::IsInSafeZone() const
{
	auto room = GetCurrentRoom();
	return room != nullptr && room->GetGameMap().IsInSafeZone(_objectInfo.position());
}

void Monster::UpdatePatrol()
{
	auto room = GetCurrentRoom();
	if (room == nullptr) return;

	// 홈에서 배회 반경 밖에 있으면(추격 중 잠들었다 깨어난 경우 등) 배회가 아니라 복귀부터 한다.
	const float homeSlack = _wanderRadius + 200.f;
	if (_hasHome && DistSqToHome() > homeSlack * homeSlack)
	{
		ChangeState(MonsterState::RETURN);
		return;
	}

	// 선공(AggroRangeCm > 0)만 먼저 찾는다. 후보 = 대상으로 삼을 수 있고, 인식 범위 안이고, 홈에서 추격 제한 안에 있는 가장 가까운 플레이어.
	// (추격 제한 밖의 플레이어를 잡으면 쫓자마자 복귀해 추격·복귀를 되풀이한다)
	if (_aggroRange > 0.f)
	{
		std::shared_ptr<Player> closestPlayer = nullptr;
		float currentMinDistSq = FLT_MAX;
		const float aggroRangeSq = _aggroRange * _aggroRange;
		const float leashSq = _leashRange * _leashRange;

		for (int objectId : _viewList)
		{
			auto object = room->GetGameObject(objectId);
			if (!object || object->GetType() != Protocol::ObjectType::OBJECT_TYPE_PLAYER) continue;

			shared_ptr<Player> player = std::static_pointer_cast<Player>(object);

			float diffX = player->GetPosition().x() - GetPosition().x();
			float diffY = player->GetPosition().y() - GetPosition().y();
			float distSq = diffX * diffX + diffY * diffY;

			if (distSq < currentMinDistSq && distSq <= aggroRangeSq && IsTargetable(player) &&
				DistSqFromHome(player->GetPosition().x(), player->GetPosition().y()) <= leashSq)
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
	}

	if (!_hasPath && _patrolTimer.IsReady())
	{
		// 배회: 홈 중심 MonsterTable.WanderRadiusCm 안의 임의 지점(현재 위치 기준으로 고르면 걸을수록 스폰 지점에서 멀어진다)
		Protocol::PositionInfo randomDest = room->GetGameMap().GetRandomPosNear(_hasHome ? _homePos : _objectInfo.position(), _wanderRadius);

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
	// 추격 포기(L3, D-193): 대상을 잃음(사망·시야 이탈·방 퇴장·안전 구역 진입) / 내가 홈에서 추격 제한을 넘음 / 내가 안전 구역에 닿음 → 홈으로 복귀.
	// 대상과의 거리로는 포기하지 않는다(원거리 공격을 맞고도 쫓아가야 한다).
	shared_ptr<Player> target = _targetPlayer.lock();
	if (!IsTargetable(target) || DistSqToHome() > _leashRange * _leashRange || IsInSafeZone())
	{
		ChangeState(MonsterState::RETURN);
		return;
	}

	float diffX = target->GetPosition().x() - _objectInfo.position().x();
	float diffY = target->GetPosition().y() - _objectInfo.position().y();
	float distSq = diffX * diffX + diffY * diffY;

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
	// 포기 조건은 UpdateTrace와 같다.
	auto target = _targetPlayer.lock();
	if (!IsTargetable(target) || DistSqToHome() > _leashRange * _leashRange || IsInSafeZone())
	{
		ChangeState(MonsterState::RETURN);
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
			room->CastSkill(static_pointer_cast<Character>(shared_from_this()), 0, target->GetId(), _objectInfo.position().yaw(), 0.f, 0.f, 0u);
		}

	}
}

void Monster::UpdateHit()
{
	if (IsHit()) return;

	// 쫓을 대상 없이 맞은 경우(공격자가 사라짐)만 여기로 온다. 경직이 끝나면 배회로 돌아간다(NONE에 두면 다시 깨어날 때까지 멈춘다).
	ChangeState(MonsterState::PATROL);
}

void Monster::UpdateReturn()
{
	auto room = GetCurrentRoom();
	if (room == nullptr) return;

	// 도착 판정은 수평 거리(홈은 내비메시 위로 보정된 자리라 경로 끝과 거의 같다)
	if (!_hasHome || DistSqToHome() <= 100.f * 100.f)
	{
		ArriveHome();
		return;
	}

	// 제한 시간 안에 닿지 못했다(경로가 끊겼거나 부분 경로만 나옴): 홈으로 옮긴다.
	if (_returnTimer.IsReady())
	{
		StopMove();
		Protocol::PositionInfo home = _homePos;
		home.set_state(Protocol::MOVE_STATE_IDLE);
		SetPosition(home);
		room->NPCMove(std::static_pointer_cast<Monster>(shared_from_this()));
		room->BroadcastAOI(static_pointer_cast<Character>(shared_from_this()), PacketSerializer::MAKE_SC_MOVE_OBJECT(shared_from_this()));
		LOG_DEBUG("Monster", "몬스터 {} 가 제한 시간 안에 홈에 닿지 못해 홈으로 옮겼습니다", GetId());
		ArriveHome();
		return;
	}

	if (!_hasPath && _pathSearchTimer.IsReady() && room->GetNavManager())
	{
		std::vector<Protocol::PositionInfo> newPath;
		if (room->GetNavManager()->FindPath(GetPosition(), _homePos, newPath))
		{
			SetPath(newPath);
			_pathSearchTimer.Reset(1000);
		}
		else
		{
			_pathSearchTimer.Reset(500);
		}
	}
}

void Monster::ArriveHome()
{
	// ReturnMode 1: 복귀를 마치면 HP를 가득 채운다. 피해·아이템이 아닌 HP 변화라 SC_CHANGE_HP로 알린다.
	if (_returnMode == MONSTER_RETURN_EVADE && !_statInfo.IsDead() && _statInfo.GetHp() < _statInfo.GetMaxHp())
	{
		_statInfo.OnHealed(_statInfo.GetMaxHp());
		if (auto room = GetCurrentRoom())
			room->BroadcastAOI(static_pointer_cast<Character>(shared_from_this()), PacketSerializer::MAKE_SC_CHANGE_HP(GetId(), _statInfo.GetHp()));
	}
	ChangeState(MonsterState::PATROL);
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
		_targetPlayer.reset();
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
