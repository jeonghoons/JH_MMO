#include "pch.h"
#include "Character.h"

Character::Character(Protocol::ObjectType type) : MovableObject(type)
{
	
}

void Character::Update(float deltaTime)
{
	MovableObject::Update(deltaTime);
}

bool Character::Attack(int& skillId)
{
	if (_statInfo.IsDead()) return false;

	if (IsHit()) return false;

	if (skillId == 0)
	{
		skillId = (int)_objectInfo.player_type() * 100 + 1; // 몬스터 기본 공격
	}

	return true;
}

void Character::OnDamaged(int damage, shared_ptr<Character> attacker)
{
	if (_statInfo.IsDead()) return;

	int actualDamage = _statInfo.OnDamaged(damage);
	// cout << "Object[" << GetId() << "] Hp - " << GetStat().GetHp() << " / " << GetStat().GetMaxHp() << endl;

	_hitTimer.Reset(300);

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
