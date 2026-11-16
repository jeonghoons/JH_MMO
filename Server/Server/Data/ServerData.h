#pragma once
#include <unordered_map>
#include <array>

struct DB_PlayerInfo {
	int64_t playerUID;
	int64_t accountUID;
	WCHAR playerName[20];
	int32_t playerType;
};

// 진행 상황 = Player_Info에 저장하는 값(A2.5). 저장할 필드가 늘면 여기에 추가한다 —
// 골드는 여기에 없다(A3.3, D-152): 아이템 트랜잭션 안에서 증감하고 스냅숏으로 덮어쓰지 않는다.
// 비교(operator==)로 "바뀌었는가"를 판단하므로 값을 바꾸는 코드마다 표시를 넣을 필요가 없다.
// 함께 고칠 곳: Player::MakeProgressSnapshot, DbQueries::UpdatePlayerProgress, DatabaseWorker::SavePlayerProgress.
struct DB_PlayerProgress {
	int32_t level = 0;
	int64_t exp = 0;
	int32_t hp = 0;

	bool operator==(const DB_PlayerProgress&) const = default;
};

// 액티브 칸(숫자키 1~5) 수와 칸별 아이템 종류(ItemTable ID, 빈 칸 0). 인덱스 = 칸 번호 - 1 (A3.6, D-154)
constexpr int QUICK_SLOT_COUNT = 5;
using QuickSlotArray = std::array<int32_t, QUICK_SLOT_COUNT>;

// Player_Item.Location (DESIGN_ITEMS_TRADE "아이템 인스턴스와 DB")
constexpr int ITEM_LOC_INVENTORY = 1;
constexpr int ITEM_LOC_EQUIPPED = 2;
constexpr int ITEM_LOC_AUCTION = 3;   // 경매 보관(에스크로, A4.3)
constexpr int ITEM_LOC_MAIL = 4;      // 수령함(A4.3)

// Player_Item 한 행 = 아이템 한 개(스택이면 한 묶음) (A3.3, D-153)
struct DB_ItemRow {
	int64_t itemDbId = 0;
	int32_t itemId = 0;
	int32_t count = 0;
	int32_t location = 0;    // ITEM_LOC_*
	int32_t equipSlot = 0;   // 장착 중이면 Protocol::EquipPart(1~7), 아니면 0
	int32_t bagSlot = -1;    // 인벤토리 칸 번호(0부터), 인벤토리가 아니면 -1 (A6.1)
};

// 아이템·골드의 소유가 바뀌는 DB 작업 하나 = 한 트랜잭션(A3.3, D-153·D-165).
// Room 잡이 검증하고 계획을 채워 DatabaseWorker::ExecItemOp로 보내면, 결과와 함께 Room::OnItemOpDone으로 그대로 돌아온다.
// 모든 UPDATE는 기대 상태를 WHERE에 적고, 바뀐 행 수가 계획과 다르면 롤백한다(메모리 검증이 놓친 경우의 두 번째 방어선).
enum class ItemOpKind
{
	Give,       // 개발 지급: 덜 찬 스택 채우기 + 새 행 + 골드 증액 (조립식 쿼리)
	Equip,      // 장착·해제: 옛 장비·자동 해제 방패를 내리고 새 장비를 올린다 (고정 쿼리)
	LoginFix,   // 로그인 때 맞지 않은 장착 행을 인벤토리로 내린다
	Move,       // 인벤토리 칸 옮기기·맞바꾸기 (고정 쿼리, A6.1)
	Pickup,     // 바닥 드랍 줍기: Give와 같은 계획(fills·newCounts·goldDelta)을 쓴다 (고정 쿼리, A3.5)
	Discard,    // 버리기: 인벤토리 행 DELETE, 성공 뒤 바닥 드랍 생성 (고정 쿼리, A3.5)
	Use,        // 소비 아이템 사용: 수량 1 감소(1개였으면 DELETE), 성공 뒤 효과 적용 (고정 쿼리, A3.6)
};

