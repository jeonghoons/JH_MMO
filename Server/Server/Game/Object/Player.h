#pragma once
#include "pch.h"
#include "Game/Object/Character.h"
#include "Session/GameSession.h"
#include "Game/Room/Room.h"

class Player : public Character
{
public:
	// Player();
	Player(shared_ptr<GameSession> ownerSession, Protocol::PlayerType type);
	virtual ~Player();

public:
	virtual void Update(float deltaTime) override;

	void SetOwnerSession(shared_ptr<GameSession> session) { _ownerSession = session; }
	shared_ptr<GameSession> GetSession() {return _ownerSession.lock();	}
	
	virtual void OnDamaged(int damage, int attackerId, std::shared_ptr<Character> attacker) override;
	virtual void OnDead(std::shared_ptr<Character> attacker) override;
	virtual int GetReviveDelayMs() const override;
public:

	// DB에서 읽은 값(레벨·HP·장비)을 반영한다(MC3.3·A2.5). DB 레벨의 PlayerStatTable 행이 없으면 false(로그인 거부).
	bool InitFromDb(const DB_PlayerInfo& info, const DB_PlayerData& data);
	int64_t GetPlayerUID() const { return _dbPlayerInfo.playerUID; }

	void SetPlayerType(Protocol::PlayerType type) { _objectInfo.set_player_type(type); }
	bool isDummy = false;

	// 캐릭터 이름(Player_Info.Name). 로그인 때 정해지고 바뀌지 않는다. ObjectInfo.name(UTF-8)에도 실어 HUD·다른 플레이어에게 보낸다(A2.3a).
	void SetName(const wstring& name) { _name = name; _objectInfo.set_name(Utils::WStringToUTF8(name)); }
	const wstring& GetName() const { return _name; }

	// 사망·부활(A2.4, D-149). Room 잡 안에서만 읽고 쓴다.
	long long deadAtMs = 0;                // 죽은 시각(steady_clock ms). 부활 요청은 이 시각 + ReviveDelaySec 뒤부터 받는다
	Protocol::PositionInfo spawnPosition;  // 방에 들어올 때 배치된 자리(Room::AddObject) = 부활 위치

	// 채팅 도배 방지. Room 잡 안에서만 읽고 쓴다.
	Cooldown chatCooldown;

	// 이동 검증 상태(S1, Room::PlayerMove). Room 잡 안에서만 읽고 쓴다. 방에 들어갈 때 초기화한다.
	struct MoveGuard
	{
		float budgetCm = 0.f;        // 지금까지 모인 이동 가능 거리(토큰 버킷)
		long long lastAccrueMs = 0;  // 예산을 마지막으로 채운 시각
		long long lastRecvMs = 0;    // 마지막으로 처리한 이동 패킷 시각(전송 빈도 제한)
		uint32_t correctionSeq = 0;  // 서버가 이 플레이어의 이동을 거부(보정)한 횟수
	};
	MoveGuard moveGuard;

	// 이동 예산을 지금 속도 상한(스탯 × 허용 배율 × 조준 배율)으로 nowMs까지 채운다. 상한 = BudgetMs 분량.
	// Room::PlayerMove와 조준 배율을 바꾸기 직전(SetSkillAim)에 부른다 — 배율이 바뀌기 전 시간은 이전 배율로 쌓이게.
	float GetMoveSpeedLimit(const MovementData& rule) const;
	void AccrueMoveBudget(long long nowMs, const MovementData& rule);

	// 스킬 조준 상태(MC4.2d-1, DECISIONS 2026-11-09). Room 잡 안에서만 읽고 쓴다. 방에 들어갈 때 초기화한다.
	struct AimState
	{
		int skillId = 0;            // 0 = 조준 안 함
		float speedMul = 1.f;       // 조준 중 이동 속도 배율 (SkillTable.AimMoveSpeedMul)
		long long deadlineMs = 0;   // 이 시각이 지나면 서버가 해제 (SkillTable.AimMaxMs)
		uint32_t seq = 0;           // 마지막으로 받은 CS_SKILL_AIM의 seq (거부·시간 초과 때 본인에게 돌려준다)
		Cooldown onCooldown;        // 켜기 요청 빈도 제한 (MovementTable.ServerAimMinIntervalMs)
	};
	AimState aim;
	bool IsSkillAiming() const { return aim.skillId != 0; }
	// 조준을 켜거나(skill != nullptr) 끈다(nullptr). 이동 예산을 이전 배율로 정산한 뒤 배율을 바꾸고 ObjectInfo.aim_skill_id도 맞춘다.
	// 패킷은 보내지 않는다(호출한 쪽이 상황에 맞게 보낸다). 반환 = 바꾸기 전에 조준 중이었는가.
	bool SetSkillAim(const SkillData* skill, long long nowMs);
	// 무기 타입 + 슬롯의 스킬 (시전 가능 여부는 보지 않음)
	const SkillData* FindSlotSkill(int slot) const { return GetSlotSkill(slot); }

