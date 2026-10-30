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

const SkillData* Character::FindCastableSkill(int slot) const
{
	if (_statInfo.IsDead()) return nullptr;

	if (IsHit()) return nullptr;

	const SkillData* skill = DataManager::GetSkillBySlot((int)_objectInfo.player_type(), slot);
	if (skill == nullptr) return nullptr;

	// 이전 시전의 애니메이션이 끝나기 전에는 기본 공격도 스킬도 받지 않는다.
	if (IsActing()) return nullptr;

	// 클라이언트가 공격 패킷을 연달아 보내도 스킬 쿨타임 안에서는 받아들이지 않는다.
	auto it = _skillCooldowns.find(skill->skillId);
	if (it != _skillCooldowns.end() && !it->second.IsReady()) return nullptr;

	return skill;
}

Character::CastTiming Character::BeginCast(const SkillData& skill)
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

	_actionTimer.Reset(timing.actionMs);
	if (timing.cooldownMs > 0)
		_skillCooldowns[skill.skillId].Reset(timing.cooldownMs);
	return timing;
}
void Character::OnDamaged(int damage, int attackerId, shared_ptr<Character> attacker)
{
	if (_statInfo.IsDead()) return;

	int actualDamage = _statInfo.OnDamaged(damage);
	// cout << "Object[" << GetId() << "] Hp - " << GetStat().GetHp() << " / " << GetStat().GetMaxHp() << endl;

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
		shared_ptr<SendBuffer> sendBuffer = PacketSerializer::MAKE_SC_DEAD(GetId());
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
		// cout << "Object[" << GetId() << "]에서 " << objectId << "제거" << endl;
	}
}