struct DB_ItemOp {
	ItemOpKind kind = ItemOpKind::Give;
	int64_t playerUID = 0;
	long long requestUs = 0;      // 요청을 받은 시각(지표: 요청 → 적용)
	bool forceFail = false;       // 시험용([Debug] DbItemFailEvery): 트랜잭션을 끝에서 일부러 롤백한다

	// Give — Inventory::PlanAdd의 결과
	int32_t itemId = 0;                       // Give: 지급하는 종류(0 = 골드만) / Equip: 새 장비의 종류(0 = 해제)
	struct Fill { int64_t itemDbId; int32_t expectCount; int32_t add; };
	std::vector<Fill> fills;                  // 덜 찬 스택에 더하기 (expectCount = 지금 메모리의 수량)
	std::vector<int32_t> newCounts;           // 새 행의 수량 (행마다)
	std::vector<int32_t> newSlots;            // 새 행이 들어갈 인벤토리 칸 (newCounts와 같은 순서)
	int64_t goldDelta = 0;                    // 골드 증감(0 = 없음)
	int64_t goldMax = 0;                      // 증액 뒤 상한(EconomyTable.GoldMax)

	// Equip
	int32_t equipPart = 0;                    // Protocol::EquipPart
	int64_t newDbId = 0;                      // 올릴 행(0 = 해제만)
	int64_t oldDbId = 0;                      // 그 슬롯에 있던 행(0 = 비어 있었음)
	int64_t offhandDbId = 0;                  // 함께 내리는 방패(0 = 없음)
	int32_t newSlot = -1;                     // 올릴 행이 지금 있는 인벤토리 칸(기대 상태)
	int32_t oldSlot = -1;                     // 옛 장비가 내려갈 칸
	int32_t offhandSlot = -1;                 // 방패가 내려갈 칸

	// LoginFix
	std::vector<int64_t> demoteDbIds;         // Location 2 → 1
	std::vector<int32_t> demoteSlots;         // 내려갈 칸 (demoteDbIds와 같은 순서)

	// Move
	int64_t moveDbId = 0;                     // 옮길 행
	int32_t moveFrom = -1;                    // 지금 칸(기대 상태)
	int32_t moveTo = -1;                      // 대상 칸
	int64_t moveOtherDbId = 0;                // 대상 칸에 있던 행(맞바꿈, 0 = 빈 칸)

	// Pickup — 배치 계획은 Give의 필드(itemId·fills·newCounts·newSlots·goldDelta·goldMax)에 담는다
	int32_t dropId = 0;                       // 줍는 드랍의 오브젝트 id (결과가 오면 이 id로 다시 찾는다)

	// Discard — 버리는 종류는 itemId에 담는다
	int64_t discardDbId = 0;                  // 지울 행
	int32_t discardSlot = -1;                 // 그 행이 지금 있는 칸(기대 상태)
	int32_t discardCount = 0;                 // 그 행의 수량(기대 상태, 바닥 드랍의 수량이 된다)
	float discardX = 0.f, discardY = 0.f, discardZ = 0.f;   // 요청 때의 위치(결과가 왔을 때 방에 없으면 여기에 떨어뜨린다)

	// Use — 쓰는 종류는 itemId에 담는다
	int64_t useDbId = 0;                      // 1개를 쓸 행
	int32_t useExpectCount = 0;               // 그 행의 지금 수량(기대 상태)
};

struct DB_ItemOpResult {
	bool success = false;
	bool unknown = false;            // 실패인데 롤백됐는지 알 수 없다(실행 오류·결과 행 없음). DB에 반영됐을 수도 있다
	std::vector<int64_t> newDbIds;   // Give: newCounts 순서대로 발급된 ItemDbId
	int64_t gold = -1;               // 골드를 바꿨으면 바뀐 뒤 잔액, 아니면 -1
};