	// 장착 무기의 타입. 무기 슬롯이 비면 Unarmed.
	WeaponType GetWeaponType() const;

	// 장착 (MC3.2). Room 잡 안에서만 부른다. 검증은 Room::PlayerChangeEquip이 하고, 여기서는 반영·스탯 재계산만 한다.
	// itemId 0 = 해제. 슬롯 순서 = Protocol::EquipPart(1~7) = ObjectInfo.equip_items[part - 1]. 7 = 보조손(방패, MC4.2f)
	static constexpr int EQUIP_SLOT_COUNT = 7;
	int GetEquippedItem(Protocol::EquipPart part) const;
	void SetEquippedItem(Protocol::EquipPart part, int itemId);
	// 기본 스탯(PlayerStatTable) + 장착 아이템 보너스로 최종 스탯을 다시 계산한다. HP는 자르기만 한다.
	void RecalcStat();

	// 장착 요청 빈도 제한. Room 잡 안에서만 읽고 쓴다.
	Cooldown equipCooldown;

	// 저장 상태(MC3.3·A2.5). Room 잡 안에서만 읽고 쓴다. 저장은 종류마다 플레이어당 동시에 하나만 보내고,
	// 진행 중에 또 요청되면 dirty만 표시했다가 끝난 뒤 최신 값으로 한 번 더 저장한다(순서 보장).
	// 저장 종류가 늘면(인벤토리 A3.3) SaveState를 하나 더 둔다.
	struct SaveState
	{
		bool inFlight = false;   // DB에 가 있는 저장이 있다
		bool dirty = false;      // 진행 중에 또 요청됐거나 실패해 다시 저장해야 한다
	};
	SaveState equipSave;       // Player_Equipped — 바뀔 때마다 즉시 (Room::RequestEquipSave)
	SaveState progressSave;    // Player_Info 레벨·경험치·HP·골드 — 주기·퇴장 때, 값이 달라졌을 때만 (Room::RequestProgressSave)

	// 진행 상황 저장(A2.5). Room 잡 안에서만 읽고 쓴다.
	// 지금 값의 스냅숏. 죽어 있으면 HP는 부활 규칙 값(최대치 × WorldTable.ReviveHpPercent) — 다음 접속은 살아서 입장한다.
	DB_PlayerProgress MakeProgressSnapshot() const;
	DB_PlayerProgress lastSavedProgress;   // DB에 있는 값(로그인 때 읽은 값 → 저장 성공 때마다 갱신)
	long long nextProgressSaveMs = 0;      // 다음 주기 저장 시각(steady_clock ms). 방에 들어올 때 정한다
	int progressSaveRetries = 0;           // 방을 나간 뒤 마지막 저장이 실패해 다시 시도한 횟수

	// 접속 목록에서 빼라는 신호(AuthLobby::OnPlayerClosed)를 이미 보냈다(A2.7). Room 잡 안에서만 읽고 쓴다.
	bool closedNotified = false;

	int64_t GetExp() const { return _dbPlayerData.exp; }
	int64_t GetGold() const { return _dbPlayerData.gold; }

protected:
	virtual const SkillData* GetSlotSkill(int slot) const override { return DataManager::GetSkillBySlot(GetWeaponType(), slot); }

private:
	weak_ptr<GameSession> _ownerSession;
	wstring _name;
	
	DB_PlayerInfo _dbPlayerInfo{};   // InitFromDb 전에는 playerUID 0 (저장하지 않음)
	DB_PlayerData _dbPlayerData{};
};



