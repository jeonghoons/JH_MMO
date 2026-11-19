#pragma once
#include "pch.h"
#include "Job/JobQueue.h"
#include "Network/ServerService.h"
#include "Job/Timer.h"
#include "Game/World/GameMap.h"
#include "Data/ServerData.h"
#include <deque>

// 모든 Room이 쓰는 레벨 이름은 ServerConfig [World] MapName(GServerConfig.mapName), 정원은 WorldTable.RoomCapacity (D-172·D-176)
constexpr int DROP_ID_BEGIN = 1000000000;      // 드랍 오브젝트 id 시작(방마다 따로 증가, int32 끝에 닿으면 처음으로)
constexpr int NPC_ID_BEGIN = 900000000;        // 마을 NPC 오브젝트 id 시작(방을 만들 때 마커 순서대로 — 플레이어·몬스터·드랍과 겹치지 않음, L4)
constexpr int MAX_CHAT_LEN = 100;               // 채팅 한 줄 최대 글자 수(UTF-16 문자 기준)
constexpr long long CHAT_MIN_INTERVAL_MS = 500;  // 같은 플레이어의 채팅 최소 간격(도배 방지)

class Session;
class SendBuffer;
class GameObject;
class Character;
class Player;
class Monster;
class DropItem;
class TownNpc;

// ---- 1:1 거래 상태 (A4.1, D-158, DESIGN_ITEMS_TRADE "1:1 거래") ----
// 거래는 같은 방의 두 플레이어 사이에서만 하고, 상태는 Room이 갖는다(Player에는 tradeId만). 모든 전이는 Room 잡 안에서 일어난다.
// 취소는 이 메모리 상태만 버리면 된다 — 확정 전에는 DB를 건드리지 않는다.
struct TradeItem
{
	int64_t itemDbId = 0;
	int32_t itemId = 0;
	int64_t count = 0;    // 올린 수량(1 ~ 그 행의 수량)
};

struct TradeSide
{
	shared_ptr<class Player> player;   // 거래가 끝날 때까지 잡고 있는다(접속이 끊겨도 DB 결과를 반영할 대상을 안다)
	int64_t playerUID = 0;
	int objectId = 0;
	vector<TradeItem> items;
	int64_t gold = 0;
	bool locked = false;
	bool confirmed = false;
};

struct TradeSession
{
	int id = 0;
	Protocol::TradePhase phase = Protocol::TRADE_PHASE_REQUESTING;
	TradeSide sides[2];        // [0] = 요청한 쪽, [1] = 받은 쪽
	long long requestedAtMs = 0;   // 요청 시각(응답 시간 초과 판정)
	long long acceptedAtMs = 0;    // 수락 시각(전체 시간 초과 판정)
};
struct SkillData;

class Room : public enable_shared_from_this<Room>
{
public:
	Room(shared_ptr<Timer> timer, HANDLE iocpHandle);

	~Room();

	void InitRoom();

	template<typename... Arguments>
	void PushJob(void(Room::* memFunc)(Arguments...), Arguments... args)
	{

		_jobQueue->Push(shared_from_this(), memFunc, std::forward<Arguments>(args)...);
	}

	template<typename... Arguments>
	void ReserveJob(DWORD ectime, void(Room::* memFunc)(Arguments...), Arguments... args)
	{
		_timer->Reserve(ectime, _jobQueue, shared_from_this(), memFunc, std::forward<Arguments>(args)...);
	}
	void Update(long long elapsedTime);
	// 송신 묶기의 깨우기용 작업(P1b). 하는 일은 지표 기록뿐이고, 이 작업이 끝난 뒤 잡 큐가 때가 된 세션을 보낸다.
	// dueUs = 깨우기로 예약한 시각(Metrics::NowUs 기준) — 실제 실행이 얼마나 늦었는지 잰다.
	void OnSendWake(long long dueUs);
	// 공용
	bool AddObject(shared_ptr<GameObject> object);
	bool RemoveObject(int objectId);
	shared_ptr<GameObject> GetGameObject(int objectId) const
	{
		auto it = _objects.find(objectId);
		if (it == _objects.end()) {
			return nullptr;
		}
		return it->second;
	}
	void UpdateView(shared_ptr<Character> subjectChar, const ViewUpdate& result);