struct DB_PlayerData {
	int64_t playerUID;
	int32_t level;
	int64_t exp;
	int32_t hp;
	int64_t gold;
	int32_t neverSaved;   // 1 = 진행 상황을 저장한 적이 없다(Player_Info.UpdatedAt NULL) — 가입 직후, A2.5 전에 만든 캐릭터
	QuickSlotArray quickSlots{};   // Player_QuickSlot (빈 칸 0). 검증은 Player::InitFromDb가 한다 (A3.6)
	
	// Player_Item 행 중 인벤토리(Location 1)·장착(Location 2). ItemDbId 순. 검증은 Player::InitFromDb가 한다.
	std::vector<struct DB_ItemRow> items;
};

enum class HitShape
{
	Sector,     // 부채꼴 (근거리 즉시 타격)
	Line,       // 직선 (원거리 관통/즉시 타격)
	Projectile, // 투사체 (직선 + 단일 대상 + 비행 시간 지연 타격)
	Zone        // 지면 장판 (지정 지점에 원형 영역을 만들고 지속 시간 동안 주기적으로 피해)
};

enum class SkillTargeting
{
	NonTargeted, // 방향/지점을 지정한다
	Targeted      // 대상(액터)을 지정한다
};

// 스킬이 종속되는 무기 타입 (SkillTable.WeaponType, ItemTable.WeaponType). 플레이어는 장착 무기가 정하고(비면 Unarmed),
// 몬스터 스킬은 Monster로 두되 MonsterTable.SkillId로 찾는다. (Docs/DESIGN_COMBAT_SKILL.md)
enum class WeaponType
{
	None,      // ItemTable: 무기가 아닌 아이템
	Unarmed,
	Sword,
	Bow,
	Monster
};

// 장비 슬롯. 번호 = itemId 첫째 자리 = Protocol::EquipPart (MC3.2)
enum class ItemSlot
{
	None = 0,     // 장착할 수 없는 아이템(소비 아이템). itemId 첫째 자리는 8 (A3.2)
	Head = 1,
	Top = 2,
	Bottom = 3,
	Gloves = 4,
	Boots = 5,
	Weapon = 6,
	Offhand = 7   // 보조손(방패). 한손검 전용 (MC4.2f)
};

// 장비 티어. 번호 = itemId 둘째 자리.
enum class ItemTier
{
	None = 0,     // 티어 없음(소비 아이템)
	Cloth = 1,
	Leather = 2,
	Plate = 3
};

enum class ItemGrade
{
	Common,
	Uncommon,
	Rare
};

// 소비 아이템의 사용 효과 (ItemTable.UseEffect, UE EItemUseEffect와 이름이 같아야 한다)
enum class ItemUseEffect
{
	None,
	HealHp   // UseValue만큼 HP 회복
};

// ItemTable 중 서버가 읽는 열. 외형(메시·아이콘·이름)은 클라이언트 전용이라 읽지 않는다.
struct ItemData
{
	int itemId;            // 슬롯(1자리) + 티어(1자리) + 일련번호(2자리). 예: 6101 = 천 한손검, 8001 = HP 물약(슬롯 None = 8, 티어 None = 0)
	ItemSlot slot;
	ItemGrade grade;
	ItemTier tier;
	WeaponType weaponType; // 무기만 Sword/Bow, 나머지는 None
	int bonusAttackDamage; // 무기만
	int bonusMaxHp;        // 방어구만
	bool starter;          // 가입 시 기본 장비 (슬롯당 최대 1개, MC3.3)
	int requiredLevel;     // 장착(소비 아이템은 사용)에 필요한 레벨 (D-156)
	int maxStack;          // 한 칸에 겹치는 최대 수량. 장비는 1
	ItemUseEffect useEffect; // 장비는 None
	int useValue;          // HealHp: 회복량
	int useCooldownMs;     // 같은 아이템 종류의 재사용 시간
};

