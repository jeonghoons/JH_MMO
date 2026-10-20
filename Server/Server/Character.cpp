#include "pch.h"
#include "Character.h"
#include "ServerData.h"

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

	// 클라이언트가 공격 패킷을 연달아 보내도 스킬 쿨타임 안에서는 받아들이지 않는다.
	if (GetType() == Protocol::ObjectType::OBJECT_TYPE_PLAYER)
	{
		auto it = _skillCooldowns.find(skill->skillId);
		if (it != _skillCooldowns.end() && !it->second.IsReady()) return nullptr;
	}

	return skill;
}

void Character::BeginSkillCooldown(const SkillData& skill)
{
	long long cooldownMs = skill.cooldownMs;
	if (skill.slot == 0)
	{
		const float attackSpeed = _statInfo.GetAttackSpeed();
		cooldownMs = static_cast<long long>(skill.cooldownMs / (attackSpeed > 0.f ? attackSpeed : 1.f));
	}
	_skillCooldowns[skill.skillId].Reset(cooldownMs);
}
void Character::OnDamaged(int damage, shared_ptr<Character> attacker)
{
	if (_statInfo.IsDead()) return;

	int actualDamage = _statInfo.OnDamaged(damage);
	// cout << "Object[" << GetId() << "] Hp - " << GetStat().GetHp() << " / " << GetStat().GetMaxHp() << endl;

	_hitTimer.Reset(GetHitStunMs());

	auto room = GetCurrentRoom();
	if (room) {
		shared_ptr<SendBuffer> damageBuffer = PacketSerializer::MAKE_SC_DAMAGE(attacker->GetId(), GetId(), actualDamage, _statInfo.GetHp());
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
