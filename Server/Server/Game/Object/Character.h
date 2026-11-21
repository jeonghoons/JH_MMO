#pragma once
#include "Game/Object/GameObject.h"
#include	"StatComponent.h"

struct SkillData;

class Character : public MovableObject
{
public:
	Character(Protocol::ObjectType type);
	virtual ~Character() = default;

public:
	virtual void Update(float deltaTime) override;
	// attacker는 nullptr일 수 있다(투사체가 날아가는 동안 공격자가 접속을 끊거나 사라진 경우). 그래도 피해는 적용하고 attackerId를 그대로 알린다.
	virtual void OnDamaged(int damage, int attackerId, std::shared_ptr<Character> attacker);

	virtual void OnDead(std::shared_ptr<Character> attacker);

	// 시전이 확정됐을 때 서버가 적용한 시간(ms). (Docs/DECISIONS.md 2026-10-28)
	struct CastTiming
	{
		long long actionMs;     // 행동 잠금: 이 시간 동안 다음 시전 불가 (= 애니메이션 1회)
		long long hitDelayMs;   // 판정 시점
		long long cooldownMs;   // 스킬 쿨타임 (기본 공격은 0)
		long long moveLockMs;   // 이동 잠금 시간 (SkillTable.MoveLockMs, 기본 공격은 AttackSpeed로 나눈 값)
	};

	// 슬롯의 스킬을 지금 시전할 수 있는지 확인한다(부수 효과 없음).
	// 죽음/피격 경직/행동 중(이전 시전의 애니메이션)/스킬 쿨타임/해당 슬롯 스킬 없음이면 nullptr.
	// check를 주면 거부 사유·남은 시간을 채운다(CP1, D-211).
	// earlyToleranceMs(D-213): 앞 시전의 행동 잠금·스킬 쿨타임이 이 시간 이하로 남았으면 통과시키고, 남아 있던 시간을
	// check의 carry에 적는다(BeginCast에 넘겨 다음 잠금·쿨타임에 더함 — 공격 주기가 빨라지지 않게). 무기 스왑 잠금·피격 경직에는 적용하지 않는다.
	struct CastCheck
	{
		Protocol::AttackResult reason = Protocol::ATTACK_RESULT_NONE;
		long long remainMs = 0;          // ACTING·COOLDOWN으로 거부했을 때 남은 시간
		long long actionCarryMs = 0;     // 허용 오차로 통과했을 때 남아 있던 행동 잠금
		long long cooldownCarryMs = 0;   // 허용 오차로 통과했을 때 남아 있던 스킬 쿨타임
	};
	const SkillData* FindCastableSkill(int slot, CastCheck* check = nullptr, long long earlyToleranceMs = 0) const;
	// 시전이 확정되면 호출해 행동 잠금과 스킬 쿨타임(CooldownMs > 0)을 시작한다.
	// 기본 공격(슬롯 0)은 행동 시간과 판정 시점을 AttackSpeed(애니메이션 재생 배율)로 나눈다. 스킬은 원래 속도.
	// carry: 허용 오차로 일찍 받은 시간(FindCastableSkill의 check). 잠금·쿨타임 길이에만 더하고 CastTiming(클라이언트에 알리는 값·판정 시점)은 그대로다.
	CastTiming BeginCast(const SkillData& skill, long long actionCarryMs = 0, long long cooldownCarryMs = 0);
	// 스킬 쿨타임이 끝났는가 (쿨타임 기록이 없으면 준비됨)
	bool IsSkillReady(int skillId) const { auto it = _skillCooldowns.find(skillId); return it == _skillCooldowns.end() || it->second.IsReady(); }
	bool IsHit() const { return !_hitTimer.IsReady(); }
	bool IsActing() const { return !_actionTimer.IsReady(); }
	// 시전 애니메이션 중인가 (무기 스왑 잠금은 제외 — 스왑 잠금 중에도 방어구 교체는 받는다)
	bool IsCasting() const { return IsActing() && !_actionIsSwapLock; }
	// 시전 없이 공용 행동 잠금만 건다 (무기 스왑, DECISIONS 2026-11-04). 스킬 쿨타임은 건드리지 않는다.
	void LockActionForSwap(long long ms) { _actionTimer.Reset(ms); _actionIsSwapLock = true; }
	virtual int GetHitStunMs() const { return 300; }   // 피격 후 행동이 막히는 시간
	// 몸 반경(cm): 이 캐릭터를 맞히는 판정에 더한다. 몬스터는 MonsterTable.BodyRadiusCm, 플레이어는 0(중심점 판정) (MN1, D-198)
	virtual float GetBodyRadius() const { return 0.f; }
	// SC_DEAD에 싣는 부활 대기 시간(ms). 플레이어만 값이 있고 그 밖은 0 (A2.4)
	virtual int GetReviveDelayMs() const { return 0; }
	// 부활할 때 피격 경직·행동 잠금을 푼다(스킬 쿨타임은 그대로).
	void ClearActionLocks() { _hitTimer.Reset(0); _actionTimer.Reset(0); _actionIsSwapLock = false; }
protected:
	// 슬롯에 해당하는 스킬(시전 가능 여부는 보지 않음). 플레이어는 무기 타입, 몬스터는 MonsterTable.SkillId로 찾는다.
	virtual const SkillData* GetSlotSkill(int slot) const = 0;
public:
	StatComponent& GetStat() { return _statInfo; }
	const StatComponent& GetStat() const { return _statInfo; }

	void RemoveView(int objectId);
	vector<int>	_viewList;

	// 전투 판정(A2.3b, D-254): 마지막으로 때리거나 맞은 시각(steady_clock ms). 플레이어만 쓴다(자연 회복 — Room::UpdateRegen).
	// 그 플레이어의 소유 Room 잡에서만 쓴다(Character::OnDamaged가 같은 Room인지 확인한다).
	long long lastCombatMs = 0;
	// 전투 중 = 지금 - lastCombatMs < COMBAT_HOLD_MS(ServerData.h)
	bool IsInCombat(long long nowMs) const;
protected:
	StatComponent _statInfo;
	Cooldown _hitTimer;
	Cooldown _actionTimer;                               // 공용 행동 잠금 (기본 공격·스킬 공통)
	bool _actionIsSwapLock = false;                      // 지금 행동 잠금이 무기 스왑 때문인가 (BeginCast가 false로 되돌림)
	std::unordered_map<int, Cooldown> _skillCooldowns;   // 스킬별 쿨타임 (CooldownMs > 0인 스킬만)
};