// DropTable 한 행 = 몬스터 한 종류가 떨어뜨릴 수 있는 것 하나. 처치 때 그 몬스터의 모든 행을 각각 독립으로 굴린다 (D-157). 서버 전용.
struct DropData
{
	int id;
	int monsterId;
	int itemId;      // 0 = 골드
	int dropRate;    // 만분율 (10000 = 100%)
	int minCount;    // 아이템 수량 또는 골드 금액 범위
	int maxCount;
};

// 드랍 롤 결과 하나. itemId 0 = 골드(count = 금액)
struct DropResult
{
	int itemId;
	int count;
};

// EconomyTable(행 1001 하나): 인벤토리·드랍·거래·경매 수치. 서버 전용 (Docs/DESIGN_ITEMS_TRADE.md)
struct EconomyData
{
	int inventorySlots;          // 인벤토리 칸 수(장착품 제외)
	int itemMinIntervalMs;       // 같은 플레이어의 아이템 요청 최소 간격
	int goldMax;                 // 보유 골드 상한
	int dropOwnerSec;            // 막타 소유 시간
	int dropLifeSec;             // 드랍이 사라지기까지 (dropOwnerSec 이상)
	int dropScatterCm;           // 사망 지점에서 흩뿌리는 반경
	int pickupRangeCm;           // 줍기 거리
	int maxDropsPerRoom;         // 방 하나의 드랍 상한
	int tradeRangeCm;            // 거래 요청·유지 거리
	int tradeRequestTimeoutSec;  // 요청 응답 대기
	int tradeTimeoutSec;         // 수락부터 완료까지
	int tradeMaxItems;           // 한 사람이 올리는 아이템 수
	int auctionDurationSec;      // 등록 기간
	int auctionMaxListings;      // 1인 등록 수
	int auctionMinPrice;
	int auctionMaxPrice;
	int auctionPageSize;         // 목록 한 쪽
	int auctionExpireCheckSec;   // 만료 검사 주기
};

// EconomyTable의 유일한 행
constexpr int ECONOMY_ID = 1001;

struct SkillData
{
	int skillId;
	WeaponType weaponType; // 이 스킬을 쓰는 무기 타입 (무기 종속)
	int slot;             // 0 = 기본 공격, 1~5 = 스킬 슬롯
	SkillTargeting targeting;
	float castRange;       // Targeted: 대상까지 최대 거리, Zone: 지점까지 최대 거리
	HitShape hitShape;
	int hitDelayMs;     // 발사 선딜레이 (손에서 이펙트가 떠나는 시간)
	int cooldownMs;     // 스킬 쿨타임(시전 순간부터). 기본 공격은 0
	int animMs;         // 행동 시간 = 애니메이션 1회. 이 동안 다음 시전 불가. 기본 공격은 AttackSpeed로 나눈다
	float radius;       // 부채꼴 반경
	float angle;        // 부채꼴 각도
	float range;        // 투사체/직선 사거리
	float width;        // 투사체/직선 폭
	int baseDamage;
	float projSpeed;    // 투사체 이동 속도 (units/sec)
	int durationMs;      // Zone: 장판 지속 시간
	int tickMs;          // Zone: 한 대상에게 피해를 주는 최소 간격
	float aimMoveSpeedMul; // 스킬 조준 중 이동 속도 배율 (0 < 값 <= 1, MC4.2d-1)
	int aimMaxMs;          // 스킬 조준 최대 유지 시간. 0 = 조준할 수 없는 스킬 (기본 공격은 항상 0)
};

// 캐릭터 기본 스탯 묶음. 플레이어는 PlayerStatTable(레벨별, 체형 공용), 몬스터는 MonsterTable(종류별)에서 읽는다.
// 피해 = 스킬 BaseDamage + 시전자 attackDamage (시전 순간 확정). 플레이어는 여기에 장비 보너스가 더해진다(MC3.2).
struct StatData
{
	int maxHp;
	int attackDamage;
	float attackSpeed;  // 기본 공격 애니메이션 재생 배율 (행동 시간·판정 시점 ÷ attackSpeed)
	float moveSpeed;
};

