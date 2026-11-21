#pragma once
#include "pch.h"
#include "Game/Object/Character.h"
#include "Session/GameSession.h"
#include "Game/Room/Room.h"
#include "Game/Item/Inventory.h"

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
	const struct WorldData* CurrentWorldData() const;
public:

	// DB에서 읽은 값(레벨·HP·인벤토리·장비)을 반영한다(MC3.3·A2.5·A3.3). DB 레벨의 PlayerStatTable 행이 없으면 false(로그인 거부).
	bool InitFromDb(const DB_PlayerInfo& info, const DB_PlayerData& data);
	int64_t GetPlayerUID() const { return _dbPlayerInfo.playerUID; }

	void SetPlayerType(Protocol::PlayerType type) { _objectInfo.set_player_type(type); }
	int channel = 0;   // 로그인 때 WorldService가 배정하고 접속이 끝날 때까지 바뀌지 않는다(D-215). 0 = 아직 배정 전
	bool isDummy = false;

	// 캐릭터 이름(Player_Info.Name). 로그인 때 정해지고 바뀌지 않는다. ObjectInfo.name(UTF-8)에도 실어 HUD·다른 플레이어에게 보낸다(A2.3a).
	void SetName(const wstring& name) { _name = name; _objectInfo.set_name(Utils::WStringToUTF8(name)); }
	const wstring& GetName() const { return _name; }

	// 사망·부활(A2.4, D-149). Room 잡 안에서만 읽고 쓴다.
	long long deadAtMs = 0;                // 죽은 시각(steady_clock ms). 부활 요청은 이 시각 + ReviveDelaySec 뒤부터 받는다
	Protocol::PositionInfo spawnPosition;  // 방에 들어올 때 배치된 자리 = 구역이 없는 Room의 부활 위치(구역이 있는 Room은 부활할 때마다 시작 지점을 새로 고른다 — DG3b)

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

	// 이동 잠금(D-199): 시전부터 휘두르기가 끝날 때까지(SkillTable.MoveLockMs) 이동을 받지 않는다. 잠금 동안에는 이동 예산이 쌓이지 않고,
	// 시작할 때 예산을 ServerMoveLockSlackCm로 줄인다(시전 순간 서버가 아는 위치와 클라이언트가 멈춘 위치의 차이만 받아 준다).
	long long moveLockUntilMs = 0;
	bool IsMoveLocked(long long nowMs) const { return nowMs < moveLockUntilMs; }
	void BeginMoveLock(long long nowMs, long long lockMs, const MovementData& rule);

	// 포털 재사용 대기(L5, WorldTable.PortalCooldownMs). Room 잡 안에서만 읽고 쓴다.
	Cooldown portalCooldown;

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

	// 가진 아이템(인벤토리 + 장착)의 메모리 사본(A3.3). 권위는 DB — DB 트랜잭션이 성공한 뒤에만 바꾼다(Room::OnItemOpDone).
	// 장착 7칸의 itemId는 ObjectInfo.equip_items에도 있다(다른 플레이어에게 보이는 값, SetEquippedItem).
	Inventory inventory;

	// 아이템 작업 상태(A3.3, D-153). Room 잡 안에서만 읽고 쓴다.
	// 아이템·골드의 소유가 바뀌는 DB 작업은 플레이어당 동시에 하나만 보낸다. 진행 중에 온 요청은 쌓지 않고 BUSY로 거부한다.
	struct ItemOpState
	{
		bool inFlight = false;   // DB에 가 있는 아이템 작업이 있다
		// 진행 중인 작업이 무기 슬롯 장착·해제다. 결과가 올 때까지 시전을 받지 않는다(Room::CastSkill) —
		// 받으면 시전 도중 무기가 바뀌고, 결과 적용 때 거는 스왑 잠금이 시전의 행동 잠금을 덮어써 짧아진다.
		bool weaponSwapPending = false;
		Cooldown interval;       // 아이템 요청 최소 간격(EconomyTable.ItemMinIntervalMs). 장착은 equipCooldown을 쓴다
	};
	ItemOpState itemOp;
	// 경매 조회(검색·내 목록) 요청의 최소 간격(EconomyTable.ItemMinIntervalMs). 조회는 itemOp를 쓰지 않아 간격을 따로 잰다 (A4.3). Room 잡 안에서만
	Cooldown auctionQuery;

	// 지금 클라이언트에 보이는 바닥 드랍 id(A3.4). 캐릭터 시야(_viewList)와 따로 관리한다(Room::UpdateDropView).
	// Room 잡 안에서만 읽고 쓴다. 방에 들어올 때·나갈 때 비운다.
	std::unordered_set<int> dropView;
	// 지금 클라이언트에 보이는 마을 NPC id(L4). 드랍 시야와 같은 규칙으로 따로 관리한다(Room::UpdateNpcView).
	std::unordered_set<int> npcView;

	// 로그인 때 맞지 않아 메모리에서 인벤토리로 내린 장착 행(슬롯 불일치, 검이 아닌데 방패) = (ItemDbId, 내려갈 인벤토리 칸).
	// 방에 들어올 때 DB도 고친다(Room::PlayerEnterRoom).
	std::vector<std::pair<int64_t, int32_t>> pendingEquipFix;

	// 저장 상태(A2.5). Room 잡 안에서만 읽고 쓴다. 저장은 플레이어당 동시에 하나만 보내고,
	// 진행 중에 또 요청되면 dirty만 표시했다가 끝난 뒤 최신 값으로 한 번 더 저장한다(순서 보장).
	// 아이템·골드는 스냅숏 저장이 아니라 itemOp(즉시 트랜잭션)로 저장한다. 장비 스냅숏 저장(MC3.3 equipSave)은 A3.3에서 없앴다.
	struct SaveState
	{
		bool inFlight = false;   // DB에 가 있는 저장이 있다
		bool dirty = false;      // 진행 중에 또 요청됐거나 실패해 다시 저장해야 한다
	};
	SaveState progressSave;    // Player_Info 레벨·경험치·HP — 주기·퇴장 때, 값이 달라졌을 때만 (Room::RequestProgressSave)
	SaveState quickSlotSave;   // Player_QuickSlot 5칸 — 배치를 바꿀 때·퇴장 때, 값이 달라졌을 때만 (Room::RequestQuickSlotSave)
	SaveState questSave;       // Player_Quest.Progress — 처치가 인정될 때마다·퇴장 때, 값이 커졌을 때만 (Room::RequestQuestSave, Q1)

	// ---- 퀘스트(Q1, D-259·D-261). 소유 Room 잡 안에서만 읽고 쓴다 ----
	// 받은 퀘스트(진행 중 + 완료)의 메모리 사본. 권위는 DB(Player_Quest) — 수락·완료는 아이템 작업이 성공한 뒤에만 바꾸고(Room::ApplyItemOpResult),
	// 처치 진행(progress)만 메모리에 먼저 올린 뒤 저장한다(Room::CreditKill → RequestQuestSave). 없는 항목 = 받지 않은 퀘스트.
	struct QuestEntry
	{
		Protocol::QuestState state = Protocol::QUEST_STATE_NONE;
		int32_t progress = 0;   // 처치형의 처치 수(0 ~ QuestTable.Count). 수집형은 0
	};
	std::map<int32_t, QuestEntry> quests;                    // QuestId → 상태
	std::map<int32_t, int32_t> lastSavedQuestProgress;       // 진행 중인 퀘스트의 DB 값(로그인 때 읽은 값 → 수락·저장 성공 때마다 갱신)
	int questSaveRetries = 0;                                // 방을 나간 뒤 마지막 저장이 실패해 다시 시도한 횟수
	Protocol::QuestState GetQuestState(int32_t questId) const
	{
		const auto it = quests.find(questId);
		return it == quests.end() ? Protocol::QUEST_STATE_NONE : it->second.state;
	}
	// 지금 저장할 것: 진행 중인 퀘스트 가운데 메모리의 처치 수가 DB 값보다 큰 것. 비어 있으면 저장할 것이 없다.
	QuestProgressList MakeQuestSaveList() const;
	// 로그인 때 수령함에 받을 것이 있었다(DB_PlayerData.hasMail) — 처음 입장 때 한 번 알린다 (D-262)
	bool hasMailAtLogin = false;

	// 진행 중인 1:1 거래의 id(Room::_trades의 키, 0 = 거래 중 아님). 거래 상태 자체는 Room이 갖는다 (A4.1, D-158). Room 잡 안에서만 읽고 쓴다.
	int tradeId = 0;

	// 파티 번호의 사본(DG2, 0 = 파티 없음). 권위는 WorldService의 명부이고, 바뀔 때마다 서비스가 소유 Room 잡으로 알린다(Room::SetPlayerParty).
	// 드랍 소유권 판정에 쓴다. 소유 Room 잡 안에서만 읽고 쓴다. partySeq = 마지막으로 적용한 알림의 순번(늦게 온 옛 알림을 거른다).
	int partyId = 0;
	uint32_t partySeq = 0;

	// 액티브 칸 배치(A3.6, D-154): 칸 1~5에 올린 아이템 종류(빈 칸 0). 소유가 바뀌지 않는 설정값이라 메모리에 먼저 반영하고 저장한다.
	// Room 잡 안에서만 읽고 쓴다.
	QuickSlotArray quickSlots{};
	QuickSlotArray lastSavedQuickSlots{};   // DB에 있는 값(로그인 때 읽은 값 → 저장 성공 때마다 갱신)
	int quickSlotSaveRetries = 0;           // 방을 나간 뒤 마지막 저장이 실패해 다시 시도한 횟수

	// 소비 아이템 재사용 시간(A3.6): 아이템 종류 → 다음에 쓸 수 있는 시각(steady_clock ms). 저장하지 않는다(재접속하면 초기화).
	std::unordered_map<int, long long> itemCooldownUntilMs;

	// 진행 상황 저장(A2.5). Room 잡 안에서만 읽고 쓴다.
	// 지금 값의 스냅숏. 죽어 있으면 HP는 부활 규칙 값(최대치 × WorldTable.ReviveHpPercent) — 다음 접속은 살아서 입장한다.
	DB_PlayerProgress MakeProgressSnapshot() const;
	DB_PlayerProgress lastSavedProgress;   // DB에 있는 값(로그인 때 읽은 값 → 저장 성공 때마다 갱신)
	long long nextProgressSaveMs = 0;      // 다음 주기 저장 시각(steady_clock ms). 방에 들어올 때 정한다
	int progressSaveRetries = 0;           // 방을 나간 뒤 마지막 저장이 실패해 다시 시도한 횟수

	// 접속 목록에서 빼라는 신호(AuthLobby::OnPlayerClosed)를 이미 보냈다(A2.7). 소유 Room 잡에서 쓰지만,
	// Room 이동과 접속 해제가 겹치는 순간에 두 Room이 함께 볼 수 있어 원자적으로 둔다(DG3b).
	atomic<bool> closedNotified = false;

	// ---- Room 간 이동 상태(DG3b, D-216 — DESIGN_PARTY_DUNGEON §4-2). 이 플레이어를 가진 Room의 잡 안에서만 읽고 쓴다 ----
	using MoveReason = Player_MoveReason;
	struct RoomMoveState
	{
		bool waiting = false;     // ② 끝나지 않은 일(DB 작업·저장)이 끝나기를 기다리는 중
		bool transfer = false;    // ③ 이동 표시: 도착 Room을 물어본 뒤 떠나기까지. 이 동안 이 플레이어의 요청은 버린다
		MoveReason reason = MoveReason::None;
		MoveRequest request;      // 목적지(구역 Room / 다른 레벨 포털 / 정해진 인스턴스 — DG3c)
		bool hasDestPos = false;  // true = destPos에 도착(같은 레벨 포털의 도착 마커), false = 도착 Room이 자리를 고른다(부활·내보냄·다른 레벨)
		Protocol::PositionInfo destPos;
		long long deadlineMs = 0; // 대기 시한(steady_clock ms)
		bool crossLevel = false;  // 도착 Room의 레벨이 출발 Room과 다르다(떠날 때 출발 Room이 적는다) → 도착 Room이 로딩 대기에 둔다
		bool Active() const { return waiting || transfer; }
	};
	RoomMoveState roomMove;
	// 서버가 내보내려는데(Room::EjectPlayer) 다른 이동의 응답을 기다리는 중(transfer)이었다 — 그 이동이 취소되면 내보낸다. 소유 Room 잡 안에서만.
	bool ejectPending = false;
	// DB에 가 있는 이 플레이어의 작업이 있는가(아이템 작업 — 확정 중인 1:1 거래의 양쪽·퀘스트 수락/완료 포함, 진행 상황·액티브 칸·퀘스트 진행 저장).
	// Room::NotifyClosedIfDone과 Room 이동이 같은 조건을 본다 — DB로 가는 플레이어 작업을 추가하면 여기에 넣는다.
	bool HasUnfinishedWork() const { return itemOp.inFlight || progressSave.inFlight || quickSlotSave.inFlight || questSave.inFlight; }

	// 경험치·레벨(A2.3b, D-253). exp = 지금 레벨 안의 누적(다음 레벨까지 PlayerStatTable.ExpToNext). 소유 Room 잡 안에서만.
	int64_t GetExp() const { return _dbPlayerData.exp; }
	struct ExpGain
	{
		int64_t gained = 0;   // 실제로 쌓은 양(이미 최대 레벨이었으면 0)
		int oldLevel = 0;
		int newLevel = 0;
		bool LeveledUp() const { return newLevel != oldLevel; }
	};
	// 경험치를 더하고 필요한 만큼 레벨을 올린다(한 번에 여러 레벨). 최대 레벨이면 0으로 고정하고 더 쌓지 않는다.
	// 레벨이 오르면 새 레벨 행으로 RecalcStat → HP·MP 가득. 패킷·명부 통지는 부른 쪽(Room::ApplyExpGain)이 한다.
	ExpGain AddExp(int64_t amount);

	// 자연 회복(A2.3b, D-253): Room의 1초 처리(Room::UpdateRegen)가 센다. 소유 Room 잡 안에서만.
	int hpRegenSec = 0;   // 전투 밖에서 HP가 덜 찬 채 지난 초. HP_REGEN_INTERVAL_SEC가 되면 회복하고 0으로
	int64_t GetGold() const { return _dbPlayerData.gold; }
	// DB 트랜잭션이 돌려준 잔액으로만 바꾼다(D-152). 메모리에서 더하거나 빼지 않는다.
	void SetGold(int64_t gold) { _dbPlayerData.gold = gold; }

protected:
	virtual const SkillData* GetSlotSkill(int slot) const override { return DataManager::GetSkillBySlot(GetWeaponType(), slot); }

private:
	weak_ptr<GameSession> _ownerSession;
	wstring _name;
	
	DB_PlayerInfo _dbPlayerInfo{};   // InitFromDb 전에는 playerUID 0 (저장하지 않음)
	DB_PlayerData _dbPlayerData{};
};