	// Player
	void PlayerEnterRoom(shared_ptr<Player> player);	
	void PlayerLeaveRoom(shared_ptr<Player> player);
	// 클라이언트가 예측한 위치를 검증한다(서버 권위 이동, S1). 통과하면 저장·방송, 아니면 본인에게 보정 위치를 보낸다.
	void PlayerMove(shared_ptr<Player> player, Protocol::PositionInfo position, uint32_t correctionSeq);
	void Broadcast(shared_ptr<SendBuffer> sendBuffer);
	void BroadcastAOI(shared_ptr<class Character> viewableObj, shared_ptr<SendBuffer> sendBuffer, bool sendToSelf = false);

	void PlayerChat(shared_ptr<Player> player, wstring msg);
	void PlayerPing(shared_ptr<Player> player, long long clientTime);
	// 장착 요청(MC3.2·A3.3)을 검증한다. itemDbId = 내 인벤토리의 행(0 = 해제). 통과하면 DB 트랜잭션을 보내고(StartItemOp),
	// 성공 결과가 돌아온 뒤(OnItemOpDone) 메모리에 반영해 SC_CHANGE_EQUIP을 AOI 전체(본인 포함)에 보낸다.
	// 거부·DB 실패는 SC_CHANGE_EQUIP_RESULT를 본인에게만.
	void PlayerChangeEquip(shared_ptr<Player> player, Protocol::EquipPart part, int64_t itemDbId);
	// 개발 지급 명령(A3.3, [Debug] DevCommands). itemId 0 = 골드. 다른 아이템 작업과 같은 경로를 탄다.
	void PlayerDevGive(shared_ptr<Player> player, int itemId, int64_t count);
	// 인벤토리 칸 옮기기(A6.1): 대상 칸이 비어 있으면 옮기고, 다른 아이템이 있으면 맞바꾼다. 칸 위치도 DB가 권위라 itemOp 경로를 탄다.
	void PlayerMoveItem(shared_ptr<Player> player, int64_t itemDbId, int toSlot);
	// 줍기(A3.5): 드랍 존재 → 줍는 중 아님 → 거리 → 소유권 → 빈 칸·골드 상한을 검증하고, 드랍에 줍는 중 표시를 한 뒤 DB로 보낸다.
	// 성공하면 드랍 제거(SC_REMOVE_OBJECT) + SC_INVENTORY_UPDATE. 줍는 중인 드랍에 온 다른 요청은 BUSY라 경쟁해도 한 명만 얻는다.
	void PlayerPickupItem(shared_ptr<Player> player, int dropId);
	// 버리기(A3.5, D-155): 인벤토리 위치의 행을 DB에서 지운 뒤, 그 자리 근처에 소유권 없는 드랍을 만든다(스택 통째).
	void PlayerDropItem(shared_ptr<Player> player, int64_t itemDbId);
	// ---- 상점 NPC (L4, D-201·D-202 — DESIGN_ITEMS_TRADE "상점 NPC") ----
	// 서버에 "상점을 연 상태"는 없다. 세 요청 모두 매번 방 안 → 더미 봇 아님 → 생존 → 요청 간격·진행 중인 작업 → 거래 중 아님 → NPC·상점 존재 → 거리를 본다.
	// 품목 요청: SC_SHOP_OPEN(품목·구매가). DB를 쓰지 않는다.
	void PlayerShopOpen(shared_ptr<Player> player, int npcId);
	// 구매: 품목 → 수량 → 골드 → 빈 칸(덜 찬 스택 먼저)을 검증하고 줍기와 같은 쿼리로 아이템 추가 + 골드 차감을 한 트랜잭션에 한다.
	void PlayerShopBuy(shared_ptr<Player> player, int npcId, int itemId, int count);
	// 판매: 인벤토리 위치의 내 행 → 수량(스택 일부 가능) → SellPrice > 0 → 골드 상한을 검증하고 행 삭제(또는 수량 감소) + 골드 증액을 한 트랜잭션에 한다.
	void PlayerShopSell(shared_ptr<Player> player, int npcId, int64_t itemDbId, int count);

