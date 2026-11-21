#pragma once
#include "pch.h"

class StatComponent
{
public:
	void Init(Protocol::StatInfo* info) { _statInfo = info; }

	// 테이블 기본 스탯으로 StatInfo를 채우고(HP는 최대치) 이 컴포넌트에 연결한다. (플레이어·몬스터 공용)
	void InitFromData(Protocol::StatInfo* info, const StatData& data)
	{
		info->set_max_hp(data.maxHp);
		info->set_hp(data.maxHp);
		info->set_attack_damage(data.attackDamage);
		info->set_attack_speed(data.attackSpeed);
		info->set_move_speed(data.moveSpeed);
		info->set_max_mp(data.maxMp);   // 몬스터는 0
		info->set_mp(data.maxMp);
		Init(info);
	}

	// Init 전에 접근해도 쓰레기 포인터를 읽지 않도록 모든 접근에서 null을 검사한다. (Init 전 조회는 0을 반환)
	int GetHp() const { return _statInfo ? _statInfo->hp() : 0; }
	int GetMaxHp() const { return _statInfo ? _statInfo->max_hp() : 0; }
	int GetAttackDamage() const { return _statInfo ? _statInfo->attack_damage() : 0; }
	float GetAttackSpeed() const { return _statInfo ? _statInfo->attack_speed() : 0.f; }
	float GetMoveSpeed() const { return _statInfo ? _statInfo->move_speed() : 0.f; }
	int GetMp() const { return _statInfo ? _statInfo->mp() : 0; }
	int GetMaxMp() const { return _statInfo ? _statInfo->max_mp() : 0; }
	// MP를 0 ~ 최대치 안으로 넣는다(스킬 소모·자연 회복 — A2.3b). 패킷은 부른 쪽이 보낸다
	void SetMp(int mp) { if (_statInfo) _statInfo->set_mp(std::clamp(mp, 0, _statInfo->max_mp())); }
	// HP·MP를 최대치로(레벨업 — D-253). 죽어 있으면 HP는 그대로 둔다(되살리지 않음 — 부활은 CS_REVIVE)
	void FillHpMp()
	{
		if (_statInfo == nullptr) return;
		if (_statInfo->hp() > 0) _statInfo->set_hp(_statInfo->max_hp());
		_statInfo->set_mp(_statInfo->max_mp());
	}

	// 장비 반영 후 최종 스탯을 넣는다(플레이어, MC3.2). HP·MP는 새 최대치로 자르기만 하고 회복하지 않는다.
	// bonusMaxMp = 장비의 최대 MP(투구 — IT3에서 ItemTable 열을 더한다, 지금은 0)
	void ApplyFinalStat(const StatData& base, int bonusAttackDamage, int bonusMaxHp, int bonusMaxMp)
	{
		if (_statInfo == nullptr) return;
		const int maxHp = base.maxHp + bonusMaxHp;
		_statInfo->set_max_hp(maxHp);
		_statInfo->set_hp(std::min(_statInfo->hp(), maxHp));
		_statInfo->set_attack_damage(base.attackDamage + bonusAttackDamage);
		_statInfo->set_attack_speed(base.attackSpeed);
		_statInfo->set_move_speed(base.moveSpeed);
		const int maxMp = base.maxMp + bonusMaxMp;
		_statInfo->set_max_mp(maxMp);
		_statInfo->set_mp(std::min(_statInfo->mp(), maxMp));
	}

	bool IsDead() const { return _statInfo == nullptr || _statInfo->hp() <= 0; }

	int OnDamaged(int damage)
	{
		if (IsDead()) return 0;
		int currentHp = _statInfo->hp();
		int actualDamage = std::min(_statInfo->hp(), damage);
		_statInfo->set_hp(currentHp - actualDamage);
		return actualDamage;
	}

	// 부활(A2.4): 죽은 상태에서만 HP를 최대치의 percent%로 채운다(최소 1). 반환 = 부활했는가
	bool Revive(int percent)
	{
		if (_statInfo == nullptr || _statInfo->hp() > 0) return false;
		_statInfo->set_hp(std::max(1, static_cast<int>(static_cast<long long>(_statInfo->max_hp()) * percent / 100)));
		return true;
	}

	void OnHealed(int amount)
	{
		if (IsDead()) return;
		_statInfo->set_hp(std::min(_statInfo->max_hp(), _statInfo->hp() + amount));
	}

private:
	Protocol::StatInfo* _statInfo = nullptr;
};

