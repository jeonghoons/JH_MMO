#include "pch.h"
#include "Game/Object/Character.h"
#include "Data/ServerData.h"

Character::Character(Protocol::ObjectType type) : MovableObject(type)
{
	
}

void Character::Update(float deltaTime)
{
	MovableObject::Update(deltaTime);
}

const SkillData* Character::FindCastableSkill(int slot, CastCheck* check, long long earlyToleranceMs) const
{
	CastCheck local;
	CastCheck& out = check ? *check : local;
	out = CastCheck{};
	auto deny = [&out](Protocol::AttackResult reason, long long remainMs = 0) -> const SkillData* {
		out.reason = reason;
		out.remainMs = remainMs;
		return nullptr;
	};

	if (_statInfo.IsDead()) return deny(Protocol::ATTACK_RESULT_DEAD);

	if (IsHit()) return deny(Protocol::ATTACK_RESULT_HIT_STUN);

	const SkillData* skill = GetSlotSkill(slot);
	if (skill == nullptr) return deny(Protocol::ATTACK_RESULT_NO_SKILL);

	// 이전 시전의 애니메이션이 끝나기 전에는 기본 공격도 스킬도 받지 않는다.
	const long long actionRemain = _actionTimer.RemainMs();
	if (actionRemain > 0)
	{
		if (_actionIsSwapLock || actionRemain > earlyToleranceMs) return deny(Protocol::ATTACK_RESULT_ACTING, actionRemain);
		out.actionCarryMs = actionRemain;
	}

	// 클라이언트가 공격 패킷을 연달아 보내도 스킬 쿨타임 안에서는 받아들이지 않는다.
	auto it = _skillCooldowns.find(skill->skillId);
	if (it != _skillCooldowns.end())
	{
		const long long cooldownRemain = it->second.RemainMs();
		if (cooldownRemain > earlyToleranceMs)
		{
			out.actionCarryMs = 0;
			return deny(Protocol::ATTACK_RESULT_COOLDOWN, cooldownRemain);
		}
		out.cooldownCarryMs = cooldownRemain;
	}

	return skill;
}

Character::CastTiming Character::BeginCast(const SkillData& skill, long long actionCarryMs, long long cooldownCarryMs)
{
	// 기본 공격만 AttackSpeed만큼 애니메이션을 빨리 재생한다. (클라이언트도 같은 배율로 몽타주를 재생)
	float rate = 1.f;
	if (skill.slot == 0)
	{
		const float attackSpeed = _statInfo.GetAttackSpeed();
		rate = attackSpeed > 0.f ? attackSpeed : 1.f;
	}

	CastTiming timing;
	timing.actionMs = static_cast<long long>(skill.animMs / rate);
	timing.hitDelayMs = static_cast<long long>(skill.hitDelayMs / rate);
	timing.cooldownMs = skill.cooldownMs;
	timing.moveLockMs = static_cast<long long>(skill.moveLockMs / rate);

	_actionTimer.Reset(timing.actionMs + actionCarryMs);
	_actionIsSwapLock = false;
	if (timing.cooldownMs > 0)
		_skillCooldowns[skill.skillId].Reset(timing.cooldownMs + cooldownCarryMs);
	return timing;
}
void Character::OnDamaged(int damage, int attackerId, shared_ptr<Character> attacker)
{
	if (_statInfo.IsDead()) return;

	int actualDamage = _statInfo.OnDamaged(damage);

	_hitTimer.Reset(GetHitStunMs());

	auto room = GetCurrentRoom();
	if (room) {
		shared_ptr<SendBuffer> damageBuffer = PacketSerializer::MAKE_SC_DAMAGE(attackerId, GetId(), actualDamage, _statInfo.GetHp());
		room->BroadcastAOI(static_pointer_cast<Character>(shared_from_this()), damageBuffer, true);
	}

	if (_statInfo.IsDead())
	{
		OnDead(attacker);
	}
}

void Character::OnDead(shared_ptr<Character> attacker)
{
	auto room = GetCurrentRoom();
	if (room) {
		shared_ptr<SendBuffer> sendBuffer = PacketSerializer::MAKE_SC_DEAD(GetId(), GetReviveDelayMs());
		room->BroadcastAOI(static_pointer_cast<Character>(shared_from_this()), sendBuffer, true);
		// room->PushJob(&Room::RemoveObject, GetId());
	}
}

void Character::RemoveView(int objectId)
{
	auto it = find(_viewList.begin(), _viewList.end(), objectId);
	if (it != _viewList.end()) {
		*it = _viewList.back();
		_viewList.pop_back();
	}
}