	// 소비 아이템 사용(A3.6, D-154). itemDbId가 0이면 itemId 종류 중 수량이 적은 스택부터 고른다(액티브 칸).
	// 검증(보유 → 사용 효과 → 레벨 → 생존 → HP 최대 → 재사용 시간 → 요청 간격·진행 중인 작업) 뒤 DB에서 1개를 줄이고,
	// 성공하면 효과를 적용해 SC_USE_ITEM(AOI, 본인 포함) + SC_INVENTORY_UPDATE를 보낸다.
	void PlayerUseItem(shared_ptr<Player> player, int64_t itemDbId, int itemId);
	// 액티브 칸 배치(A3.6). slot 1~5, itemId 0 = 비우기. 메모리에 먼저 반영해 SC_INVENTORY_UPDATE(quick_slots)로 알리고 저장을 요청한다.
	void PlayerSetQuickSlot(shared_ptr<Player> player, int slot, int itemId);
	// 액티브 칸 5칸을 DB에 저장한다. DB에 있는 값과 다를 때만 보내고, 진행 중이면 끝난 뒤 최신 값으로 한 번 더 저장한다(순서 보장).
	void RequestQuickSlotSave(shared_ptr<Player> player);
	void OnQuickSlotsSaved(shared_ptr<Player> player, QuickSlotArray saved, bool success);
	// ---- 1:1 거래(A4.1·A4.2, D-158). 거부는 SC_TRADE_RESULT(본인), 진행은 SC_TRADE_STATE(두 사람), 끝은 SC_TRADE_END(두 사람) ----
	void TradeRequest(shared_ptr<Player> player, int targetId);
	void TradeRespond(shared_ptr<Player> player, bool accept);
	// 내 제시 전체를 교체한다. 누가 제시를 바꾸면 양쪽의 잠금·확정이 풀린다. tooMany = 패킷의 목록이 너무 길어 잘렸다(거부).
	void TradeSetOffer(shared_ptr<Player> player, vector<TradeItem> items, int64_t gold, bool tooMany);
	void TradeLock(shared_ptr<Player> player, bool locked);
	// 양쪽이 잠근 뒤에만 받는다. 양쪽이 확정하면 다시 검증하고 한 트랜잭션으로 교환한다(TryExecuteTrade).
	void TradeConfirm(shared_ptr<Player> player);
	void TradeCancel(shared_ptr<Player> player);
	// 이 플레이어가 하던 거래를 사유와 함께 끝낸다(퇴장·사망·거리 이탈). 확정 중(DB 결과 대기)이면 건드리지 않는다.
	void CancelTradeOf(const shared_ptr<Player>& player, Protocol::TradeResult reason);
	// 그 행을 진행 중인 거래에 올려 두었는가(일부만 올렸어도). 올린 행은 장착·버리기·사용을 거부한다(IN_TRADE).
	bool IsItemInTrade(const shared_ptr<Player>& player, int64_t itemDbId) const;
	// 아이템 작업의 DB 결과(A3.3, D-153). 성공이고 플레이어가 아직 방에 있으면 메모리·패킷에 반영한다.
	// 방을 나갔으면 반영을 건너뛴다(DB에는 이미 들어갔고 다음 로그인 때 읽는다). 끝에서 NotifyClosedIfDone을 부른다.
	void OnItemOpDone(shared_ptr<Player> player, DB_ItemOp op, DB_ItemOpResult result);
	// 진행 상황(레벨·경험치·HP)을 DB에 저장한다(A2.5). 골드는 아이템 트랜잭션에서 증감한다(A3.3). DB에 있는 값과 다를 때만 보낸다. 더미 봇은 저장하지 않는다
	// (전원이 dummy 계정 한 행을 쓴다). 주기(UpdateSaves)와 퇴장 때 부르고, 방을 나간 플레이어도 받는다.
	void RequestProgressSave(shared_ptr<Player> player);
	void OnProgressSaved(shared_ptr<Player> player, DB_PlayerProgress saved, bool success);
	// 1초에 한 번: 주기 저장 시각(ServerConfig Database.PlayerSaveIntervalSec)이 된 플레이어를 저장한다.
	void UpdateSaves(long long nowMs);
	// 중복 접속 차단(A2.7): 끊긴 플레이어가 방에 없고 DB에 가 있는 작업이 0건이면 로비에 알려 접속 목록에서 빼게 한다(플레이어당 한 번).
	// 플레이어가 방을 떠나는 경로와 그 플레이어의 DB 작업 결과를 받는 곳 **모두**에서 불러야 한다 — 빠지면 그 캐릭터는
	// 다음 로그인이 LoginCloseForceSec만큼 막힌다. 새 저장 종류·아이템 작업을 추가하면 여기 조건과 결과 처리 끝에 함께 넣는다.
	void NotifyClosedIfDone(const shared_ptr<Player>& player);
	// 스킬 조준 켜기/끄기(MC4.2d-1, DECISIONS 2026-11-09). 켜기 성공 = AOI 전체(본인 포함) 방송, 거부 = 본인에게만 false + seq,
	// 끄기 = 조준 중일 때만 해제하고 본인 제외 방송(본인 클라이언트는 이미 껐다).
	void PlayerSkillAim(shared_ptr<Player> player, int slot, bool active, uint32_t seq);
	// 부활 요청(A2.4, D-149). 죽어 있고 WorldTable.ReviveDelaySec가 지났을 때만 받는다(그 밖은 조용히 버림).
	// 입장 때 배치된 자리로 옮기고 HP를 채운 뒤 SC_REVIVE를 AOI 전체(본인 포함)에 보낸다.
	void PlayerRevive(shared_ptr<Player> player);
	// 포털 사용 요청(L5, D-203). 검증(방 안 → 생존 → 시전·이동 잠금 아님 → 재사용 대기 → 포털 존재 → 도착지 → 거리)을 통과하면
	// 도착지로 옮기고 SC_TELEPORT를 떠나는 자리의 AOI 전체(본인 포함)에 보낸다. 거부는 응답 없이 버린다.
	void PlayerUsePortal(shared_ptr<Player> player, int portalId);
	// 포털의 도착지를 정하는 유일한 곳(D-200·D-204). 지금은 같은 레벨의 짝 포털 자리만 받는다.
	// 도착지가 다른 Room이 되면(맵별 Room 분할 — DG1·DG3) 이 함수와 PlayerUsePortal의 적용 부분만 바꾼다.
	bool ResolvePortalTarget(const struct PortalTriggerPoint& portal, Protocol::PositionInfo& outPos) const;