// 플레이어가 처음 갖는 레벨. 레벨 저장·성장은 A2.3·A2.5 (그 전까지 모든 플레이어가 이 레벨의 스탯을 쓴다).
constexpr int PLAYER_START_LEVEL = 1;

struct MonsterData
{
	int templateId;           // MonsterTable.Name(1001~) = 스폰 마커의 monsterTemplateId = ObjectInfo.template_id
	int skillId;              // 기본 공격 스킬 (SkillTable, WeaponType = Monster)
	StatData stat;            // 몬스터 고정 스탯(공격력 포함)
	int respawnMs;            // 사망 후 리스폰까지의 시간
	int deathRemoveDelayMs;   // 사망 연출을 위해 시체를 남겨 두는 시간
	int hitStunMs;            // 피격 후 행동(공격)이 막히는 시간. 피격 모션 길이에 맞춘다
	int wanderRadiusCm;       // 대기 중 배회: 현재 위치에서 이 반경 안 임의 지점 (임시 — 행동트리 개선 때 재검토)
};

// MovementTable 중 서버가 쓰는 열. 이동 속도 자체는 스탯(PlayerStatTable·MonsterTable의 MoveSpeed)을 쓰고, 여기는 검증 허용치만 둔다.
struct MovementData
{
	int id;
	float speedTolerance;      // 허용 속도 배율 (MoveSpeed × 배율까지 인정)
	int moveBudgetMs;          // 이동 거리 예산을 최대 몇 ms 분량까지 모아 둘지 (지연으로 몰려 온 패킷 흡수)
	float moveSlackCm;         // 예산과 별도로 항상 인정하는 여유 거리
	int minMoveIntervalMs;     // 이보다 잦은 이동 패킷은 무시한다
	float navToleranceCm;      // 목표가 내비메시 경계 밖으로 이만큼(수평)까지는 허용. 벽 두께 + 양쪽 침식(34cm×2)보다 작아야 벽을 못 뚫는다
	int weaponSwapLockMs;      // 무기 슬롯이 바뀌면(착용·해제) 이 시간 동안 공용 행동 잠금 (MC3.2)
	int equipMinIntervalMs;    // 같은 캐릭터의 장착 요청 최소 간격 (MC3.2)
	int aimMinIntervalMs;      // 같은 캐릭터의 스킬 조준 켜기 요청 최소 간격 — 끄기는 제한하지 않는다 (MC4.2d-1)
};

// MovementTable에서 플레이어 기본값을 담은 행 (클라이언트 UDataManager도 같은 행을 쓴다)
constexpr int PLAYER_MOVEMENT_ID = 1001;

// WorldTable: 맵별 시야(AOI) 수치와 부활 규칙. 서버 전용(클라이언트는 읽지 않는다).
struct WorldData
{
	int id;
	std::string mapName;   // 맵 파일 이름(GameMap::LoadMapData 인자)과 같아야 한다
	int cellSizeCm;        // AOI 큰 셀 한 변 길이(셀마다 쿼드트리)
	int quadSplitCount;    // 쿼드트리 노드가 이 수를 넘으면 4분할
	int quadMinSizeCm;     // 이 크기보다 작은 노드로는 나누지 않는다
	int viewRadiusCm;      // 목표 시야 반경 R: 이 거리 안의 캐릭터는 반드시 보여야 한다
	int viewHysteresisCm;  // 히스테리시스 H: 나갈 때는 R+H를 넘어야 사라진다(경계 깜빡임 방지)
	int reviveDelaySec;    // 플레이어가 죽은 뒤 이 시간이 지나야 부활 요청(CS_REVIVE)을 받는다 (A2.4)
	int reviveHpPercent;   // 부활 HP = 최대 HP × 이 값 / 100 (최소 1)
};