	// 슬롯(0 = 기본 공격)의 스킬을 시전한다. 사거리/대상/방향은 서버가 검증·보정하며, 실패하면 아무 일도 일어나지 않는다.
	void CastSkill(shared_ptr<Character> caster, int slot, int targetId, float aimYaw, float aimX, float aimY);
	// damage: 시전 순간에 확정한 피해(BaseDamage + 시전자 공격력)
	void ExecuteSkillHit(int attackerId, int skillId, int targetId, float aimYaw, float aimX, float aimY, int damage);
	void ApplyDelayedDamage(int targetId, int attackerId, int damage);

	// 지면 장판 (HitShape::Zone)
	void CreateZone(shared_ptr<Character> attacker, const SkillData& skill, float x, float y, int damage);
	// Npc
	void NpcEnterRoom(shared_ptr<Monster> monster);
	// 스폰 마커 반경 안의 임의 위치에 몬스터 한 마리를 만든다. templateId·respawnMs는 SpawnTable 행(구역 이름이 없는 마커는 마커 값·0). respawnMs 0 = MonsterTable 값
	void SpawnMonster(int spawnIndex, int templateId, int respawnMs);
	void RemoveMonster(int monsterId);   // 사망한 몬스터를 월드에서 제거하고 리스폰을 예약한다.
	void NPCMove(shared_ptr<Monster> monster);
	int MonsterIdGenerator()
	{
		static atomic<int> _midGenerator = 100000;
		return ++_midGenerator;
	}
	// ---- 바닥 드랍(A3.4, D-157) ----
	// 몬스터가 죽었을 때 DropTable을 굴려 걸린 행마다 사망 지점 DropScatterCm 안(내비메시 위)에 드랍을 하나씩 만든다.
	// attacker(막타)가 개별 계정 플레이어면 DropOwnerSec 동안 소유권을 준다. nullptr(투사체가 날아가는 동안 퇴장)·더미 봇이면 소유권 없음.
	void SpawnMonsterDrops(int monsterTemplateId, Protocol::PositionInfo pos, shared_ptr<Character> attacker);
	// 드랍 하나를 만들어 주변 플레이어에게 SC_ADD_OBJECT를 보낸다. itemId 0 = 골드. ownerUID 0 = 소유권 없음.
	// MaxDropsPerRoom에 닿으면 가장 오래된 것부터 지운 뒤 만든다. DropLifeSec 뒤 소멸을 예약한다.
	shared_ptr<DropItem> CreateDrop(int itemId, int64_t count, const Protocol::PositionInfo& pos, int64_t ownerUID);
	// 드랍을 지우고 보고 있던 플레이어에게 SC_REMOVE_OBJECT를 보낸다(소멸·상한·줍기 공용). 없는 id면 false.
	bool RemoveDrop(int dropId);
	// 소멸 예약(ReserveJob)이 부른다. 이미 없거나(주움), 그 id의 새 드랍이거나, 줍는 중이면 아무것도 하지 않는다.
	void ExpireDrop(int dropId);
	shared_ptr<DropItem> GetDrop(int dropId) const
	{
		auto it = _drops.find(dropId);
		return it != _drops.end() ? it->second : nullptr;
	}

	NavmeshManager* GetNavManager() { return _gameMap.GetNavManager(); }
	const GameMap& GetGameMap() const { return _gameMap; }
	shared_ptr<Player> Id2Player(int pId);
	shared_ptr<Monster> Id2Monster(int mId);

	// 입장 예약(정원 확인 + 자리 확보)을 원자적으로 한다. 입장 처리(PlayerEnterRoom)는 JobQueue에서 나중에 실행되므로
	// _players.size()로 정원을 판단하면 동시에 들어오는 플레이어가 정원을 넘길 수 있다.
	bool TryReserveSlot()
	{
		int count = _playerCount.load();
		while (count < _capacity)
		{
			if (_playerCount.compare_exchange_weak(count, count + 1))
				return true;
		}
		return false;
	}
	void ReleaseSlot() { _playerCount.fetch_sub(1); }
	int NumPlayers() const { return _playerCount.load(); }

private:
	struct SkillZone
	{
		int attackerId = 0;
		float x = 0.f, y = 0.f;
		float radius = 0.f;
		int damage = 0;
		int tickMs = 1000;
		chrono::steady_clock::time_point endTime;
		unordered_map<int, chrono::steady_clock::time_point> nextHit;   // 대상별 다음 피해 가능 시각
	};
	vector<SkillZone> _zones;
	void UpdateZones();
	// AimMaxMs가 지난 조준을 해제한다(Update마다). 본인에게는 seq를 실어, 다른 사람에게는 seq 없이 보낸다.
	void UpdateAims(long long nowMs);

	// ---- 1:1 거래 내부(A4.1) ----
	TradeSession* FindTrade(const shared_ptr<Player>& player);
	const TradeSession* FindTrade(const shared_ptr<Player>& player) const;
	void SendTradeState(const TradeSession& trade);
	// 거래를 끝낸다: 두 사람에게 SC_TRADE_END(result), tradeId 해제, 상태 삭제.
	void EndTrade(int tradeId, Protocol::TradeResult result);
	// 양쪽 확정 뒤의 재검증. 통과하면 CONFIRMING으로 바꾸고 DB 트랜잭션을 보낸다. 실패하면 양쪽 잠금·확정을 풀고 사유를 두 사람에게 알린다.
	void TryExecuteTrade(TradeSession& trade);
	// 거래 DB 결과(OnItemOpDone의 Trade 분기)
	void OnTradeDone(const DB_ItemOp& op, const DB_ItemOpResult& result);
	// 요청 응답 시간·전체 시간 초과를 본다(1초마다).
	void UpdateTrades(long long nowMs);
	bool IsTradeRangeOk(const TradeSession& trade) const;

	// ---- 아이템 작업 공통(A3.3) ----
	// 이 플레이어 객체가 지금 이 방에 있는가(같은 id를 다른 플레이어가 다시 쓸 수 있으므로 포인터로 비교).
	bool IsInRoom(const shared_ptr<Player>& player) const;
	// 아이템 요청의 공통 검사: 방 안 → 더미 봇 아님 → 요청 간격 → 진행 중인 작업 없음. 통과하면 ITEM_RESULT_SUCCESS.
	Protocol::ItemResult CheckItemRequest(const shared_ptr<Player>& player);
	// itemOp를 켜고 DB 작업을 보낸다. 결과는 OnItemOpDone으로 돌아온다.
	void StartItemOp(const shared_ptr<Player>& player, DB_ItemOp op);