class DataManager
{
public:
	// Common/DataTable/PlayerStatTable.csv, SkillTable.csv, MonsterTable.csv, ItemTable.csv, MovementTable.csv, WorldTable.csv, EconomyTable.csv, DropTable.csv 를 읽는다. 실패하면 false (서버 시작을 중단해야 한다).
	static bool Init();

	// 몬스터 한 종류의 DropTable 행들. 드랍이 없는 몬스터는 빈 목록
	static const std::vector<DropData>& GetDrops(int monsterId)
	{
		static const std::vector<DropData> kEmpty;
		auto it = _dropTable.find(monsterId);
		return it != _dropTable.end() ? it->second : kEmpty;
	}

	// 처치 1회의 드랍 롤: 그 몬스터의 모든 행을 각각 독립으로 굴려 나온 것을 out에 더한다 (D-157).
	static void RollDrops(int monsterId, std::vector<DropResult>& out);

	// 시험용(--roll-drops <kills>): 몬스터마다 kills번 굴린 드랍 빈도를 기대값과 함께 로그에 남긴다.
	static void LogDropRollTest(int kills);

	// Init이 성공한 뒤에만 부른다.
	static const EconomyData& GetEconomy() { return _economy; }

	// 가입 시 지급하는 기본 장비(ItemTable.Starter = 1), 슬롯 순서
	static const std::vector<const ItemData*>& GetStarterItems() { return _starterItems; }

	static const ItemData* GetItemData(int itemId)
	{
		auto it = _itemTable.find(itemId);
		if (it != _itemTable.end()) return &it->second;
		return nullptr;
	}

	static const SkillData* GetSkillData(int skillId)
	{
		auto it = _skillTable.find(skillId);
		if (it != _skillTable.end()) return &it->second;
		return nullptr;
	}

	// 무기 타입의 슬롯에 해당하는 플레이어 스킬. 없으면 nullptr (몬스터 스킬은 MonsterData.skillId로 찾는다)
	static const SkillData* GetSkillBySlot(WeaponType weaponType, int slot)
	{
		auto it = _skillBySlot.find(static_cast<int>(weaponType) * 100 + slot);
		if (it == _skillBySlot.end()) return nullptr;
		return GetSkillData(it->second);
	}

	static const MonsterData* GetMonsterData(int templateId)
	{
		auto it = _monsterTable.find(templateId);
		if (it != _monsterTable.end()) return &it->second;
		return nullptr;
	}

	static const MovementData* GetMovementData(int id)
	{
		auto it = _movementTable.find(id);
		if (it != _movementTable.end()) return &it->second;
		return nullptr;
	}

	// 플레이어 기본 스탯(체형 공용). 없는 레벨이면 nullptr
	static const StatData* GetPlayerStatData(int level)
	{
		auto it = _playerStatTable.find(level);
		if (it != _playerStatTable.end()) return &it->second;
		return nullptr;
	}

	static const WorldData* GetWorldData(const std::string& mapName)
	{
		auto it = _worldTable.find(mapName);
		if (it != _worldTable.end()) return &it->second;
		return nullptr;
	}

private:
	static inline std::unordered_map<int, SkillData> _skillTable;
	static inline std::unordered_map<int, StatData> _playerStatTable; // key = Level
	static inline std::unordered_map<int, MonsterData> _monsterTable;
	static inline std::unordered_map<int, MovementData> _movementTable;
	static inline std::unordered_map<std::string, WorldData> _worldTable; // key = mapName
	static inline std::unordered_map<int, ItemData> _itemTable;
	static inline std::vector<const ItemData*> _starterItems;
	static inline std::unordered_map<int, std::vector<DropData>> _dropTable; // key = monsterId
	static inline EconomyData _economy{};
	static inline std::unordered_map<int, int> _skillBySlot; // key = (int)WeaponType*100 + slot -> skillId (플레이어 무기 타입만)
};