	// 플레이어의 드랍 시야(Player::dropView)를 지금 위치 기준으로 맞춘다: 반경 R 안에 새로 들어온 드랍은 SC_ADD_OBJECT,
	// R+H 밖으로 멀어진 드랍은 SC_REMOVE_OBJECT. 플레이어 위치가 바뀌는 곳(PlayerMove·PlayerRevive)과 입장 때 부른다.
	void UpdateDropView(const shared_ptr<Player>& player);

	// ---- 마을 NPC (L4) ----
	// 맵의 Npc 마커 + NpcTable로 NPC를 놓는다(InitRoom). 마커의 종류가 NpcTable에 없으면 그 마커는 건너뛰고 ERROR를 남긴다.
	void SpawnTownNpcs();
	shared_ptr<TownNpc> GetNpc(int npcId) const;
	// 플레이어의 NPC 시야(Player::npcView)를 지금 위치 기준으로 맞춘다(드랍과 같은 반경: 들어옴 <= R, 나감 > R+H).
	// NPC는 움직이지 않고 수가 적어 색인 없이 거리로 본다. **UpdateDropView를 부르는 곳에서 함께 부른다.**
	void UpdateNpcView(const shared_ptr<Player>& player);
	// 상점 요청 공통 검증. 통과하면 SUCCESS와 함께 outNpc를 채운다. needItemOp = false(품목 요청)면 진행 중인 작업(BUSY)은 보지 않는다.
	Protocol::ItemResult CheckShopRequest(const shared_ptr<Player>& player, int npcId, bool needItemOp, shared_ptr<TownNpc>& outNpc);
	int NextDropId();

	atomic<int> _playerCount = 0;
	int _capacity = 100;  // 정원(WorldTable.RoomCapacity). InitRoom에서 정하고 그 뒤로 바뀌지 않는다(Room이 목록에 등록되기 전)
	int _tickCount = 0;  // Update 횟수. 1초(10틱)마다 시야 인원 지표·시야 감사를 하는 데 쓴다

	// [Debug] 부하·검증 시험용
	bool PickDummyZonePosition(Protocol::PositionInfo& out);   // DummySpawnZone 안의 내비메시 위치
	void AuditViews();                                         // ViewAudit: 시야 목록을 실제 거리와 비교

	unordered_map<int, shared_ptr<GameObject>> _objects;
	unordered_map<int, shared_ptr<Player>> _players;
	unordered_map<int, shared_ptr<Monster>> _monsters;
	// 바닥 드랍. _objects에는 넣지 않는다(DropItem.h). id = DROP_ID_BEGIN부터(플레이어 = 세션 번호, 몬스터 = 100001~ 과 겹치지 않음)
	unordered_map<int, shared_ptr<DropItem>> _drops;
	// 마을 NPC(L4). _objects에는 넣지 않는다(TownNpc.h). id = NPC_ID_BEGIN부터
	unordered_map<int, shared_ptr<TownNpc>> _npcs;
	// 입장한 플레이어에게 보내는 포털 목록(SC_PORTAL_LIST, L5). InitRoom에서 한 번 만들고 바꾸지 않는다. 쓸 포털이 없으면 nullptr
	shared_ptr<SendBuffer> _portalListBuffer;
	deque<int> _dropOrder;   // 만든 순서(상한을 넘으면 앞에서부터 지운다). 이미 사라진 id는 앞에서 걷어 낸다
	int _nextDropId = 0;

	// 진행 중인 1:1 거래(A4.1). 키 = 거래 id(방 안에서만 유일)
	unordered_map<int, TradeSession> _trades;
	int _nextTradeId = 0;

	GameMap				_gameMap;

	shared_ptr<JobQueue> _jobQueue;

	shared_ptr<Timer>		_timer;
};

class RoomManager
{
public:
	RoomManager(HANDLE iocpHandle) : _iocpHandle(iocpHandle) {}
public:
	shared_ptr<Room> CreateRoom();
	void Remove(int roomId);
	void Remove(shared_ptr<Room> room);

	void EnterPlayer(shared_ptr<Player> player);

	int IdGenerator();

private:
	// 정원이 남은 기존 방에 입장 예약을 시도한다.
	bool TryEnterExisting(shared_ptr<Player> player);

public:

	RWLock				_lock;
	mutex				_createMutex; // 방 생성(맵 로딩 포함)은 한 번에 하나만, _lock 밖에서 수행한다.

	unordered_map<int, shared_ptr<Room>> _rooms;

	HANDLE _iocpHandle;
};

