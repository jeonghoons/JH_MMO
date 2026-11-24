#define _WINSOCK_DEPRECATED_NO_WARNINGS
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0A00   // WSAPoll 상수(POLLRDNORM 등) 사용
#include <WinSock2.h>
#include <WS2tcpip.h>
#include <windows.h>
#include <tlhelp32.h>
#include <psapi.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <limits>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <unordered_set>

#include "BotEngine.h"
#include "BotNav.h"
#include "BotData.h"
#include "../Server/Protocol/Protocol.pb.h"
#include <nlohmann/json.hpp>

#pragma comment(lib, "ws2_32.lib")
#ifdef _DEBUG
#pragma comment(lib, "libprotobufd.lib")
#else
#pragma comment(lib, "libprotobuf.lib")
#endif

using namespace std;
using Clock = chrono::steady_clock;
using BotEngine::NowUs;

BotConfig GConfig;
BotCounters GStat;
vector<pair<float, float>> GMonsterSpawns;
vector<pair<float, float>> GDungeonSpawns;
vector<TownZone> GTownZones;

static atomic<bool> GRunning{ true };
static vector<BotProfile> GBotProfiles;   // 봇 번호 → 프로필(Start에서 --mix 비율로 정함)
// 경매 NPC 위치(맵 로직 JSON의 Npc 마커 중 NpcTable.Auction=1) — Auction 봇이 걸어가는 곳. 못 찾으면 has = false
static bool GAuctionNpcKnown = false;
static float GAuctionNpcX = 0.f, GAuctionNpcY = 0.f;
// 거래 봇의 오브젝트 id(로그인 때 넣음). 거래 봇이 시야 안 플레이어 중 상대를 고르는 데 쓴다(워커 스레드 사이 공유)
static mutex GTradeIdsLock;
static unordered_set<int> GTradeObjectIds;

static const char* const kProfileNames[static_cast<int>(BotProfile::Count)] = { "idle", "move", "turn", "chat", "hunt", "loot", "trade", "auction" };
const char* BotEngine::ProfileName(BotProfile p)
{
	const int i = static_cast<int>(p);
	return (i >= 0 && i < static_cast<int>(BotProfile::Count)) ? kProfileNames[i] : "?";
}
static atomic<bool> GShuttingDown{ false };
static atomic<int> GAllowed{ 0 };   // 램프업으로 지금까지 접속이 허용된 봇 수
static long long GStartUs = 0;
static vector<thread> GWorkers;

long long BotEngine::NowUs()
{
	return chrono::duration_cast<chrono::microseconds>(Clock::now().time_since_epoch()).count();
}

// 스레드별 RTT 샘플 버퍼. 워커가 push, 리포터가 swap으로 가져간다.
struct SampleBuffer
{
	mutex m;
	vector<uint32_t> pingUs;
	vector<uint32_t> loginUs;
	vector<uint32_t> enterUs;   // 첫 연결 시작 → 내 SC_ADD_OBJECT(방 입장 끝), 재시도 포함(A5.4b)
};
static vector<unique_ptr<SampleBuffer>> GSamples;

// 스레드별 시각화 스냅숏. 워커가 주기적으로 덮어쓰고, 화면이 GetSnapshot으로 읽는다.
struct SnapshotBuffer
{
	mutex m;
	vector<BotView> bots;
	vector<MonsterView> monsters;
};
static vector<unique_ptr<SnapshotBuffer>> GSnapshots;

uint32_t BotEngine::Percentile(const vector<uint32_t>& sorted, double p)
{
	if (sorted.empty()) return 0;
	size_t idx = static_cast<size_t>(p * (sorted.size() - 1) + 0.5);
	return sorted[min(idx, sorted.size() - 1)];
}

// ---------------------------------------------------------------- 봇
struct Bot
{
	int index = 0;
	SOCKET sock = INVALID_SOCKET;
	BotState state = BotState::Idle;
	long long connectStartUs = 0;
	long long firstConnectUs = 0;   // 처음 연결을 시작한 시각(재시도해도 유지) — 접속 시간 표본(A5.4b)
	long long loginSentUs = 0;
	long long nextMoveUs = 0;
	long long nextPingUs = 0;
	long long nextEquipUs = 0;
	BotProfile profile = BotProfile::Move;   // 이 봇의 행동(A5.2, D-298)
	// 인벤토리(A3.3): item_db_id -> 행. SC_INVENTORY·SC_INVENTORY_UPDATE로 채운다(A5.2부터 수량·칸·골드도)
	struct Item { int itemId = 0; int equip = 0; long long count = 0; int slot = -1; };
	unordered_map<long long, Item> items;
	long long gold = 0;
	bool inventoryKnown = false;
	int maxSlots = 30;         // SC_INVENTORY.max_slots(서버 EconomyTable.InventorySlots)
	int gearGiveIndex = 0;        // --give-gear: 다음에 요청할 지급 목록 위치
	long long nextGiveUs = 0;
	long long nextSlotMoveUs = 0;
	long long nextShopUs = 0;
	int shopNpcId = 0;            // 시야 안의 상점 NPC(OBJECT_TYPE_NPC) id. 없으면 0 (--shop-interval)
	long long nextPortalUs = 0;
	vector<array<float, 3>> portals;   // SC_PORTAL_LIST: (x, y, id) (--portal-interval)
	bool shopBuyNext = true;      // 다음 상점 요청이 구매인지(구매·판매를 번갈아 보낸다)
	bool aimBowGiveSent = false;  // --aim: 활이 없어 지급을 요청했는지
	int objectId = -1;
	float x = 0, y = 0, z = 0;
	vector<uint8_t> rbuf;   // 수신 누적 버퍼
	size_t rpos = 0;
	vector<uint8_t> pending; // 아직 못 보낸 송신 바이트
	unordered_map<int, pair<float, float>> monsters; // 시야 안의 몬스터 id -> (x, y)
	unordered_set<int> deadMonsters;   // SC_DEAD를 받았고 아직 제거되지 않은 몬스터(사냥 봇은 죽은 몬스터를 노리지 않는다, A5.2)
	long long nextAttackUs = 0;
	int castCount = 0;
	// --aim (MC4.2d-1): 슬롯 1을 쓰기 전에 조준을 켜고 잠시 느리게 움직인 뒤 쏜다
	bool aiming = false;
	bool aimBowSent = false;    // 시작할 때 활(6102)을 한 번 장착했는지
	uint32_t aimSeq = 0;
	uint32_t attackSeq = 0;   // CS_ATTACK.seq (CP1)
	int moveFailures = 0;   // 서버가 내 이동을 거부(보정)한 횟수. hunt 봇이 막혔는지 판단한다
	int spawnTarget = -1;   // hunt 봇이 찾아가는 몬스터 스폰(GMonsterSpawns 인덱스). 처음엔 가장 가까운 곳
	int blockedCount = 0;   // 지금 목표로 가다가 막힌 횟수. 많이 막히면 다른 스폰으로 바꾼다
	long long loginDueUs = 0;   // 가입 후 로그인을 보낼 시각(archer)
	bool spawnKnown = false;    // 입장 후 내 SC_ADD_OBJECT로 실제 시작 위치를 받았는지(로그인 응답의 위치는 0)
	float moveSpeed = 0.f;      // 서버 스탯 move_speed. 서버가 이 속도로 이동을 검증한다
	long long lastMoveSentUs = 0;
	uint32_t correctionSeq = 0; // 서버가 보낸 마지막 보정 순번. CS_MOVE에 실어 보낸다
	long long windowBytes = 0;  // 이번 리포트 구간에 받은 바이트(봇 1명당 최대 수신량 지표)
	// 무작위 이동: 목표 지점을 정해 몇 초씩 그쪽으로 걷는다(매번 방향을 바꾸면 클라이언트에서 제자리 미끄러짐으로 보인다)
	bool hasGoal = false;
	float goalX = 0, goalY = 0;
	float heading = 0.f;        // turn 프로필: 현재 진행 방향(라디안)
	long long nextTurnUs = 0;
	long long goalUntilUs = 0;
	int windowEpoch = -1;
	// 사망·부활(A2.4): 내 SC_DEAD를 받으면 행동을 멈추고, 서버가 알려 준 대기 시간이 지나면 CS_REVIVE를 보낸다
	bool dead = false;
	long long reviveAtUs = 0;
	// Room 이동(DG3b): SC_ROOM_CHANGE로 아는 Room 종류. roomKind 0 = 아직 모름(로그인 직후). inField는 사냥터 Room일 때 true
	int roomKind = 0;
	bool inField = false;
	// 던전(DG3c, --dungeon): 지금 있는 레벨 이름(로그인 응답 → SC_ROOM_CHANGE), 다음 입구 요청 시각, 출구 요청 시각(0 = 안 나감)
	string levelName;
	long long nextDungeonEnterUs = 0;
	long long dungeonExitAtUs = 0;
	long long nextHuntPortalUs = 0;   // 사냥 봇이 마을 포털 요청을 다시 보낼 시각(응답이 없으면 1초 뒤)
	// 파티(DG2, --party-size): 마지막 SC_PARTY_STATE의 파티장·구성원(오브젝트 id). 파티가 없으면 partyLeaderId 0
	int partyLeaderId = 0;
	unordered_set<int> partyMembers;
	long long nextPartyInviteUs = 0;
	long long nextPartyLeaveUs = 0;
	int partyInviteCursor = 0;        // 묶음 안에서 다음에 초대해 볼 자리
	// 길찾기(A5.1, BotNav): 지금 따라가는 길의 꺾이는 점들과 그 길을 찾은 목표. 보정·Room 이동·부활·순간이동이면 버린다
	vector<array<float, 3>> path;
	size_t pathIdx = 0;
	// --town-wander: 처음 배치된 곳(0 = 아직 모름, 1 = 마을, 2 = 사냥터). 마을 출신만 마을을 걷고, 사냥터 출신은 마을에서 부활해도 포털로 돌아간다
	int home = 0;
	bool pathValid = false;
	float pathGoalX = 0, pathGoalY = 0;

	// ---- A5.2
	// 로그인 재시도(D-300): 실패·응답 없음·연결 실패면 retryAtUs에 다시 접속한다(0 = 예약 없음)
	int retryCount = 0;
	long long retryAtUs = 0;
	bool loginFailed = false;      // SC_LOGIN 실패를 받았다 — 다음 루프에서 닫고 재시도를 예약한다(수신 처리 중에 소켓을 닫지 않으려고)
	// 공격·이동 잠금(D-299): 내 SC_ATTACK(받아들여진 시전)의 스킬로 서버와 같은 시간을 계산한다
	float attackSpeed = 1.f;
	long long moveLockUntilUs = 0;   // 이 시각까지 이동을 보내지 않는다(공격 요청 직후 임시 잠금 → 응답으로 확정)
	bool lockProvisional = false;    // 아직 응답 전의 임시 잠금
	long long slotReadyUs[3] = { 0, 0, 0 };   // 슬롯별 쿨타임이 끝나는 시각
	// 잠수(Idle): 이 시각까지만 걷는다(짧은 산책), 다음 산책 시각
	long long idleWalkUntilUs = 0, nextIdleWalkUs = 0;
	long long nextChatUs = 0;
	// 줍기(Loot): 시야 안 드랍(id → x, y, 종류), 주울 수 없다고 판정된 드랍, 응답을 기다리는 아이템 요청
	unordered_map<int, array<float, 3>> drops;
	unordered_set<int> skipDrops;
	// 드랍을 주울 수 있게 되는 시각: 내가 잡은 몬스터 자리에 막 생긴 드랍 = 바로(내 소유), 그 밖 = 소유 시간(DropOwnerSec)이 지난 뒤.
	// 실제 클라이언트처럼 남의 드랍을 소유 시간 안에 줍지 않는다(NOT_OWNER 거부를 만들지 않게, A5.2)
	unordered_map<int, long long> dropFreeAtUs;
	int lastTargetId = 0;                      // 마지막으로 공격한 몬스터
	struct Kill { float x = 0, y = 0; long long us = 0; };
	vector<Kill> recentKills;                  // 내가 노리던 몬스터가 죽은 자리(최근 4개)
	int pickupTarget = 0;
	bool itemOpPending = false;
	int itemOpKind = 0;             // 응답을 기다리는 아이템 요청: 1 = 줍기, 2 = 버리기
	long long nextPickupUs = 0;     // 다음 줍기 요청 시각(서버 ItemMinIntervalMs 100보다 넉넉히)
	long long itemOpSentUs = 0, nextDropItemUs = 0;
	// 거래(Trade): 시야 안 플레이어(id → x, y), 진행 중인 거래 상태
	unordered_map<int, pair<float, float>> players;
	bool tradeActive = false;
	int tradeTargetId = 0;          // 다가가는 중인 상대(요청 전)
	long long tradeStartUs = 0, nextTradeUs = 0;
	bool tradeOfferSent = false, tradeLockSent = false, tradeConfirmSent = false;
	int tradeRetries = 0;
	// 경매(Auction): 경매 NPC 오브젝트 id·위치, 응답을 기다리는 요청, 최근 검색 결과(남의 등록)·내 등록·수령함
	int auctionNpcId = 0;
	float auctionNpcX = 0.f, auctionNpcY = 0.f;
	bool auctionPending = false;
	int auctionPendingKind = -1;
	long long auctionSentUs = 0, nextAuctionUs = 0, nextAuctionSearchUs = 0;
	struct Listing { long long id = 0; long long count = 0; long long unitPrice = 0; };
	vector<Listing> otherListings;
	vector<long long> myListings, mails;
	bool myKnown = false;
};

// 봇 번호 → 오브젝트 id(로그인 응답으로 받은 값, 0 = 아직 모름). 파티 초대 대상을 찾는 데 쓴다(워커 스레드 사이 공유).
static unique_ptr<atomic<int>[]> GBotObjectIds;

static void SendPacket(Bot& b, uint16_t id, const google::protobuf::Message& msg);

static bool IsHunter(const Bot& b) { return b.profile == BotProfile::Hunt || b.profile == BotProfile::Loot; }
static bool IsTownProfile(const Bot& b) { return b.profile == BotProfile::Trade || b.profile == BotProfile::Auction; }   // 마을에서 하는 일(거래·경매)
static bool MovesFreely(const Bot& b) { return b.profile != BotProfile::Idle && b.profile != BotProfile::Auction; }

// 서버의 속도 검증(MovementTable 허용치)에 걸리지 않도록, 마지막 이동 이후 경과 시간만큼만 움직인다(최대 1초 분량, 여유 10%).
static float MaxStep(const Bot& b, long long now)
{
	// 이동 잠금(A5.2) 동안 서버는 이동 예산을 쌓지 않으므로 잠금이 끝난 시각부터 잰다
	const long long since = max(b.lastMoveSentUs, b.moveLockUntilUs);
	const double elapsedSec = max(0.0, min(1.0, (now - since) / 1e6));
	// 조준 중에는 서버가 AimMoveSpeedMul(활 0.4)을 곱하므로 그만큼만 움직인다
	const double aimMul = b.aiming ? 0.4 : 1.0;
	return static_cast<float>(b.moveSpeed * 0.9 * aimMul * elapsedSec);
}

static void SendMove(Bot& b, long long now, float nx, float ny, float yaw, Protocol::MoveState state)
{
	// 공격 중 이동 잠금(A5.2, D-299): 실제 클라이언트처럼 휘두르는 동안은 움직이지 않는다(서버가 "acting"으로 거부·보정하는 이동을 만들지 않게)
	if (now < b.moveLockUntilUs && !GConfig.chaos)
	{
		GStat.moveHeldByLock.fetch_add(1);
		return;
	}
	Protocol::CS_MOVE_PACKET mv;
	Protocol::PositionInfo* p = mv.mutable_pos_info();
	// 높이는 새 위치의 바닥 기준(A5.1): 실제 클라이언트처럼 딛고 선 높이를 보낸다. 서버는 받은 z를 저장하고 다음 이동의 시작점을 그 위아래 3m에서 찾으므로,
	// 처음 z를 계속 보내면 언덕·내리막에서 이동이 모두 거부된다. 메시가 없거나 바닥을 못 찾으면 지금 z
	// z는 실제 클라이언트처럼 캐릭터 중심 높이(바닥 + 캡슐 반높이 96cm — BP_Player CapsuleHalfHeight, 서버 Room.cpp DUMMY_STAND_UP_CM과 같은 값).
	// 바닥 그대로 보내면 다른 클라이언트 화면에서 땅에 묻힌다
	constexpr float kStandUpCm = 96.f;
	float nz = b.z, ground = 0.f;
	if (BotNav::SnapZ(b.levelName, nx, ny, b.z, ground)) nz = ground + kStandUpCm;
	p->set_x(nx); p->set_y(ny); p->set_z(nz); p->set_yaw(yaw);
	p->set_state(state);
	// 실제 클라이언트처럼 속도를 싣는다(원격 클라이언트의 외삽·걷기 애니메이션이 이 값을 쓴다, MC4.1b). 이동 스탯 이하로 자른다.
	const double elapsedSec = (now - b.lastMoveSentUs) / 1e6;
	if (state == Protocol::MOVE_STATE_RUN && elapsedSec > 0.0 && b.lastMoveSentUs > 0)
	{
		float vx = static_cast<float>((nx - b.x) / elapsedSec), vy = static_cast<float>((ny - b.y) / elapsedSec);
		const float sp = sqrtf(vx * vx + vy * vy);
		if (sp > b.moveSpeed && sp > 0.f) { vx *= b.moveSpeed / sp; vy *= b.moveSpeed / sp; }
		p->set_v_x(vx); p->set_v_y(vy);
	}
	mv.set_force(false);
	mv.set_correction_seq(b.correctionSeq);

	// 오류 테스트: 일부 이동을 잘못된 값으로 바꾼다. 서버는 거부하고 보정(SC_MOVE)을 보내야 한다. 내 위치는 바꾸지 않는다.
	if (GConfig.chaos && rand() % 100 < 10)
	{
		switch (rand() % 4)
		{
		case 0: p->set_x(b.x + 5000.f); break;                                       // 순간이동
		case 1: p->set_x(std::numeric_limits<float>::quiet_NaN()); break;             // NaN
		case 2: p->set_y(b.y + 3.f * b.moveSpeed); break;                             // 3배 속도
		default: mv.set_correction_seq(b.correctionSeq + 7); break;                   // 맞지 않는 보정 순번(무시돼야 함)
		}
		SendPacket(b, Protocol::CS_MOVE, mv);
		b.lastMoveSentUs = now;
		return;
	}

	SendPacket(b, Protocol::CS_MOVE, mv);
	b.lastMoveSentUs = now;
	b.x = nx; b.y = ny; b.z = nz;   // 서버가 거부하면 보정 패킷(SC_MOVE_OBJECT)으로 되돌려진다.
}

// (tx, ty)를 향한 한 걸음(최대 stride). 내비메시가 있으면 걷는 영역 위의 길(꺾이는 점)을 따라가고, 없으면 직선(A5.1).
// 목표가 2m 넘게 바뀌었거나 길을 다 걸었으면 길을 다시 찾는다. 목표에 닿을 수 없으면 가장 가까이 갈 수 있는 곳까지 간다.
static void StepToward(Bot& b, float tx, float ty, float stride, float& nx, float& ny, float& yaw)
{
	nx = b.x; ny = b.y;
	if (BotNav::Has(b.levelName))
	{
		const float gdx = tx - b.pathGoalX, gdy = ty - b.pathGoalY;
		if (!b.pathValid || b.pathIdx >= b.path.size() || gdx * gdx + gdy * gdy > 200.f * 200.f)
		{
			b.pathValid = BotNav::FindPath(b.levelName, b.x, b.y, b.z, tx, ty, b.path);
			b.pathIdx = 0;
			b.pathGoalX = tx; b.pathGoalY = ty;
			GStat.pathSearch.fetch_add(1);
			if (!b.pathValid) GStat.pathFail.fetch_add(1);
		}
		if (b.pathValid)
		{
			float remain = stride;
			while (remain > 0.f && b.pathIdx < b.path.size())
			{
				const array<float, 3>& wp = b.path[b.pathIdx];
				const float dx = wp[0] - nx, dy = wp[1] - ny;
				const float d = sqrtf(dx * dx + dy * dy);
				if (d > 1.f) yaw = atan2f(dy, dx) * 180.f / 3.14159265f;
				if (d <= remain) { nx = wp[0]; ny = wp[1]; remain -= d; ++b.pathIdx; }
				else { nx += dx / d * remain; ny += dy / d * remain; remain = 0.f; }
			}
			return;
		}
	}
	const float dx = tx - b.x, dy = ty - b.y;
	const float d = sqrtf(dx * dx + dy * dy);
	if (d > 1.f)
	{
		const float s = min(stride, d);
		nx = b.x + dx / d * s; ny = b.y + dy / d * s;
		yaw = atan2f(dy, dx) * 180.f / 3.14159265f;
	}
}

// 무작위 목표를 길이 닿는 곳으로 바꾼다: 길의 끝(닿을 수 있는 가장 가까운 곳)을 목표로 삼아, 막힌 목표 앞에서 제자리걸음하지 않게 한다
// 길을 못 찾으면(목표가 걷는 영역에서 멀리 떨어짐 등) false — 부르는 쪽이 다른 목표를 고른다. 메시가 없으면 늘 true(직선)
static bool ClampGoalToPath(Bot& b)
{
	if (!BotNav::Has(b.levelName)) return true;
	b.pathValid = BotNav::FindPath(b.levelName, b.x, b.y, b.z, b.goalX, b.goalY, b.path);
	b.pathIdx = 0;
	GStat.pathSearch.fetch_add(1);
	if (!b.pathValid) { GStat.pathFail.fetch_add(1); return false; }
	b.goalX = b.path.back()[0]; b.goalY = b.path.back()[1];
	b.pathGoalX = b.goalX; b.pathGoalY = b.goalY;
	return true;
}

// ---------------------------------------------------------------- Room 이동 (DG3b)
// 마을 Room의 문 포털은 101~103이고 사냥터 쪽은 201~203이다. 던전 입구(100)·정거장(110)은 다른 레벨로 가므로 봇은 쓰지 않는다.
static constexpr float kPortalStopCm = 150.f;   // 사냥 봇이 포털 앞에서 멈추는 거리(포털 반경 300cm 안쪽)
static constexpr float kPortalSendCm = 200.f;   // 이 거리 안에 들어오면 CS_PORTAL을 보낸다

// 던전 포털 id(임시 던전 레벨, DG3c): 사냥터의 입구 100, 던전 안의 출구 900
static constexpr int kDungeonEntrancePortal = 100;
static constexpr int kDungeonExitPortal = 900;

static bool HasPortal(const Bot& b, int id)
{
	for (const array<float, 3>& p : b.portals)
		if (static_cast<int>(p[2]) == id) return true;
	return false;
}

// hunt 봇이 찾아갈 몬스터 스폰 목록: 던전 Room 안이면 던전 레벨의 것
static const vector<pair<float, float>>& HuntSpawns(const Bot& b)
{
	return b.roomKind == static_cast<int>(Protocol::ROOM_KIND_DUNGEON) ? GDungeonSpawns : GMonsterSpawns;
}

static bool IsTownDoorPortal(float id) { return id >= 101.f && id <= 103.f; }

// 가장 가까운 마을 쪽 포털. 목록에 없으면 nullptr.
static const array<float, 3>* NearestTownPortal(const Bot& b)
{
	const array<float, 3>* best = nullptr;
	float bestSq = FLT_MAX;
	for (const array<float, 3>& p : b.portals)
	{
		if (!IsTownDoorPortal(p[2])) continue;
		const float dx = p[0] - b.x, dy = p[1] - b.y;
		if (dx * dx + dy * dy < bestSq) { bestSq = dx * dx + dy * dy; best = &p; }
	}
	return best;
}

// 사냥 봇이 지금 마을 포털로 나가야 하면 그 포털을, 아니면 nullptr.
// 사냥터 Room(inField)과 던전은 아니다. 마을 Room(TOWN)이면 마을 쪽 포털이 목록에 있을 때 그렇다.
// 아직 모르면(로그인 직후) 몬스터가 안 보이고 마을 쪽 포털이 목록에 있을 때만 그렇다 — 사냥터의 포털 목록에는 마을 쪽 포털이 없다.
static bool WandersTown(const Bot& b);
static const array<float, 3>* HuntTownPortal(const Bot& b)
{
	if (!IsHunter(b) || b.inField || WandersTown(b)) return nullptr;
	if (b.roomKind == static_cast<int>(Protocol::ROOM_KIND_FIELD) || b.roomKind == static_cast<int>(Protocol::ROOM_KIND_DUNGEON)) return nullptr;
	if (b.roomKind == static_cast<int>(Protocol::ROOM_KIND_NONE) && !b.monsters.empty()) return nullptr;
	return NearestTownPortal(b);
}

// 지금 마을 Room에 있는가. 로그인 직후(Room 종류를 아직 모름)에는 포털 목록에 마을 쪽 문 포털(101~103)이 있으면 마을이다
static bool InTownRoom(const Bot& b)
{
	if (b.roomKind == static_cast<int>(Protocol::ROOM_KIND_TOWN)) return true;
	if (b.roomKind != static_cast<int>(Protocol::ROOM_KIND_NONE)) return false;
	for (const array<float, 3>& p : b.portals)
		if (IsTownDoorPortal(p[2])) return true;
	return false;
}

// --town-wander이고 마을 출신이며 지금 마을에 있으면 마을 안을 걷는다(포털로 나가지 않음). 거래 봇은 마을에 있으면 늘 마을 안을 걷는다(A5.2)
static bool WandersTown(const Bot& b)
{
	return ((GConfig.townWander && b.home == 1) || b.profile == BotProfile::Trade) && InTownRoom(b);
}

// 사냥터 쪽 문 포털(201~203 — 마을로 간다)
static bool IsFieldDoorPortal(float id) { return id >= 201.f && id <= 203.f; }

// 거래·경매 봇(A5.2)이 마을로 가야 하면 가장 가까운 사냥터 쪽 문 포털, 아니면 nullptr(마을에 있음·던전·포털 목록을 아직 모름)
static const array<float, 3>* TownBoundPortal(const Bot& b)
{
	if (!IsTownProfile(b) || InTownRoom(b) || b.roomKind == static_cast<int>(Protocol::ROOM_KIND_DUNGEON)) return nullptr;
	const array<float, 3>* best = nullptr;
	float bestSq = FLT_MAX;
	for (const array<float, 3>& p : b.portals)
	{
		if (!IsFieldDoorPortal(p[2])) continue;
		const float dx = p[0] - b.x, dy = p[1] - b.y;
		if (dx * dx + dy * dy < bestSq) { bestSq = dx * dx + dy * dy; best = &p; }
	}
	return best;
}

// 지금 걸어가 요청할 문 포털(사냥 봇 = 마을에서 사냥터로, 거래·경매 봇 = 사냥터에서 마을로)
static const array<float, 3>* DoorPortalGoal(const Bot& b)
{
	if (const array<float, 3>* p = HuntTownPortal(b)) return p;
	return TownBoundPortal(b);
}

#pragma pack(push, 1)
struct Header { uint16_t size; uint16_t id; };
#pragma pack(pop)

static void CloseBot(Bot& b, bool unexpected)
{
	if (b.sock != INVALID_SOCKET)
	{
		closesocket(b.sock);
		b.sock = INVALID_SOCKET;
	}
	if (b.state == BotState::LoggingIn || b.state == BotState::InGame)
	{
		GStat.connected.fetch_sub(1);
		if (b.state == BotState::InGame) GStat.inGame.fetch_sub(1);
	}
	if (unexpected && !GShuttingDown.load()) GStat.unexpectedClose.fetch_add(1);
	b.state = BotState::Closed;
	b.rbuf.clear();
	b.rpos = 0;
	b.pending.clear();
	b.monsters.clear();
	b.deadMonsters.clear();
}

// 로그인 재시도 예약(A5.2, D-300): 1·2·4·…·30초(±25%) 뒤 다시 접속한다. 횟수를 넘으면 포기(loginGiveUp)
static void ScheduleRetry(Bot& b)
{
	if (GShuttingDown.load()) return;
	if (b.retryCount >= GConfig.loginRetries) { GStat.loginGiveUp.fetch_add(1); b.retryAtUs = 0; return; }
	const long long baseMs = min(30000LL, 1000LL << min(b.retryCount, 5));
	const long long jitter = baseMs / 4;
	b.retryAtUs = NowUs() + (baseMs - jitter + rand() % (2 * jitter + 1)) * 1000LL;
	++b.retryCount;
	GStat.loginRetry.fetch_add(1);
}

static bool FlushPending(Bot& b)
{
	while (!b.pending.empty())
	{
		int n = send(b.sock, reinterpret_cast<const char*>(b.pending.data()), static_cast<int>(b.pending.size()), 0);
		if (n == SOCKET_ERROR)
			return WSAGetLastError() == WSAEWOULDBLOCK;
		b.pending.erase(b.pending.begin(), b.pending.begin() + n);
	}
	return true;
}

static void SendPacket(Bot& b, uint16_t id, const google::protobuf::Message& msg)
{
	const size_t payload = msg.ByteSizeLong();
	vector<uint8_t> out(sizeof(Header) + payload);
	Header h{ static_cast<uint16_t>(out.size()), id };
	memcpy(out.data(), &h, sizeof(h));
	msg.SerializeToArray(out.data() + sizeof(h), static_cast<int>(payload));

	if (b.pending.size() > (1u << 20)) { GStat.sendDropped.fetch_add(1); return; }

	if (b.pending.empty())
	{
		int n = send(b.sock, reinterpret_cast<const char*>(out.data()), static_cast<int>(out.size()), 0);
		if (n == SOCKET_ERROR)
		{
			if (WSAGetLastError() != WSAEWOULDBLOCK) return; // 오류는 poll 쪽에서 닫힘 처리
			n = 0;
		}
		if (static_cast<size_t>(n) < out.size())
			b.pending.insert(b.pending.end(), out.begin() + n, out.end());
	}
	else
	{
		b.pending.insert(b.pending.end(), out.begin(), out.end());
	}
	GStat.pktSent.fetch_add(1);
}

static void StartConnect(Bot& b)
{
	GStat.connectAttempts.fetch_add(1);

	b.sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (b.sock == INVALID_SOCKET) { GStat.connectFail.fetch_add(1); b.state = BotState::Closed; ScheduleRetry(b); return; }

	u_long nonBlocking = 1;
	ioctlsocket(b.sock, FIONBIO, &nonBlocking);
	BOOL noDelay = TRUE;
	setsockopt(b.sock, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&noDelay), sizeof(noDelay));

	sockaddr_in addr{};
	addr.sin_family = AF_INET;
	addr.sin_port = htons(static_cast<u_short>(GConfig.port));
	inet_pton(AF_INET, GConfig.host.c_str(), &addr.sin_addr);

	b.connectStartUs = NowUs();
	if (b.firstConnectUs == 0) b.firstConnectUs = b.connectStartUs;
	b.state = BotState::Connecting;
	if (connect(b.sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR &&
		WSAGetLastError() != WSAEWOULDBLOCK)
	{
		GStat.connectFail.fetch_add(1);
		closesocket(b.sock);
		b.sock = INVALID_SOCKET;
		b.state = BotState::Closed;
		ScheduleRetry(b);
	}
}

static void OnConnected(Bot& b)
{
	GStat.connectOk.fetch_add(1);
	GStat.connected.fetch_add(1);
	b.state = BotState::LoggingIn;

	if (GConfig.accountLogin)
	{
		// SQL로 미리 만든 부하 시험 계정(D-296)으로 일반 로그인한다 — 로그인·아이템·저장이 실제 플레이어와 같은 DB 경로
		Protocol::CS_LOGIN_PACKET login;
		login.set_account_id("bot_" + to_string(b.index + GConfig.accountStart));
		login.set_account_pw("1234");
		b.loginSentUs = NowUs();
		SendPacket(b, Protocol::CS_LOGIN, login);
		return;
	}
	if (GConfig.body == "female")
	{
		// 여성 체형 계정은 먼저 가입(이미 있으면 서버가 무시)하고 잠시 뒤 일반 로그인한다. (dummy 계정은 MALE)
		Protocol::CS_SIGNUP_PACKET signup;
		signup.set_account_id("fem_" + to_string(b.index));
		signup.set_account_pw("1234");
		signup.set_player_type(Protocol::PLAYER_TYPE_FEMALE);
		SendPacket(b, Protocol::CS_SIGNUP, signup);
		b.loginDueUs = NowUs() + 700 * 1000LL;
		return;
	}

	Protocol::CS_LOGIN_PACKET login;
	login.set_account_id("bot_" + to_string(b.index));
	login.set_account_pw("1234");
	login.set_is_dummy(true);   // 서버가 dummy 계정으로 치환한다([Debug] AllowDummyLogin일 때만, D-291)
	b.loginSentUs = NowUs();
	SendPacket(b, Protocol::CS_LOGIN, login);
}

static void HandlePacket(Bot& b, SampleBuffer& samples, uint16_t id, const uint8_t* payload, int len)
{
	switch (id)
	{
	case Protocol::SC_LOGIN:
	{
		Protocol::SC_LOGIN_INFO_PACKET pkt;
		if (!pkt.ParseFromArray(payload, len)) { GStat.protocolError.fetch_add(1); return; }
		// 로그인 실패도 SC_LOGIN으로 온다. 실패면 object_info가 비어 있으므로 성공으로 세지 않는다.
		if (pkt.result() != Protocol::LOGIN_RESULT_SUCCESS)
		{
			GStat.loginFail.fetch_add(1);
			const int code = static_cast<int>(pkt.result());
			if (code >= 0 && code < 8) GStat.loginFailBy[code].fetch_add(1);
			if (b.index == 0) printf("[bot] bot0 login failed: %s\n", Protocol::LoginResult_Name(pkt.result()).c_str());
			b.loginFailed = true;   // 다음 루프에서 닫고 다시 시도한다(A5.2)
			return;
		}

		b.objectId = pkt.object_info().id();
		if (GBotObjectIds) GBotObjectIds[b.index].store(b.objectId);
		b.x = pkt.object_info().position().x();
		b.y = pkt.object_info().position().y();
		b.z = pkt.object_info().position().z();
		b.moveSpeed = pkt.object_info().stat().move_speed();
		b.attackSpeed = pkt.object_info().stat().attack_speed() > 0.f ? pkt.object_info().stat().attack_speed() : 1.f;
		b.retryCount = 0;
		GStat.loginOk.fetch_add(1);
		if (b.profile == BotProfile::Trade)
		{
			lock_guard<mutex> lock(GTradeIdsLock);
			GTradeObjectIds.insert(b.objectId);
		}
		if (b.index == 0)
		{
			const auto& s = pkt.object_info().stat();
			printf("[bot] bot0 login: id=%d playerType=%d maxHp=%d attack=%d attackSpeed=%.2f moveSpeed=%.1f pos=(%.0f, %.0f, %.0f)\n",
				b.objectId, static_cast<int>(pkt.object_info().player_type()), s.max_hp(), s.attack_damage(), s.attack_speed(), s.move_speed(),
				b.x, b.y, b.z);
		}
		{
			lock_guard<mutex> lock(samples.m);
			samples.loginUs.push_back(static_cast<uint32_t>(NowUs() - b.loginSentUs));
		}

		b.levelName = pkt.map_name();

		Protocol::CS_ENTER_ROOM_PACKET enter;
		enter.set_result(true);
		SendPacket(b, Protocol::CS_ENTER_ROOM, enter);

		b.state = BotState::InGame;
		GStat.inGame.fetch_add(1);
		const long long now = NowUs();
		// 봇들이 같은 순간에 몰리지 않도록 첫 전송 시각을 분산한다.
		b.nextMoveUs = now + (rand() % max(1, GConfig.moveIntervalMs)) * 1000LL;
		b.nextPingUs = now + (rand() % max(1, GConfig.pingIntervalMs)) * 1000LL;
		break;
	}
	case Protocol::SC_PONG:
	{
		Protocol::SC_PONG_PACKET pkt;
		if (!pkt.ParseFromArray(payload, len)) { GStat.protocolError.fetch_add(1); return; }
		GStat.pongRecv.fetch_add(1);
		lock_guard<mutex> lock(samples.m);
		samples.pingUs.push_back(static_cast<uint32_t>(NowUs() - pkt.client_time()));
		break;
	}
	case Protocol::SC_MOVE_BATCH:
	{
		// 다른 대상의 이동 묶음(P2, D-307). 위치를 따라가야 하는 봇(거래 상대·사냥 대상·시각화)만 파싱한다.
		GStat.moveBatchRecv.fetch_add(1);
		const bool trackPlayers = (b.profile == BotProfile::Trade);
		const bool trackMonsters = GConfig.trackWorld || IsHunter(b);
		if (!trackPlayers && !trackMonsters) return;
		Protocol::SC_MOVE_BATCH_PACKET pkt;
		if (!pkt.ParseFromArray(payload, len)) { GStat.protocolError.fetch_add(1); return; }
		for (const Protocol::MoveEntry& e : pkt.moves())
		{
			if (trackPlayers)
			{
				auto pit = b.players.find(e.id());
				if (pit != b.players.end()) { pit->second = { e.position().x(), e.position().y() }; continue; }
			}
			if (trackMonsters)
			{
				auto it = b.monsters.find(e.id());
				if (it != b.monsters.end()) it->second = { e.position().x(), e.position().y() };
			}
		}
		break;
	}
	case Protocol::SC_MOVE_OBJECT:
	{
		// 모든 이동 브로드캐스트를 파싱하면 비용이 크므로, object_info.id만 먼저 훑어서 내 것일 때만 파싱한다.
		// SC_MOVE_PACKET = { 1: object_info(LEN) { 1: id(varint) ... } }
		if (len < 4 || payload[0] != 0x0A || payload[2] != 0x08) return;
		uint32_t id = 0; int shift = 0; int i = 3;
		while (i < len && shift < 32) { id |= (payload[i] & 0x7F) << shift; if (!(payload[i++] & 0x80)) break; shift += 7; }
		if (static_cast<int>(id) != b.objectId)
		{
			// 거래 봇(A5.2)은 다가갈 상대의 위치를 알아야 한다
			if (b.profile == BotProfile::Trade)
			{
				auto pit = b.players.find(static_cast<int>(id));
				if (pit != b.players.end())
				{
					Protocol::SC_MOVE_PACKET pkt;
					if (pkt.ParseFromArray(payload, len))
						pit->second = { pkt.object_info().position().x(), pkt.object_info().position().y() };
				}
				return;
			}
			// 시각화 중이거나 사냥 봇(A5.2 — 움직이는 몬스터를 옛 위치로 노리면 TOO_FAR)이면 시야 안 몬스터의 이동도 따라간다.
			if (!GConfig.trackWorld && !IsHunter(b)) return;
			auto it = b.monsters.find(static_cast<int>(id));
			if (it == b.monsters.end()) return;
			Protocol::SC_MOVE_PACKET pkt;
			if (pkt.ParseFromArray(payload, len))
				it->second = { pkt.object_info().position().x(), pkt.object_info().position().y() };
			return;
		}

		Protocol::SC_MOVE_PACKET pkt;
		if (pkt.ParseFromArray(payload, len))
		{
			b.x = pkt.object_info().position().x();
			b.y = pkt.object_info().position().y();
			b.z = pkt.object_info().position().z();   // 서버가 아는 높이로 되돌린다(A5.1 — 전에는 버려서 틀린 z가 남았다)
			b.correctionSeq = pkt.correction_seq();
			b.pathValid = false;
			GStat.moveCorrected.fetch_add(1);
			b.moveFailures = min(b.moveFailures + 3, 8);
			b.hasGoal = false;   // 막혔으면 무작위 이동 목표를 새로 고른다
			// 서버가 벽 통과를 막으므로 직선으로 못 가는 스폰이 있다. 같은 목표로 여러 번 막히면 다른 스폰을 고른다.
			if (++b.blockedCount >= 6 && !HuntSpawns(b).empty())
			{
				b.spawnTarget = rand() % static_cast<int>(HuntSpawns(b).size());
				b.blockedCount = 0;
			}
		}
		break;
	}
	case Protocol::SC_ADD_OBJECT:
	{
		// 몬스터 목록은 hunt 프로필과 시각화에서만 추적한다. 그 외에는 내 시작 위치를 받을 때까지만 본다.
		// --shop-interval이면 상점 NPC id를 알아야 하므로 계속 읽는다(L4).
		// A5.2: 사냥·줍기 봇은 몬스터·드랍, 거래 봇은 플레이어, 경매 봇은 경매 NPC를 기억한다
		if (!IsHunter(b) && !IsTownProfile(b) && !GConfig.trackWorld && GConfig.shopIntervalMs <= 0 && b.spawnKnown) return;
		Protocol::SC_ADD_OBJECT_PACKET pkt;
		if (!pkt.ParseFromArray(payload, len)) { GStat.protocolError.fetch_add(1); return; }
		GStat.objectAdded.fetch_add(1);
		if (b.index == 0)
			printf("[bot] bot0 sees object id=%d type=%d playerType=%d template=%d pos=(%.0f,%.0f)\n", pkt.object_info().id(),
				static_cast<int>(pkt.object_info().object_type()), static_cast<int>(pkt.object_info().player_type()), pkt.object_info().template_id(),
				pkt.object_info().position().x(), pkt.object_info().position().y());
		// 로그인 응답의 위치는 0이고 실제 시작 위치는 입장 후 내 SC_ADD_OBJECT로 온다.
		if (pkt.object_info().id() == b.objectId && !b.spawnKnown)
		{
			b.spawnKnown = true;
			b.lastMoveSentUs = NowUs();
			if (b.firstConnectUs != 0)
			{
				lock_guard<mutex> lock(samples.m);
				samples.enterUs.push_back(static_cast<uint32_t>(min<long long>(UINT32_MAX, NowUs() - b.firstConnectUs)));
			}
			b.x = pkt.object_info().position().x();
			b.y = pkt.object_info().position().y();
			b.z = pkt.object_info().position().z();
		}
		if (pkt.object_info().object_type() == Protocol::OBJECT_TYPE_NPC)
		{
			const int tmpl = pkt.object_info().template_id();
			if (BotData::AuctionNpcTemplate() != 0 && tmpl == BotData::AuctionNpcTemplate())
			{
				b.auctionNpcId = pkt.object_info().id();
				b.auctionNpcX = pkt.object_info().position().x();
				b.auctionNpcY = pkt.object_info().position().y();
			}
			else if (BotData::ShopNpcTemplate() == 0 || tmpl == BotData::ShopNpcTemplate())
				b.shopNpcId = pkt.object_info().id();
		}
		if (pkt.object_info().object_type() == Protocol::OBJECT_TYPE_ITEM && b.profile == BotProfile::Loot)
		{
			GStat.dropSeen.fetch_add(1);
			const float dxp = pkt.object_info().position().x(), dyp = pkt.object_info().position().y();
			b.drops[pkt.object_info().id()] = { dxp, dyp, static_cast<float>(pkt.object_info().template_id()) };
			const long long now = NowUs();
			bool mine = false;
			for (const Bot::Kill& k : b.recentKills)
				if (now - k.us < 3000000LL && (k.x - dxp) * (k.x - dxp) + (k.y - dyp) * (k.y - dyp) <= 400.f * 400.f) { mine = true; break; }
			b.dropFreeAtUs[pkt.object_info().id()] = mine ? now : now + (BotData::DropOwnerSec() + 1) * 1000000LL;
		}
		if (pkt.object_info().object_type() == Protocol::OBJECT_TYPE_PLAYER && b.profile == BotProfile::Trade && pkt.object_info().id() != b.objectId)
			b.players[pkt.object_info().id()] = { pkt.object_info().position().x(), pkt.object_info().position().y() };
		if (pkt.object_info().object_type() == Protocol::OBJECT_TYPE_MONSTER)
		{
			GStat.monsterAdded.fetch_add(1);
			b.monsters[pkt.object_info().id()] = { pkt.object_info().position().x(), pkt.object_info().position().y() };
		}
		break;
	}
	case Protocol::SC_REMOVE_OBJECT:
	{
		Protocol::SC_REMOVE_OBJECT_PACKET pkt;
		if (!pkt.ParseFromArray(payload, len)) { GStat.protocolError.fetch_add(1); return; }
		if (b.monsters.erase(pkt.object_id()) > 0) GStat.monsterRemoved.fetch_add(1);
		if (pkt.object_id() == b.shopNpcId) b.shopNpcId = 0;
		if (pkt.object_id() == b.auctionNpcId) b.auctionNpcId = 0;
		b.deadMonsters.erase(pkt.object_id());
		b.drops.erase(pkt.object_id());
		b.skipDrops.erase(pkt.object_id());
		b.dropFreeAtUs.erase(pkt.object_id());
		b.players.erase(pkt.object_id());
		if (pkt.object_id() == b.pickupTarget) b.pickupTarget = 0;
		if (pkt.object_id() == b.tradeTargetId) b.tradeTargetId = 0;
		break;
	}
	case Protocol::SC_DEAD:
	{
		Protocol::SC_DEAD_PACKET pkt;
		if (!pkt.ParseFromArray(payload, len)) { GStat.protocolError.fetch_add(1); return; }
		if (b.monsters.count(pkt.object_id()))
		{
			GStat.monsterDead.fetch_add(1);
			b.deadMonsters.insert(pkt.object_id());
			if (b.profile == BotProfile::Loot && pkt.object_id() == b.lastTargetId)
			{
				const auto& pos = b.monsters[pkt.object_id()];
				if (b.recentKills.size() >= 4) b.recentKills.erase(b.recentKills.begin());
				b.recentKills.push_back({ pos.first, pos.second, NowUs() });
			}
		}
		else if (pkt.object_id() == b.objectId && !b.dead)
		{
			// 내가 죽었다. 대기 시간 + 여유 200ms 뒤에 부활을 요청한다(서버 시계 기준이라 조금 늦게 보낸다).
			b.dead = true;
			b.aiming = false;
			b.reviveAtUs = NowUs() + (pkt.revive_delay_ms() + 200) * 1000LL;
			GStat.selfDead.fetch_add(1);
		}
		break;
	}
	case Protocol::SC_REVIVE:
	{
		// 다른 사람의 부활은 파싱하지 않는다. SC_REVIVE_PACKET = { 1: object_id(varint) ... }
		Protocol::SC_REVIVE_PACKET pkt;
		if (!pkt.ParseFromArray(payload, len)) { GStat.protocolError.fetch_add(1); return; }
		if (pkt.object_id() != b.objectId) break;
		b.dead = false;
		b.x = pkt.pos_info().x();
		b.y = pkt.pos_info().y();
		b.z = pkt.pos_info().z();
		b.correctionSeq = pkt.correction_seq();
		b.pathValid = false;
		b.lastMoveSentUs = NowUs();   // 서버 이동 예산이 0에서 다시 모이므로 여기서부터 걸음을 잰다
		b.hasGoal = false;
		b.spawnTarget = -1;
		b.moveFailures = 0;
		GStat.selfRevived.fetch_add(1);
		if (b.index == 0) printf("[bot] bot0 revived: hp=%d pos=(%.0f, %.0f, %.0f) seq=%u\n", pkt.hp(), b.x, b.y, b.z, pkt.correction_seq());
		break;
	}
	case Protocol::SC_PORTAL_LIST:
	{
		// 포털 목록은 옵션과 무관하게 항상 받는다(사냥 봇의 마을 포털 판단에 쓴다, DG3b).
		Protocol::SC_PORTAL_LIST_PACKET pkt;
		if (!pkt.ParseFromArray(payload, len)) { GStat.protocolError.fetch_add(1); return; }
		b.portals.clear();
		for (const Protocol::PortalInfo& info : pkt.portals())
			b.portals.push_back({ info.x(), info.y(), static_cast<float>(info.portal_id()) });
		break;
	}
	case Protocol::SC_ROOM_CHANGE:
	{
		// 내 Room이 바뀌었다(문 포털, 부활, 시험 키의 사냥터 배치). 이전 Room의 몬스터·포털·상점 NPC는 버리고,
		// 뒤따르는 SC_ADD_OBJECT·SC_PORTAL_LIST로 다시 채운다. 죽은 상태(dead)는 건드리지 않는다(부활은 뒤따르는 SC_REVIVE).
		// 레벨이 바뀌면(던전 입장·퇴장, DG3c) 실제 클라이언트는 그 레벨을 연 뒤 CS_ENTER_ROOM을 보낸다 — 봇은 열 것이 없어 곧바로 보낸다.
		Protocol::SC_ROOM_CHANGE_PACKET pkt;
		if (!pkt.ParseFromArray(payload, len)) { GStat.protocolError.fetch_add(1); return; }
		GStat.roomChange.fetch_add(1);
		if (pkt.map_name() != b.levelName)
		{
			b.levelName = pkt.map_name();
			Protocol::CS_ENTER_ROOM_PACKET loaded;
			loaded.set_result(true);
			SendPacket(b, Protocol::CS_ENTER_ROOM, loaded);
		}
		{
			const bool wasDungeon = b.roomKind == static_cast<int>(Protocol::ROOM_KIND_DUNGEON);
			const bool isDungeon = pkt.kind() == Protocol::ROOM_KIND_DUNGEON;
			if (isDungeon != wasDungeon) GStat.inDungeon.fetch_add(isDungeon ? 1 : -1);
			if (isDungeon)
			{
				GStat.dungeonEntered.fetch_add(1);
				const int stayMs = GConfig.dungeonStaySec * 1000;
				b.dungeonExitAtUs = stayMs > 0 ? NowUs() + (stayMs * 3 / 4 + rand() % (stayMs / 2 + 1)) * 1000LL : 0;
			}
			else
				b.dungeonExitAtUs = 0;
			b.nextDungeonEnterUs = NowUs() + 1500 * 1000LL;   // 포털 재사용 대기(1초) 뒤에 다시 요청
		}
		b.roomKind = static_cast<int>(pkt.kind());
		b.inField = (pkt.kind() == Protocol::ROOM_KIND_FIELD);
		b.monsters.clear();
		b.deadMonsters.clear();
		b.portals.clear();
		b.shopNpcId = 0;
		b.auctionNpcId = 0;
		b.drops.clear();
		b.skipDrops.clear();
		b.dropFreeAtUs.clear();
		b.recentKills.clear();
		b.pickupTarget = 0;
		b.players.clear();
		b.tradeTargetId = 0;
		b.moveLockUntilUs = 0;
		b.x = pkt.pos_info().x();
		b.y = pkt.pos_info().y();
		b.z = pkt.pos_info().z();
		b.correctionSeq = pkt.correction_seq();
		b.lastMoveSentUs = NowUs();
		b.hasGoal = false;
		b.spawnTarget = -1;
		b.blockedCount = 0;
		b.moveFailures = 0;
		b.aiming = false;             // Room이 바뀌면 조준은 서버에서도 풀린다
		b.pathValid = false;
		b.nextHuntPortalUs = 0;
		if (b.index == 0)
			printf("[bot] bot0 room change: kind=%d pos=(%.0f, %.0f, %.0f) seq=%u\n", static_cast<int>(pkt.kind()), b.x, b.y, b.z, pkt.correction_seq());
		break;
	}
	case Protocol::SC_DUNGEON_STATE:
	{
		Protocol::SC_DUNGEON_STATE_PACKET pkt;
		if (!pkt.ParseFromArray(payload, len)) { GStat.protocolError.fetch_add(1); return; }
		if (pkt.state() == Protocol::DUNGEON_STATE_CLEARED) GStat.dungeonCleared.fetch_add(1);
		break;
	}
	case Protocol::SC_MOVE_RESULT:
	{
		// 이동(Room 이동) 거부. 본인에게만 온다. 사유별로 센다(TIMEOUT·NOT_AVAILABLE 포함).
		Protocol::SC_MOVE_RESULT_PACKET pkt;
		if (!pkt.ParseFromArray(payload, len)) { GStat.protocolError.fetch_add(1); return; }
		GStat.moveRejected.fetch_add(1);
		const int code = static_cast<int>(pkt.result());
		if (code >= 0 && code < 8) GStat.moveRejectedBy[code].fetch_add(1);
		break;
	}
	case Protocol::SC_PARTY_INVITE:
	{
		// 받은 초대는 모두 수락한다(--party-size 가 없어도 — 원시 소켓 시험이나 다른 봇의 초대).
		GStat.partyInviteRecv.fetch_add(1);
		Protocol::CS_PARTY_RESPOND_PACKET respond;
		respond.set_accept(true);
		SendPacket(b, Protocol::CS_PARTY_RESPOND, respond);
		break;
	}
	case Protocol::SC_PARTY_STATE:
	{
		Protocol::SC_PARTY_STATE_PACKET pkt;
		if (!pkt.ParseFromArray(payload, len)) { GStat.protocolError.fetch_add(1); return; }
		GStat.partyStateRecv.fetch_add(1);
		const bool was = b.partyLeaderId != 0;
		b.partyLeaderId = pkt.members_size() > 0 ? pkt.leader_id() : 0;
		b.partyMembers.clear();
		for (const Protocol::PartyMember& member : pkt.members())
			b.partyMembers.insert(member.id());
		const bool is = b.partyLeaderId != 0;
		if (is != was) GStat.partyIn.fetch_add(is ? 1 : -1);
		break;
	}
	case Protocol::SC_PARTY_RESULT:
	{
		Protocol::SC_PARTY_RESULT_PACKET pkt;
		if (!pkt.ParseFromArray(payload, len)) { GStat.protocolError.fetch_add(1); return; }
		GStat.partyResult.fetch_add(1);
		const int code = static_cast<int>(pkt.result());
		if (code >= 0 && code < 16) GStat.partyResultBy[code].fetch_add(1);
		break;
	}
	case Protocol::SC_TELEPORT:
	{
		// 내 순간이동만 반영한다(다른 사람 것은 위치를 추적하지 않는다). 부활과 같이 위치·보정 순번에서 다시 시작.
		Protocol::SC_TELEPORT_PACKET pkt;
		if (!pkt.ParseFromArray(payload, len)) { GStat.protocolError.fetch_add(1); return; }
		if (pkt.object_id() != b.objectId) break;
		b.x = pkt.pos_info().x();
		b.y = pkt.pos_info().y();
		b.z = pkt.pos_info().z();
		b.correctionSeq = pkt.correction_seq();
		b.lastMoveSentUs = NowUs();
		b.hasGoal = false;
		b.moveFailures = 0;
		b.pathValid = false;
		GStat.portalMoved.fetch_add(1);
		break;
	}
	case Protocol::SC_DAMAGE:
		GStat.damageRecv.fetch_add(1);
		break;
	case Protocol::SC_ATTACK:
	{
		// 내 시전이 받아들여졌는지만 센다(사냥 프로필일 때만 파싱 — 이동 측정의 봇 비용을 늘리지 않게).
		if (!IsHunter(b)) break;
		Protocol::SC_ATTACK_PACKET pkt;
		if (!pkt.ParseFromArray(payload, len)) { GStat.protocolError.fetch_add(1); return; }
		if (pkt.attacker_id() == b.objectId && pkt.seq() != 0)
		{
			GStat.attackOk.fetch_add(1);
			// 서버 Character::BeginCast와 같은 계산(A5.2, D-299): 기본 공격(슬롯 0)만 AttackSpeed로 나눈다. 여유 50ms(서버 시계·전송 지연)
			if (const BotData::Skill* sk = BotData::FindSkill(pkt.skill_id()))
			{
				const float rate = sk->slot == 0 ? b.attackSpeed : 1.f;
				const long long now = NowUs();
				b.nextAttackUs = now + static_cast<long long>(sk->animMs / rate + 50) * 1000LL;
				b.moveLockUntilUs = now + static_cast<long long>(sk->moveLockMs / rate + 50) * 1000LL;
				b.lockProvisional = false;
				if (sk->slot >= 0 && sk->slot < 3) b.slotReadyUs[sk->slot] = now + (sk->cooldownMs + 100) * 1000LL;
			}
		}
		break;
	}
	case Protocol::SC_ATTACK_RESULT:
	{
		GStat.attackRejected.fetch_add(1);
		Protocol::SC_ATTACK_RESULT_PACKET pkt;
		if (!pkt.ParseFromArray(payload, len)) { GStat.protocolError.fetch_add(1); return; }
		const int code = static_cast<int>(pkt.result());
		if (code >= 0 && code < 16) GStat.attackRejectedBy[code].fetch_add(1);
		// 거부된 시전은 잠그지 않는다(임시 잠금을 푼다, A5.2)
		if (b.lockProvisional) { b.moveLockUntilUs = 0; b.lockProvisional = false; }
		b.nextAttackUs = min(b.nextAttackUs, NowUs() + 300 * 1000LL);
		break;
	}
	case Protocol::SC_CHANGE_EQUIP:
	{
		Protocol::SC_CHANGE_EQUIP_PACKET pkt;
		if (!pkt.ParseFromArray(payload, len)) { GStat.protocolError.fetch_add(1); return; }
		if (pkt.object_id() == b.objectId) GStat.equipOk.fetch_add(1);
		break;
	}
	case Protocol::SC_CHANGE_EQUIP_RESULT:
	{
		GStat.equipRejected.fetch_add(1);
		Protocol::SC_CHANGE_EQUIP_RESULT_PACKET pkt;
		if (!pkt.ParseFromArray(payload, len)) { GStat.protocolError.fetch_add(1); return; }
		const int code = static_cast<int>(pkt.result());
		if (code >= 0 && code < 16) GStat.equipRejectedBy[code].fetch_add(1);
		break;
	}
	case Protocol::SC_INVENTORY:
	{
		Protocol::SC_INVENTORY_PACKET pkt;
		if (!pkt.ParseFromArray(payload, len)) { GStat.protocolError.fetch_add(1); return; }
		GStat.inventoryRecv.fetch_add(1);
		b.items.clear();
		for (const Protocol::ItemInfo& item : pkt.items())
			b.items[item.item_db_id()] = { item.item_id(), static_cast<int>(item.equip_part()), item.count(), item.slot() };
		b.gold = pkt.gold();
		b.inventoryKnown = true;
		if (pkt.max_slots() > 0) b.maxSlots = pkt.max_slots();
		if (b.index == 0) printf("[bot] bot0 inventory: items=%d gold=%lld slots=%d\n", pkt.items_size(), static_cast<long long>(pkt.gold()), pkt.max_slots());
		break;
	}
	case Protocol::SC_INVENTORY_UPDATE:
	{
		Protocol::SC_INVENTORY_UPDATE_PACKET pkt;
		if (!pkt.ParseFromArray(payload, len)) { GStat.protocolError.fetch_add(1); return; }
		GStat.inventoryUpdateRecv.fetch_add(1);
		for (const Protocol::ItemInfo& item : pkt.changed())
			b.items[item.item_db_id()] = { item.item_id(), static_cast<int>(item.equip_part()), item.count(), item.slot() };
		for (const long long removed : pkt.removed())
			b.items.erase(removed);
		b.gold = pkt.gold();
		if (b.itemOpPending)
		{
			// 줍기·버리기 결과(서버는 아이템 작업을 1건씩 처리한다)
			b.itemOpPending = false;
			// 드랍은 SC_REMOVE_OBJECT가 먼저 와서 pickupTarget이 지워져 있을 수 있다 — 요청 종류로 센다
			if (b.itemOpKind == 1) GStat.pickupOk.fetch_add(1);
			b.pickupTarget = 0;
			b.itemOpKind = 0;
		}
		break;
	}
	case Protocol::SC_ITEM_RESULT:
	{
		Protocol::SC_ITEM_RESULT_PACKET pkt;
		if (!pkt.ParseFromArray(payload, len)) { GStat.protocolError.fetch_add(1); return; }
		GStat.itemRejected.fetch_add(1);
		const int code = static_cast<int>(pkt.result());
		if (code >= 0 && code < 24) GStat.itemRejectedBy[code].fetch_add(1);
		if (b.itemOpPending && (pkt.request() == Protocol::CS_PICKUP_ITEM || pkt.request() == Protocol::CS_DROP_ITEM))
		{
			b.itemOpPending = false;
			b.itemOpKind = 0;
			// 남의 드랍·이미 사라짐·가득 참이면 그 드랍은 다시 줍지 않는다. 거리·간격·작업 중(BUSY)은 다시 시도
			if (pkt.request() == Protocol::CS_PICKUP_ITEM && b.pickupTarget != 0 && pkt.result() != Protocol::ITEM_RESULT_TOO_FAR
				&& pkt.result() != Protocol::ITEM_RESULT_TOO_FREQUENT && pkt.result() != Protocol::ITEM_RESULT_BUSY)
				b.skipDrops.insert(b.pickupTarget);
			b.pickupTarget = 0;
		}
		break;
	}
	case Protocol::SC_SKILL_AIM:
	{
		Protocol::SC_SKILL_AIM_PACKET pkt;
		if (!pkt.ParseFromArray(payload, len)) { GStat.protocolError.fetch_add(1); return; }
		GStat.aimRecv.fetch_add(1);
		// 내 조준의 거부·시간 초과(seq가 최신일 때만)
		if (pkt.object_id() == b.objectId && !pkt.active() && pkt.seq() == b.aimSeq && b.aiming)
		{
			b.aiming = false;
			GStat.aimOwnOff.fetch_add(1);
		}
		break;
	}
	case Protocol::SC_CHAT:
		GStat.chatRecv.fetch_add(1);   // 방 전체 방송(Room::PlayerChat) — 받는 양이 채팅 봇 수 × 방 인원으로 커진다
		break;
	// ---- 1:1 거래(A5.2): 받은 요청은 거래 봇만 수락(5%는 거절), 그 밖의 봇은 거절한다
	case Protocol::SC_TRADE_REQUEST:
	{
		GStat.tradeRequestRecv.fetch_add(1);
		Protocol::CS_TRADE_RESPOND_PACKET respond;
		const bool accept = b.profile == BotProfile::Trade && !b.dead && rand() % 20 != 0;
		respond.set_accept(accept);
		SendPacket(b, Protocol::CS_TRADE_RESPOND, respond);
		if (accept)
		{
			b.tradeActive = true;
			b.tradeStartUs = NowUs();
			b.tradeOfferSent = b.tradeLockSent = b.tradeConfirmSent = false;
			b.tradeRetries = 0;
			b.tradeTargetId = 0;
		}
		break;
	}
	case Protocol::SC_TRADE_STATE:
	{
		Protocol::SC_TRADE_STATE_PACKET pkt;
		if (!pkt.ParseFromArray(payload, len)) { GStat.protocolError.fetch_add(1); return; }
		if (!b.tradeActive || b.profile != BotProfile::Trade) break;
		if (pkt.phase() != Protocol::TRADE_PHASE_OFFERING) break;
		const Protocol::TradeOffer* mine = nullptr;
		const Protocol::TradeOffer* theirs = nullptr;
		for (const Protocol::TradeOffer& o : pkt.offers())
			(o.object_id() == b.objectId ? mine : theirs) = &o;
		if (mine == nullptr || theirs == nullptr) break;
		// 한 번은 중간에 취소한다(10% — 취소 경로 부하)
		if (!b.tradeOfferSent && rand() % 10 == 0)
		{
			SendPacket(b, Protocol::CS_TRADE_CANCEL, Protocol::CS_TRADE_CANCEL_PACKET());
			GStat.tradeCancelSent.fetch_add(1);
			break;
		}
		if (!b.tradeOfferSent)
		{
			// 장착하지 않은 행 1~2개(장비는 1개, 스택은 일부) 또는 골드 일부. 낼 것이 없으면 골드 1
			Protocol::CS_TRADE_SET_OFFER_PACKET offer;
			int want = 1 + rand() % 2;
			for (const auto& [dbId, item] : b.items)
			{
				if (want == 0) break;
				if (item.equip != 0 || item.count <= 0 || rand() % 2 == 0) continue;
				Protocol::TradeOfferItem* oi = offer.add_items();
				oi->set_item_db_id(dbId);
				oi->set_count(item.count > 1 ? 1 + rand() % min<long long>(item.count, 3) : 1);
				--want;
			}
			if (offer.items_size() == 0 || rand() % 3 == 0) offer.set_gold(min<long long>(b.gold, 1 + rand() % 50));
			if (GConfig.chaos && rand() % 20 == 0) offer.set_gold(-5);
			SendPacket(b, Protocol::CS_TRADE_SET_OFFER, offer);
			GStat.tradeOfferSent.fetch_add(1);
			b.tradeOfferSent = true;
			break;
		}
		const bool myOffer = mine->items_size() > 0 || mine->gold() > 0;
		if (mine->locked()) b.tradeLockSent = false;          // 잠금이 반영됐다 — 서버가 풀면(상대가 제시를 바꿈) 다시 잠근다
		if (!mine->confirmed() && !(mine->locked() && theirs->locked())) b.tradeConfirmSent = false;
		if (myOffer && !mine->locked() && !b.tradeLockSent)
		{
			Protocol::CS_TRADE_LOCK_PACKET lockPkt;
			lockPkt.set_locked(true);
			SendPacket(b, Protocol::CS_TRADE_LOCK, lockPkt);
			b.tradeLockSent = true;
			break;
		}
		if (mine->locked() && theirs->locked() && !mine->confirmed() && !b.tradeConfirmSent)
		{
			SendPacket(b, Protocol::CS_TRADE_CONFIRM, Protocol::CS_TRADE_CONFIRM_PACKET());
			GStat.tradeConfirmSent.fetch_add(1);
			b.tradeConfirmSent = true;
		}
		break;
	}
	case Protocol::SC_TRADE_END:
	{
		Protocol::SC_TRADE_END_PACKET pkt;
		if (!pkt.ParseFromArray(payload, len)) { GStat.protocolError.fetch_add(1); return; }
		GStat.tradeEnd.fetch_add(1);
		const int code = static_cast<int>(pkt.result());
		if (code >= 0 && code < 24) GStat.tradeEndBy[code].fetch_add(1);
		b.tradeActive = false;
		b.tradeTargetId = 0;
		const int half = GConfig.tradeIntervalMs / 2;
		b.nextTradeUs = NowUs() + (GConfig.tradeIntervalMs - half + rand() % (2 * half + 1)) * 1000LL;
		break;
	}
	case Protocol::SC_TRADE_RESULT:
	{
		Protocol::SC_TRADE_RESULT_PACKET pkt;
		if (!pkt.ParseFromArray(payload, len)) { GStat.protocolError.fetch_add(1); return; }
		const int code = static_cast<int>(pkt.result());
		if (code >= 0 && code < 24) GStat.tradeRejectBy[code].fetch_add(1);
		{
			const int req = static_cast<int>(pkt.request()) - static_cast<int>(Protocol::CS_TRADE_REQUEST);
			if (req >= 0 && req < 8) GStat.tradeRejectReq[req].fetch_add(1);
		}
		if (pkt.request() == Protocol::CS_TRADE_REQUEST)
		{
			// 요청이 거부됐다(상대가 거래 중·거리 밖 등) — 잠시 뒤 다른 상대를 고른다
			b.tradeActive = false;
			b.tradeTargetId = 0;
			b.nextTradeUs = NowUs() + (2000 + rand() % 3000) * 1000LL;
		}
		else if (pkt.request() == Protocol::CS_TRADE_CONFIRM && ++b.tradeRetries <= 3)
		{
			// 확정 재검증 실패(서버가 제시 단계로 되돌림) — 다시 잠그고 확정한다. 3번 넘으면 취소
			b.tradeConfirmSent = false;
		}
		else if (b.tradeActive)
		{
			SendPacket(b, Protocol::CS_TRADE_CANCEL, Protocol::CS_TRADE_CANCEL_PACKET());
			GStat.tradeCancelSent.fetch_add(1);
		}
		break;
	}
	// ---- 경매(A5.2)
	case Protocol::SC_AUCTION_LIST:
	{
		Protocol::SC_AUCTION_LIST_PACKET pkt;
		if (!pkt.ParseFromArray(payload, len)) { GStat.protocolError.fetch_add(1); return; }
		GStat.auctionListRecv.fetch_add(1);
		b.otherListings.clear();
		for (const Protocol::AuctionListingInfo& l : pkt.listings())
			if (!l.mine() && l.count() > 0) b.otherListings.push_back({ l.listing_id(), l.count(), l.unit_price() });
		if (b.auctionPendingKind == 0) { b.auctionPending = false; GStat.auctionOk[0].fetch_add(1); }
		break;
	}
	case Protocol::SC_AUCTION_MY:
	{
		Protocol::SC_AUCTION_MY_PACKET pkt;
		if (!pkt.ParseFromArray(payload, len)) { GStat.protocolError.fetch_add(1); return; }
		GStat.auctionMyRecv.fetch_add(1);
		b.myListings.clear();
		b.mails.clear();
		for (const Protocol::AuctionListingInfo& l : pkt.listings()) b.myListings.push_back(l.listing_id());
		for (const Protocol::MailInfo& m : pkt.mails()) b.mails.push_back(m.mail_id());
		b.myKnown = true;
		if (b.auctionPendingKind == 4) { b.auctionPending = false; GStat.auctionOk[4].fetch_add(1); }
		break;
	}
	case Protocol::SC_AUCTION_RESULT:
	{
		Protocol::SC_AUCTION_RESULT_PACKET pkt;
		if (!pkt.ParseFromArray(payload, len)) { GStat.protocolError.fetch_add(1); return; }
		const int code = static_cast<int>(pkt.result());
		if (code >= 0 && code < 32) GStat.auctionResultBy[code].fetch_add(1);
		const long long now = NowUs();
		if (b.auctionPending)
		{
			b.auctionPending = false;
			const int k = b.auctionPendingKind;
			if (pkt.result() == Protocol::AUCTION_RESULT_SUCCESS)
			{
				if (k >= 0 && k < 6) GStat.auctionOk[k].fetch_add(1);
				if (k == 1) b.myListings.push_back(pkt.id());                                                     // 등록
				if (k == 3) b.myListings.erase(remove(b.myListings.begin(), b.myListings.end(), pkt.id()), b.myListings.end());   // 취소
				if (k == 5) b.mails.erase(remove(b.mails.begin(), b.mails.end(), pkt.id()), b.mails.end());       // 수령
			}
			if (k == 2)   // 구매: 결과와 상관없이 그 등록은 목록에서 뺀다(팔림·내 것·남은 수량 등)
				b.otherListings.erase(remove_if(b.otherListings.begin(), b.otherListings.end(), [&](const Bot::Listing& l) { return l.id == pkt.id(); }), b.otherListings.end());
			if (pkt.result() == Protocol::AUCTION_RESULT_NO_MAIL || pkt.result() == Protocol::AUCTION_RESULT_NO_LISTING) b.myKnown = false;   // 내 목록을 다시 받는다
			// 요청 간격·혼잡이면 조금 더 기다린다(A4.3 act_full과 같은 뜻)
			if (pkt.result() == Protocol::AUCTION_RESULT_TOO_FREQUENT || pkt.result() == Protocol::AUCTION_RESULT_TRY_LATER || pkt.result() == Protocol::AUCTION_RESULT_BUSY)
				b.nextAuctionUs = max(b.nextAuctionUs, now + 1500 * 1000LL);
		}
		break;
	}
	case Protocol::SC_MAIL_NOTICE:
		GStat.mailNoticeRecv.fetch_add(1);
		b.myKnown = false;   // 새 우편 — 다음 차례에 내 목록을 받는다
		break;
	// 성장·MP(A2.3b): 봇은 값을 쓰지 않고 수만 센다(더미 봇은 경험치를 받지 않는다 — 드랍 소유권과 같은 규칙)
	case Protocol::SC_CHANGE_MP:
		GStat.mpChangeRecv.fetch_add(1);
		break;
	case Protocol::SC_GAIN_EXP:
	case Protocol::SC_LEVEL_UP:
		GStat.expRecv.fetch_add(1);
		break;
	default:
		break;
	}
}

// 수신 버퍼에서 완성된 패킷을 모두 처리한다. 프레임이 잘못되면 false.
static bool ProcessRecvBuffer(Bot& b, SampleBuffer& samples)
{
	while (b.rbuf.size() - b.rpos >= sizeof(Header))
	{
		Header h;
		memcpy(&h, b.rbuf.data() + b.rpos, sizeof(h));
		if (h.size < sizeof(Header)) { GStat.protocolError.fetch_add(1); return false; }
		if (b.rbuf.size() - b.rpos < h.size) break;

		GStat.pktRecv.fetch_add(1);
		HandlePacket(b, samples, h.id, b.rbuf.data() + b.rpos + sizeof(Header), h.size - static_cast<int>(sizeof(Header)));
		b.rpos += h.size;
	}
	if (b.rpos > 0 && b.rpos == b.rbuf.size()) { b.rbuf.clear(); b.rpos = 0; }
	else if (b.rpos > 65536) { b.rbuf.erase(b.rbuf.begin(), b.rbuf.begin() + b.rpos); b.rpos = 0; }
	return true;
}

static void PublishSnapshot(const vector<Bot>& bots, SnapshotBuffer& out)
{
	lock_guard<mutex> lock(out.m);
	out.bots.clear();
	out.monsters.clear();
	for (const Bot& b : bots)
	{
		if (b.state == BotState::Idle) continue;
		out.bots.push_back({ b.index, b.state, b.x, b.y });
		for (const auto& [id, pos] : b.monsters)
			out.monsters.push_back({ id, pos.first, pos.second, b.deadMonsters.count(id) > 0 });
	}
}

static void WorkerMain(int workerIndex, int workerCount)
{
	vector<Bot> bots;
	for (int i = workerIndex; i < GConfig.bots; i += workerCount)
	{
		Bot b;
		b.index = i;
		b.profile = GBotProfiles[i];
		bots.push_back(std::move(b));
	}
	SampleBuffer& samples = *GSamples[workerIndex];

	vector<WSAPOLLFD> fds;
	vector<Bot*> owners;
	vector<uint8_t> tmp(16 * 1024);
	long long nextSnapshotUs = 0;

	while (GRunning.load())
	{
		const long long now = NowUs();
		const int allowed = GAllowed.load();

		if (GConfig.trackWorld && now >= nextSnapshotUs)
		{
			nextSnapshotUs = now + 50 * 1000LL;
			PublishSnapshot(bots, *GSnapshots[workerIndex]);
		}

		fds.clear();
		owners.clear();

		for (Bot& b : bots)
		{
			// 재시도(A5.2, D-300): 예약 시각이 되면 상태를 처음으로 되돌려 다시 접속한다(번호·프로필·재시도 횟수만 남김)
			if (b.state == BotState::Closed && b.retryAtUs != 0 && now >= b.retryAtUs)
			{
				Bot fresh;
				fresh.index = b.index;
				fresh.profile = b.profile;
				fresh.retryCount = b.retryCount;
				fresh.firstConnectUs = b.firstConnectUs;
				b = std::move(fresh);
			}
			if (b.state == BotState::Idle && b.index < allowed)
				StartConnect(b);

			if (b.state == BotState::Connecting && now - b.connectStartUs > 10 * 1000 * 1000LL)
			{
				GStat.connectFail.fetch_add(1);
				closesocket(b.sock); b.sock = INVALID_SOCKET; b.state = BotState::Closed;
				ScheduleRetry(b);
			}
			// 로그인 실패·응답 없음(A5.2): 닫고 다시 시도한다. 응답 없음은 서버 DB 대기(로그인 폭주)일 수 있다
			if (b.state == BotState::LoggingIn && (b.loginFailed || (b.loginSentUs > 0 && now - b.loginSentUs > GConfig.loginTimeoutMs * 1000LL)))
			{
				if (!b.loginFailed) GStat.loginTimeout.fetch_add(1);
				CloseBot(b, false);
				ScheduleRetry(b);
				continue;
			}

			if (b.state == BotState::LoggingIn && b.loginDueUs != 0 && now >= b.loginDueUs)
			{
				b.loginDueUs = 0;
				Protocol::CS_LOGIN_PACKET login;
				login.set_account_id("fem_" + to_string(b.index));   // 가입한 계정과 같아야 한다(옛 "arc_" 잔재 수정)
				login.set_account_pw("1234");
				b.loginSentUs = now;
				SendPacket(b, Protocol::CS_LOGIN, login);
			}

			if (b.state == BotState::InGame && b.spawnKnown)
			{
				// 사망 중(A2.4): 대기 시간이 지나면 부활을 요청하고(응답이 없으면 1초마다 다시), 그동안 이동·공격·장비는 보내지 않는다.
				// chaos는 죽은 채로도 계속 보내고(서버가 버리거나 거부해야 한다) 부활 요청도 두 번 연달아 보낸다(중복 요청).
				if (b.dead && now >= b.reviveAtUs)
				{
					b.reviveAtUs = now + 1000 * 1000LL;
					Protocol::CS_REVIVE_PACKET revive;
					for (int k = 0; k < (GConfig.chaos ? 2 : 1); ++k)
					{
						SendPacket(b, Protocol::CS_REVIVE, revive);
						GStat.reviveSent.fetch_add(1);
					}
				}
				const bool canAct = !b.dead || GConfig.chaos;
				// 처음 배치된 곳: 입장 직후 포털 목록이 오면 정한다(마을 쪽 문 포털이 있으면 마을)
				if (b.home == 0 && !b.portals.empty()) b.home = InTownRoom(b) ? 1 : 2;

				// 잠수(A5.2): 30~90초마다 3~8초 동안만 짧게 걷는다
				if (b.profile == BotProfile::Idle && now >= b.nextIdleWalkUs)
				{
					const bool first = b.nextIdleWalkUs == 0;
					b.nextIdleWalkUs = now + (30000 + rand() % 60000) * 1000LL;
					if (!first) { b.idleWalkUntilUs = now + (3000 + rand() % 5000) * 1000LL; b.hasGoal = false; }
				}
				const bool idleWalking = b.profile == BotProfile::Idle && now < b.idleWalkUntilUs;
				// 줍기(A5.2): 15m 안의 가장 가까운 드랍(주울 수 없다고 판정된 것 제외)을 목표로 삼는다
				if (b.profile == BotProfile::Loot && b.pickupTarget == 0 && !b.itemOpPending)
				{
					float best = 1500.f * 1500.f;
					for (const auto& [id, d] : b.drops)
					{
						if (b.skipDrops.count(id)) continue;
						if (auto fit = b.dropFreeAtUs.find(id); fit != b.dropFreeAtUs.end() && now < fit->second) continue;   // 아직 남의 소유
						const float dx = d[0] - b.x, dy = d[1] - b.y;
						if (dx * dx + dy * dy < best) { best = dx * dx + dy * dy; b.pickupTarget = id; }
					}
				}
				const bool looting = b.profile == BotProfile::Loot && b.pickupTarget != 0 && b.drops.count(b.pickupTarget) > 0;
				const bool wantsMove = (MovesFreely(b) || idleWalking || b.profile == BotProfile::Auction) && !(b.profile == BotProfile::Trade && b.tradeActive);

				// hunt 프로필은 주변에 알려진 몬스터가 없을 때(또는 주울 드랍이 있을 때)만 걸어서 몬스터를 찾는다.
				if (canAct && wantsMove && now >= b.nextMoveUs && (!IsHunter(b) || looting || b.monsters.size() <= b.deadMonsters.size() || (GConfig.dungeon && b.roomKind != static_cast<int>(Protocol::ROOM_KIND_DUNGEON))))
				{
					b.nextMoveUs = now + GConfig.moveIntervalMs * 1000LL;
					const float step = min(300.f, MaxStep(b, now));
					float nx = b.x, ny = b.y, yaw = 0.f;
					bool stay = false;   // 이번에는 보내지 않는다(목표 앞에 서 있음)
					const float zdx = GConfig.zoneX - b.x, zdy = GConfig.zoneY - b.y;
					const float zoneDist = sqrtf(zdx * zdx + zdy * zdy);
					// 경매 NPC 위치: 시야에서 본 NPC, 없으면 맵 로직 JSON의 마커
					const bool npcKnown = b.auctionNpcId != 0 || GAuctionNpcKnown;
					const float npcX = b.auctionNpcId != 0 ? b.auctionNpcX : GAuctionNpcX, npcY = b.auctionNpcId != 0 ? b.auctionNpcY : GAuctionNpcY;
					const auto partner = b.tradeTargetId != 0 ? b.players.find(b.tradeTargetId) : b.players.end();
					if (const array<float, 3>* tp = DoorPortalGoal(b); tp && b.moveFailures < 3)
						{
							// 마을 Room의 사냥 봇(DG3b): 가장 가까운 마을 포털 앞(150cm)까지 직선으로 걸어간다. 요청은 아래 포털 블록이 보낸다.
							// 막혀서 보정이 쌓이면(moveFailures 3 이상) 이 분기를 건너뛰고 무작위 이동으로 비킨다.
							const float dx = (*tp)[0] - b.x, dy = (*tp)[1] - b.y;
							const float dist = sqrtf(dx * dx + dy * dy);
							yaw = atan2f(dy, dx) * 180.f / 3.14159265f;
							if (dist > kPortalStopCm)
								StepToward(b, (*tp)[0], (*tp)[1], min({ 500.f, dist - kPortalStopCm, MaxStep(b, now) }), nx, ny, yaw);
						}
						else if (looting)
						{
							// 드랍 앞(150cm)까지 걸어간다. 줍기 요청은 아래 줍기 블록이 보낸다(PickupRangeCm 300 안)
							const array<float, 3>& d = b.drops[b.pickupTarget];
							const float dx = d[0] - b.x, dy = d[1] - b.y;
							const float dist = sqrtf(dx * dx + dy * dy);
							if (dist > 150.f) StepToward(b, d[0], d[1], min({ 500.f, dist - 100.f, MaxStep(b, now) }), nx, ny, yaw);
							else stay = true;
						}
						else if (b.profile == BotProfile::Auction && InTownRoom(b) && npcKnown)
						{
							// 경매 NPC 앞(200cm — 이용 거리 ShopRangeCm 400 안)까지 걸어가 그 자리에 선다
							const float dx = npcX - b.x, dy = npcY - b.y;
							const float dist = sqrtf(dx * dx + dy * dy);
							if (dist > 200.f) StepToward(b, npcX, npcY, min({ 500.f, dist - 150.f, MaxStep(b, now) }), nx, ny, yaw);
							else stay = true;
						}
						else if (b.profile == BotProfile::Auction && InTownRoom(b))
							stay = true;   // NPC를 아직 모르면 기다린다(시야에 들어오면 SC_ADD_OBJECT로 안다)
						else if (b.profile == BotProfile::Trade && partner != b.players.end())
						{
							// 고른 거래 상대 앞(250cm — TradeRangeCm 500 안)까지 다가간다
							const float dx = partner->second.first - b.x, dy = partner->second.second - b.y;
							const float dist = sqrtf(dx * dx + dy * dy);
							if (dist > 250.f) StepToward(b, partner->second.first, partner->second.second, min({ 500.f, dist - 200.f, MaxStep(b, now) }), nx, ny, yaw);
							else stay = true;
						}
						else if (b.profile == BotProfile::Turn)
					{
						// 직선으로 걷다 일정 간격마다 왼쪽으로 90° 꺾는다(사각형을 돌아 시작점 근처에 머문다). 급회전 = 원격 표시의 최악 조건.
						bool corner = false;
						if (now >= b.nextTurnUs)
						{
							corner = b.nextTurnUs != 0;
							if (corner && !GConfig.turnSend) b.heading += 3.14159265f / 2.f;
							b.nextTurnUs = now + GConfig.turnIntervalMs * 1000LL;
						}
						const float stride = MaxStep(b, now);
						nx = b.x + cosf(b.heading) * stride; ny = b.y + sinf(b.heading) * stride;
						yaw = b.heading * 180.f / 3.14159265f;
						if (corner && GConfig.turnSend)
						{
							// 모서리까지 옛 방향으로 온 위치를 지금 보내고, 꺾은 방향은 서버 최소 간격(50ms)을 넘긴 60ms 뒤 패킷부터
							b.heading += 3.14159265f / 2.f;
							b.nextMoveUs = now + 60 * 1000LL;
						}
					}
					else if (GConfig.zone && b.roomKind != static_cast<int>(Protocol::ROOM_KIND_DUNGEON) && zoneDist > GConfig.zoneR && b.moveFailures < 3)
					{
						// 핫존 밖이면 중심 쪽으로 걸어간다(길 찾기 없음 — 막히면 아래 무작위 이동으로 잠시 비킨다).
						StepToward(b, GConfig.zoneX, GConfig.zoneY, min({ 500.f, zoneDist, MaxStep(b, now) }), nx, ny, yaw);
					}
					else if ((!GConfig.zone || b.roomKind == static_cast<int>(Protocol::ROOM_KIND_DUNGEON)) && IsHunter(b) && !HuntSpawns(b).empty() && b.moveFailures < 3
						&& !WandersTown(b))
					{
						// 맵 정보가 있으면 가장 가까운 몬스터 스폰 쪽으로 한 걸음씩 다가간다. 막혀서 보정되면 잠시 무작위로 돌아다니고, 계속 막히면 목표를 바꾼다.
						// 던전 안에서는 던전 레벨의 스폰(--dungeon-map)을 쓴다(핫존은 입구 앞에 세우는 용도라 던전 안에서는 무시).
						const vector<pair<float, float>>& spawns = HuntSpawns(b);
						if (b.spawnTarget < 0 || b.spawnTarget >= static_cast<int>(spawns.size()))
						{
							float best = 1e30f;
							for (int i = 0; i < static_cast<int>(spawns.size()); ++i)
							{
								const float sx = spawns[i].first - b.x, sy = spawns[i].second - b.y;
								if (sx * sx + sy * sy < best) { best = sx * sx + sy * sy; b.spawnTarget = i; }
							}
						}
						const auto& spawn = spawns[b.spawnTarget];
						const float dx = spawn.first - b.x, dy = spawn.second - b.y;
						const float dist = sqrtf(dx * dx + dy * dy);
						yaw = atan2f(dy, dx) * 180.f / 3.14159265f;
						if (dist > 1.f) StepToward(b, spawn.first, spawn.second, min({ 500.f, dist, MaxStep(b, now) }), nx, ny, yaw);
					}
					else
					{
						// 목표 지점으로 걷는다. 도착·시간 초과(10초)·막힘(보정)이면 새 목표: 10~30m 떨어진 임의 지점,
						// 핫존 안이면 존 반경 안의 임의 지점, --town-wander 마을 봇은 마을 안전 구역 안. 내비메시가 있으면 길을 따라간다(A5.1).
						float gdx = b.goalX - b.x, gdy = b.goalY - b.y;
						float goalDist = sqrtf(gdx * gdx + gdy * gdy);
						if (!b.hasGoal || goalDist < 100.f || now >= b.goalUntilUs)
						for (int attempt = 0; attempt < 5; ++attempt)   // 길이 닿지 않는 목표면 다른 목표를 고른다(최대 5번, 그래도 못 찾으면 직선)
						{
							const float angle = (rand() % 3600) * 0.1f * 3.14159265f / 180.f;
							if (WandersTown(b) && !GTownZones.empty())
							{
								// --town-wander: 마을 안전 구역 안의 임의 지점(구역이 여럿이면 하나를 고름)
								const TownZone& tz = GTownZones[rand() % GTownZones.size()];
								const float d = tz.r * sqrtf((rand() % 1000) / 1000.f);
								b.goalX = tz.x + cosf(angle) * d;
								b.goalY = tz.y + sinf(angle) * d;
							}
							else if (GConfig.zone && zoneDist <= GConfig.zoneR)
							{
								const float d = GConfig.zoneR * sqrtf((rand() % 1000) / 1000.f);
								b.goalX = GConfig.zoneX + cosf(angle) * d;
								b.goalY = GConfig.zoneY + sinf(angle) * d;
							}
							else
							{
								const float d = b.profile == BotProfile::Idle ? 200.f + (rand() % 400) : 1000.f + (rand() % 2000);   // 잠수는 몇 m만
								b.goalX = b.x + cosf(angle) * d;
								b.goalY = b.y + sinf(angle) * d;
							}
							b.hasGoal = true;
							b.goalUntilUs = now + 10'000'000LL;
							const bool reachable = ClampGoalToPath(b);
							gdx = b.goalX - b.x; gdy = b.goalY - b.y;
							goalDist = sqrtf(gdx * gdx + gdy * gdy);
							if (reachable) break;
						}
						if (goalDist > 1.f)
						{
							yaw = atan2f(gdy, gdx) * 180.f / 3.14159265f;
							StepToward(b, b.goalX, b.goalY, min(goalDist, step), nx, ny, yaw);
						}
						if (b.moveFailures > 0) --b.moveFailures;   // 무작위 이동을 몇 번 하면 다시 스폰(또는 핫존) 쪽으로 향한다
					}
					if (!stay) SendMove(b, now, nx, ny, yaw, Protocol::MOVE_STATE_RUN);
			}
				// 사냥 봇의 마을 포털 요청(DG3b): 포털 앞 200cm 안에 오면 CS_PORTAL을 보낸다. 서버는 거리·재사용 대기가 안 맞으면 응답 없이 버리므로
					// 응답(SC_ROOM_CHANGE)이 올 때까지 1초마다 다시 보낸다. 방이 바뀌면 SC_ROOM_CHANGE가 이 시각을 지운다.
					if (canAct && now >= b.nextHuntPortalUs)
					{
						if (const array<float, 3>* tp = DoorPortalGoal(b); tp)
						{
							const float dx = (*tp)[0] - b.x, dy = (*tp)[1] - b.y;
							if (dx * dx + dy * dy <= kPortalSendCm * kPortalSendCm)
							{
								b.nextHuntPortalUs = now + 1000 * 1000LL;
								Protocol::CS_PORTAL_PACKET portal;
								portal.set_portal_id(static_cast<int>((*tp)[2]));
								SendPacket(b, Protocol::CS_PORTAL, portal);
								GStat.portalSent.fetch_add(1);
							}
						}
					}
					if (canAct && IsHunter(b) && !looting && now >= b.nextAttackUs && b.monsters.size() > b.deadMonsters.size() && !(GConfig.dungeon && b.roomKind != static_cast<int>(Protocol::ROOM_KIND_DUNGEON)))   // --dungeon: outside the dungeon, do not attack (the portal request is dropped while casting)
				{
					// 다음 공격 시각은 내 SC_ATTACK(받아들여진 시전)이 스킬 동작 시간으로 다시 정한다(A5.2). 응답이 없을 때의 기본값
					b.nextAttackUs = now + (BotData::Loaded() ? 1500 : 700) * 1000LL;

					// 시야 안에서 가장 가까운 몬스터를 고른다. 서버가 허용하는 속도만큼 몬스터 앞으로 다가간 뒤 그 방향을 보고 공격한다.
					int targetId = -1; float tx = 0, ty = 0, best = 1e30f;
					for (const auto& [id, pos] : b.monsters)
					{
						if (b.deadMonsters.count(id)) continue;
						const float dx = pos.first - b.x, dy = pos.second - b.y;
						const float d = dx * dx + dy * dy;
						if (d < best) { best = d; targetId = id; tx = pos.first; ty = pos.second; }
					}

					const float dx = tx - b.x, dy = ty - b.y;
					const float dist = sqrtf(dx * dx + dy * dy);
					float yaw = atan2f(dy, dx) * 180.f / 3.14159265f;
					if (dist > 120.f)
					{
						const float stride = min(dist - 100.f, MaxStep(b, now));
						float nx = b.x, ny = b.y, moveYaw = yaw;
						StepToward(b, tx, ty, stride, nx, ny, moveYaw);
						SendMove(b, now, nx, ny, yaw, stride < dist - 100.f ? Protocol::MOVE_STATE_RUN : Protocol::MOVE_STATE_IDLE);
					}
					// 아직 닿지 않았으면(다가가는 중) 이번에는 공격하지 않는다(A5.2 — 실제 클라이언트는 사거리 밖에서 누르지 않음, TOO_FAR 거부를 만들지 않게)
					const float rdx = tx - b.x, rdy = ty - b.y;
					const bool inReach = !BotData::Loaded() || rdx * rdx + rdy * rdy <= 190.f * 190.f;
					if (!inReach) b.nextAttackUs = now + 200 * 1000LL;

					// 일반공격(슬롯 0)을 보내고, 3번에 한 번은 1번 스킬(슬롯 1)을 쓴다.
					// 몬스터 방향(aim_yaw)·위치(aim_x/y)와 대상(target_id)을 함께 보내고, 서버가 장착 무기의 스킬에 맞는 값만 쓴다.
					// (MC3.1: 무기가 없어 맨손 기본 공격만 있고 1번 슬롯은 서버가 거부한다 — MC3.2 장착 후 검·활 스킬)
					// SK2(D-278): 6번에 한 번은 2번 스킬(슬롯 2)도 쓴다 — 기본 공격 4 : 스킬1 1 : 스킬2 1.
					Protocol::CS_ATTACK_PACKET atk;
					const int castNo = ++b.castCount;
					int slot = (castNo % 6 == 0) ? 2 : (castNo % 3 == 0) ? 1 : 0;
					// 쿨타임이 남은 스킬은 쓰지 않는다(A5.2 — 실제 클라이언트는 쿨타임 중 버튼이 눌리지 않음)
					if (BotData::Loaded() && slot > 0 && now < b.slotReadyUs[slot]) slot = 0;
					// --aim: 슬롯 1은 조준을 먼저 켜고 300~800ms 뒤 다음 공격 차례에 쏜다. 10%는 쏘지 않고 취소한다.
					bool skipAttack = false;
					if (GConfig.aim)
					{
						if (b.aiming)
						{
							slot = 1;
							b.aiming = false;   // 서버는 CS_ATTACK을 받으며 해제한다
							if (rand() % 10 == 0)
							{
								Protocol::CS_SKILL_AIM_PACKET off;
								off.set_slot(1); off.set_active(false); off.set_seq(++b.aimSeq);
								SendPacket(b, Protocol::CS_SKILL_AIM, off);
								GStat.aimSent.fetch_add(1);
								skipAttack = true;
							}
						}
						else if (slot == 1)
						{
							Protocol::CS_SKILL_AIM_PACKET on;
							on.set_slot(1); on.set_active(true); on.set_seq(++b.aimSeq);
							SendPacket(b, Protocol::CS_SKILL_AIM, on);
							GStat.aimSent.fetch_add(1);
							b.aiming = true;
							b.nextAttackUs = now + (300 + rand() % 501) * 1000LL;
							skipAttack = true;
						}
					}
					atk.set_skill_slot(slot);
					// 근접 기본 공격은 대상이 없으면 헛스윙으로 보낸다(D-278) — 5%는 대상 없이 보내 그 경로도 시험한다.
					atk.set_target_id((slot == 0 && rand() % 20 == 0) ? 0 : targetId);
					atk.set_aim_yaw(yaw);
					atk.set_aim_x(tx);
					atk.set_aim_y(ty);

					// 오류 테스트: 일부 요청을 일부러 잘못된 값으로 바꾼다. 서버는 크래시 없이 무시해야 한다.
					if (GConfig.chaos && rand() % 100 < 40)
					{
						switch (rand() % 8)
						{
						case 0: atk.set_skill_slot(-3); break;
						case 1: atk.set_skill_slot(9); break;
						case 2: atk.set_aim_yaw(std::numeric_limits<float>::quiet_NaN()); break;
						case 3: atk.set_aim_x(std::numeric_limits<float>::infinity()); break;
						case 4: atk.set_target_id(99999999); break;                 // 존재하지 않는 대상
						case 5: atk.set_aim_x(1.0e9f); atk.set_aim_y(-1.0e9f); break; // 사거리 밖 지점(서버가 보정)
						case 6: atk.set_target_id(b.objectId); break;                // 자기 자신 (적대 관계 아님)
						default: atk.set_skill_slot(5); break;                       // 비어 있는 슬롯(2는 SK2부터 스킬이 있음)
						}
					}
					if (!skipAttack && inReach)
					{
						atk.set_seq(++b.attackSeq);
						b.lastTargetId = targetId;
						GStat.attackSent.fetch_add(1);
						SendPacket(b, Protocol::CS_ATTACK, atk);
						// 응답 전 임시 이동 잠금(A5.2): 시전이 받아들여지면 SC_ATTACK이 스킬 시간으로 확정하고, 거부면 푼다
						if (BotData::Loaded()) { b.moveLockUntilUs = max(b.moveLockUntilUs, now + 300 * 1000LL); b.lockProvisional = true; }
					}
				}
				// --aim: 처음 한 번 활을 장착한다(조준할 수 있는 스킬 3002). 장착은 내 인벤토리의 item_db_id로 요청하므로(A3.3)
				// 활(6102)이 없으면 개발 지급 명령으로 먼저 받는다(서버 [Debug] DevCommands=true 필요).
				if (!b.dead && GConfig.aim && !b.aimBowSent && b.inventoryKnown)
				{
					long long bowDbId = 0;
					for (const auto& [dbId, item] : b.items)
						if (item.itemId == 6102 && item.equip == 0) { bowDbId = dbId; break; }
					if (bowDbId != 0)
					{
						b.aimBowSent = true;
						Protocol::CS_CHANGE_EQUIP_PACKET eq;
						eq.set_part(Protocol::EQUIP_PART_WEAPON);
						eq.set_item_db_id(bowDbId);
						SendPacket(b, Protocol::CS_CHANGE_EQUIP, eq);
						GStat.equipSent.fetch_add(1);
					}
					else if (!b.aimBowGiveSent)
					{
						b.aimBowGiveSent = true;
						Protocol::CS_DEV_GIVE_PACKET give;
						give.set_item_id(6102);
						give.set_count(1);
						SendPacket(b, Protocol::CS_DEV_GIVE, give);
						GStat.giveSent.fetch_add(1);
					}
				}
				// --give-gear: 판금 세트(슬롯 1~7의 x301)와 활 두 종(6102·6302)을 하나씩 받는다. 서버 요청 간격(ItemMinIntervalMs)보다 넉넉히 띄운다.
				if (GConfig.giveGear && b.inventoryKnown && b.gearGiveIndex < 9 && now >= b.nextGiveUs)
				{
					static const int GEAR[9] = { 1301, 2301, 3301, 4301, 5301, 6301, 7301, 6102, 6302 };
					b.nextGiveUs = now + 300 * 1000LL;
					Protocol::CS_DEV_GIVE_PACKET give;
					give.set_item_id(GEAR[b.gearGiveIndex++]);
					give.set_count(1);
					SendPacket(b, Protocol::CS_DEV_GIVE, give);
					GStat.giveSent.fetch_add(1);
				}
				// --give-interval: 물약(8001) 1~5개 또는 골드를 받는다. 10%는 바로 한 번 더 보낸다(요청 간격·BUSY 거부 시험).
				else if (GConfig.giveIntervalMs > 0 && b.inventoryKnown && now >= b.nextGiveUs)
				{
					const int half = GConfig.giveIntervalMs / 2;
					b.nextGiveUs = now + (GConfig.giveIntervalMs - half + rand() % (2 * half + 1)) * 1000LL;
					const int repeat = (rand() % 10 == 0) ? 2 : 1;
					for (int k = 0; k < repeat; ++k)
					{
						Protocol::CS_DEV_GIVE_PACKET give;
						if (rand() % 3 == 0) { give.set_item_id(0); give.set_count(1 + rand() % 100); }
						else { give.set_item_id(8001); give.set_count(1 + rand() % 5); }
						SendPacket(b, Protocol::CS_DEV_GIVE, give);
						GStat.giveSent.fetch_add(1);
					}
				}
				// 장비 교체(--equip-interval): 슬롯 무작위(1~7), 내 인벤토리에서 그 슬롯의 장비 하나(itemId 첫째 자리 = 슬롯) 또는 해제.
				// 장착은 item_db_id로 요청한다(A3.3). 고를 장비가 없으면 해제를 보낸다. --chaos면 5%는 없는 item_db_id를 보낸다(NOT_OWNED).
				// 보조손(7, 방패)은 무기가 검이 아니면 서버가 NEED_SWORD로 거부하고, 무기를 활·맨손으로 바꾸면 서버가 내린다(MC4.2f).
				if (canAct && GConfig.equipIntervalMs > 0 && b.inventoryKnown && now >= b.nextEquipUs)
				{
					const int half = GConfig.equipIntervalMs / 2;
					b.nextEquipUs = now + (GConfig.equipIntervalMs - half + rand() % (2 * half + 1)) * 1000LL;
					const int repeat = (rand() % 10 == 0) ? 2 : 1;
					for (int k = 0; k < repeat; ++k)
					{
						const int part = 1 + rand() % 7;
						long long dbId = 0;
						if (rand() % 3 != 2)
						{
							// 인벤토리(장착하지 않은 행) 중 이 슬롯의 장비를 하나 고른다(저수지 표본)
							int seen = 0;
							for (const auto& [id, item] : b.items)
							{
								if (item.equip != 0 || item.itemId / 1000 != part) continue;
								if (rand() % ++seen == 0) dbId = id;
							}
						}
						if (GConfig.chaos && rand() % 20 == 0) dbId = 900000000000LL + rand();
						Protocol::CS_CHANGE_EQUIP_PACKET eq;
						eq.set_part(static_cast<Protocol::EquipPart>(part));
						eq.set_item_db_id(dbId);
						SendPacket(b, Protocol::CS_CHANGE_EQUIP, eq);
						GStat.equipSent.fetch_add(1);
					}
				}
				// 상점(--shop-interval, L4): 시야 안의 상점 NPC에 구매(물약 1~3개)와 판매(인벤토리 행 하나에서 1개)를 번갈아 보낸다.
				// 거리 밖이면 TOO_FAR, 골드가 모자라면 NOT_ENOUGH_GOLD로 거부된다(--zone으로 NPC 앞에 세우고 --give-interval로 골드를 준다). --chaos면 5%는 없는 품목·없는 행.
				if (GConfig.shopIntervalMs > 0 && b.inventoryKnown && b.shopNpcId != 0 && now >= b.nextShopUs)
				{
					const int half = GConfig.shopIntervalMs / 2;
					b.nextShopUs = now + (GConfig.shopIntervalMs - half + rand() % (2 * half + 1)) * 1000LL;
					long long sellDbId = 0;
					if (!b.shopBuyNext)
					{
						int seen = 0;
						for (const auto& [id, item] : b.items)
						{
							if (item.equip != 0) continue;
							if (rand() % ++seen == 0) sellDbId = id;
						}
					}
					if (b.shopBuyNext || sellDbId == 0)
					{
						Protocol::CS_SHOP_BUY_PACKET buy;
						buy.set_npc_id(b.shopNpcId);
						buy.set_item_id((GConfig.chaos && rand() % 20 == 0) ? 6301 : 8001);
						buy.set_count(1 + rand() % 3);
						SendPacket(b, Protocol::CS_SHOP_BUY, buy);
						GStat.shopBuySent.fetch_add(1);
					}
					else
					{
						Protocol::CS_SHOP_SELL_PACKET sell;
						sell.set_npc_id(b.shopNpcId);
						sell.set_item_db_id((GConfig.chaos && rand() % 20 == 0) ? 900000000000LL + rand() : sellDbId);
						sell.set_count(1);
						SendPacket(b, Protocol::CS_SHOP_SELL, sell);
						GStat.shopSellSent.fetch_add(1);
					}
					b.shopBuyNext = !b.shopBuyNext;
				}
				// 칸 옮기기(--slot-interval): 인벤토리 행 하나를 무작위 칸으로. 대상에 다른 아이템이 있으면 서버가 맞바꾼다. --chaos면 5%는 범위 밖 칸(BAD_SLOT).
				if (GConfig.slotIntervalMs > 0 && b.inventoryKnown && now >= b.nextSlotMoveUs)
				{
					const int half = GConfig.slotIntervalMs / 2;
					b.nextSlotMoveUs = now + (GConfig.slotIntervalMs - half + rand() % (2 * half + 1)) * 1000LL;
					long long dbId = 0;
					int seen = 0;
					for (const auto& [id, item] : b.items)
					{
						if (item.equip != 0) continue;
						if (rand() % ++seen == 0) dbId = id;
					}
					if (dbId != 0)
					{
						Protocol::CS_MOVE_ITEM_PACKET mv;
						mv.set_item_db_id(dbId);
						mv.set_to_slot((GConfig.chaos && rand() % 20 == 0) ? b.maxSlots + rand() % 100 : rand() % b.maxSlots);
						SendPacket(b, Protocol::CS_MOVE_ITEM, mv);
						GStat.slotMoveSent.fetch_add(1);
					}
				}
				// 파티(--party-size, DG2): 묶음(번호 / N)의 첫 봇, 또는 지금 파티장인 봇이 묶음에서 아직 파티에 없는 봇을 2초마다 한 명씩 초대한다.
				// 탈퇴(--party-churn-sec)로 파티장이 바뀌면 묶음 첫 봇의 초대는 TARGET_IN_PARTY로 거부된다(거부 경로 부하 — 정상).
				if (GConfig.partySize > 1 && b.spawnKnown && now >= b.nextPartyInviteUs)
				{
					b.nextPartyInviteUs = now + 2000 * 1000LL + (rand() % 500) * 1000LL;
					const int base = b.index - b.index % GConfig.partySize;
					const bool leaderNow = b.partyLeaderId == b.objectId;
					const bool mayInvite = leaderNow || (b.partyLeaderId == 0 && b.index == base);
					if (mayInvite && static_cast<int>(b.partyMembers.size()) < GConfig.partySize)
					{
						for (int tries = 0; tries < GConfig.partySize; ++tries)
						{
							const int idx = base + (b.partyInviteCursor++ % GConfig.partySize);
							if (idx == b.index || idx >= GConfig.bots) continue;
							const int targetId = GBotObjectIds ? GBotObjectIds[idx].load() : 0;
							if (targetId <= 0 || b.partyMembers.count(targetId) > 0) continue;
							Protocol::CS_PARTY_INVITE_PACKET invite;
							invite.set_target_id(targetId);
							SendPacket(b, Protocol::CS_PARTY_INVITE, invite);
							GStat.partyInviteSent.fetch_add(1);
							break;
						}
					}
				}
				if (GConfig.partySize > 1 && GConfig.partyChurnSec > 0 && b.spawnKnown)
				{
					if (b.nextPartyLeaveUs == 0)
						b.nextPartyLeaveUs = now + (GConfig.partyChurnSec / 2 + rand() % (GConfig.partyChurnSec + 1)) * 1000000LL;
					else if (now >= b.nextPartyLeaveUs)
					{
						b.nextPartyLeaveUs = now + (GConfig.partyChurnSec / 2 + rand() % (GConfig.partyChurnSec + 1)) * 1000000LL;
						if (b.partyLeaderId != 0)
						{
							Protocol::CS_PARTY_LEAVE_PACKET leave;
							SendPacket(b, Protocol::CS_PARTY_LEAVE, leave);
							GStat.partyLeaveSent.fetch_add(1);
						}
					}
				}
				// 던전(--dungeon, DG3c): 사냥터에서 입구를 요청한다. 파티를 맺는 중(--party-size)이면 묶음이 다 모였거나 입장 뒤 15초가 지난 파티장만
				// (파티원은 파티장을 따라 들어가고, 뒤늦게 온 파티원은 인스턴스가 있을 때 스스로 요청해 합류한다 — NOT_LEADER 거부는 정상).
				if (GConfig.dungeon && b.spawnKnown && !b.dead && now >= b.nextDungeonEnterUs)
				{
					if (b.roomKind == static_cast<int>(Protocol::ROOM_KIND_DUNGEON))
					{
						if (b.dungeonExitAtUs != 0 && now >= b.dungeonExitAtUs && HasPortal(b, kDungeonExitPortal))
						{
							b.nextDungeonEnterUs = now + 2000 * 1000LL;
							Protocol::CS_PORTAL_PACKET portal;
							portal.set_portal_id(kDungeonExitPortal);
							SendPacket(b, Protocol::CS_PORTAL, portal);
							GStat.dungeonExitSent.fetch_add(1);
						}
					}
					else if (HasPortal(b, kDungeonEntrancePortal))
					{
						b.nextDungeonEnterUs = now + 2000 * 1000LL + (rand() % 1000) * 1000LL;
						bool request = true;
						if (GConfig.partySize > 1)
						{
							const bool full = static_cast<int>(b.partyMembers.size()) >= GConfig.partySize;
							const bool waited = now - b.loginSentUs > 15 * 1000000LL;
							if (b.partyLeaderId == 0) request = waited;                       // 파티가 안 맺어지면 혼자 들어간다
							else if (b.partyLeaderId == b.objectId) request = full || waited;
							else request = waited;                                            // 파티원: 파티장이 먼저 들어간 인스턴스에 합류
						}
						if (request)
						{
							Protocol::CS_PORTAL_PACKET portal;
							portal.set_portal_id(kDungeonEntrancePortal);
							SendPacket(b, Protocol::CS_PORTAL, portal);
							GStat.dungeonEnterSent.fetch_add(1);
						}
					}
				}
				// 포털(--portal-interval, L5): 가장 가까운 포털에 요청한다. 반경 밖·재사용 대기·시전 중이면 서버가 응답 없이 버린다.
				// --chaos면 5%는 없는 포털 id.
				if (GConfig.portalIntervalMs > 0 && !b.portals.empty() && now >= b.nextPortalUs)
				{
					const int half = GConfig.portalIntervalMs / 2;
					b.nextPortalUs = now + (GConfig.portalIntervalMs - half + rand() % (2 * half + 1)) * 1000LL;
					int portalId = 0;
					float bestSq = FLT_MAX;
					for (const array<float, 3>& p : b.portals)
					{
						const float dx = p[0] - b.x, dy = p[1] - b.y;
						if (dx * dx + dy * dy < bestSq) { bestSq = dx * dx + dy * dy; portalId = static_cast<int>(p[2]); }
					}
					if (GConfig.chaos && rand() % 20 == 0) portalId = 9000 + rand() % 100;
					Protocol::CS_PORTAL_PACKET portal;
					portal.set_portal_id(portalId);
					SendPacket(b, Protocol::CS_PORTAL, portal);
					GStat.portalSent.fetch_add(1);
				}
				// ---- A5.2 프로필 행동
				// 채팅(Chat): 짧은 문장 하나. 서버 최소 간격(500ms)보다 훨씬 길게
				if (b.profile == BotProfile::Chat && canAct && now >= b.nextChatUs)
				{
					static const char* const kChat[] = { "hi", "anyone party?", "selling potions", "lfg dungeon", "where is the auction npc", "gg", "brb", "nice drop!", "wts plate armor", "ok" };
					const int half = GConfig.chatIntervalMs / 2;
					const bool first = b.nextChatUs == 0;
					b.nextChatUs = now + (GConfig.chatIntervalMs - half + rand() % (2 * half + 1)) * 1000LL;
					if (!first)
					{
						Protocol::CS_CHAT_PACKET chat;
						chat.set_message((GConfig.chaos && rand() % 20 == 0) ? string(300, 'x') : kChat[rand() % (sizeof(kChat) / sizeof(kChat[0]))]);
						SendPacket(b, Protocol::CS_CHAT, chat);
						GStat.chatSent.fetch_add(1);
					}
				}
				// 줍기(Loot): 드랍이 300cm 안이면 줍기 요청(응답이 올 때까지 다음 아이템 요청 없음, 3초 넘으면 포기), 인벤토리가 거의 차면 버리기
				if (b.profile == BotProfile::Loot && b.inventoryKnown)
				{
					if (b.itemOpPending && now - b.itemOpSentUs > 3000 * 1000LL)
					{
						b.itemOpPending = false;
						if (b.pickupTarget != 0) b.skipDrops.insert(b.pickupTarget);
						b.pickupTarget = 0;
					}
					if (!b.itemOpPending && canAct && !b.dead && looting && now >= b.nextPickupUs)
					{
						const array<float, 3>& d = b.drops[b.pickupTarget];
						const float dx = d[0] - b.x, dy = d[1] - b.y;
						if (dx * dx + dy * dy <= 280.f * 280.f)
						{
							Protocol::CS_PICKUP_ITEM_PACKET pick;
							pick.set_object_id((GConfig.chaos && rand() % 20 == 0) ? 99999999 : b.pickupTarget);
							SendPacket(b, Protocol::CS_PICKUP_ITEM, pick);
							GStat.pickupSent.fetch_add(1);
							b.itemOpPending = true;
							b.itemOpKind = 1;
							b.itemOpSentUs = now;
							b.nextPickupUs = now + 300 * 1000LL;
						}
					}
					int bagRows = 0;
					for (const auto& [id, item] : b.items) if (item.equip == 0) ++bagRows;
					if (!b.itemOpPending && !b.dead && bagRows >= b.maxSlots - 10 && now >= b.nextDropItemUs)
					{
						long long dbId = 0;
						int seen = 0;
						for (const auto& [id, item] : b.items)
							if (item.equip == 0 && rand() % ++seen == 0) dbId = id;
						if (dbId != 0)
						{
							Protocol::CS_DROP_ITEM_PACKET drop;
							drop.set_item_db_id(dbId);
							SendPacket(b, Protocol::CS_DROP_ITEM, drop);
							GStat.dropItemSent.fetch_add(1);
							b.itemOpPending = true;
							b.itemOpKind = 2;
							b.itemOpSentUs = now;
							b.pickupTarget = 0;
							b.nextDropItemUs = now + 2000 * 1000LL;
						}
					}
				}
				// 거래(Trade): 마을에서 시야 안의 다른 거래 봇(나보다 오브젝트 id가 큰 쪽 — 서로 동시에 요청하지 않게)을 골라 다가가 요청한다.
				// 진행(제시·잠금·확정)은 SC_TRADE_STATE에 반응해 보낸다. 40초가 넘으면 취소한다(서버 시간 초과보다 먼저)
				if (b.profile == BotProfile::Trade && canAct && !b.dead && b.inventoryKnown && InTownRoom(b))
				{
					if (b.tradeActive && now - b.tradeStartUs > 40 * 1000000LL)
					{
						SendPacket(b, Protocol::CS_TRADE_CANCEL, Protocol::CS_TRADE_CANCEL_PACKET());
						GStat.tradeCancelSent.fetch_add(1);
						b.tradeActive = false;
						b.nextTradeUs = now + 5000 * 1000LL;
					}
					else if (!b.tradeActive && now >= b.nextTradeUs)
					{
						auto pit = b.tradeTargetId != 0 ? b.players.find(b.tradeTargetId) : b.players.end();
						if (pit == b.players.end() || now - b.tradeStartUs > 15 * 1000000LL)
						{
							b.tradeTargetId = 0;
							float best = FLT_MAX;
							{
								lock_guard<mutex> lock(GTradeIdsLock);
								for (const auto& [id, pos] : b.players)
								{
									if (id <= b.objectId || GTradeObjectIds.count(id) == 0) continue;
									const float dx = pos.first - b.x, dy = pos.second - b.y;
									if (dx * dx + dy * dy < best) { best = dx * dx + dy * dy; b.tradeTargetId = id; }
								}
							}
							b.tradeStartUs = now;
							if (b.tradeTargetId == 0) b.nextTradeUs = now + 2000 * 1000LL;
						}
						else
						{
							const float dx = pit->second.first - b.x, dy = pit->second.second - b.y;
							if (dx * dx + dy * dy <= 350.f * 350.f)
							{
								Protocol::CS_TRADE_REQUEST_PACKET req;
								req.set_target_id((GConfig.chaos && rand() % 20 == 0) ? b.objectId : b.tradeTargetId);
								SendPacket(b, Protocol::CS_TRADE_REQUEST, req);
								GStat.tradeRequestSent.fetch_add(1);
								b.tradeActive = true;
								b.tradeStartUs = now;
								b.tradeOfferSent = b.tradeLockSent = b.tradeConfirmSent = false;
								b.tradeRetries = 0;
							}
						}
					}
				}
				// 경매(Auction): 경매 NPC 앞에서 검색·등록·구매·취소·내 목록·수령을 섞는다. 응답을 받은 뒤 다음 요청(5초 넘으면 포기)
				if (b.profile == BotProfile::Auction && canAct && !b.dead && b.inventoryKnown && InTownRoom(b) && b.auctionNpcId != 0)
				{
					if (b.auctionPending && now - b.auctionSentUs > 5000 * 1000LL) b.auctionPending = false;
					const float dx = b.auctionNpcX - b.x, dy = b.auctionNpcY - b.y;
					if (!b.auctionPending && dx * dx + dy * dy <= 380.f * 380.f && now >= b.nextAuctionUs)
					{
						const int half = GConfig.auctionIntervalMs / 2;
						b.nextAuctionUs = now + (GConfig.auctionIntervalMs - half + rand() % (2 * half + 1)) * 1000LL;
						long long regDbId = 0, regCount = 0;
						{
							int seen = 0;
							for (const auto& [id, item] : b.items)
								if (item.equip == 0 && item.count > 0 && rand() % ++seen == 0) { regDbId = id; regCount = item.count; }
						}
						const bool canSearch = now >= b.nextAuctionSearchUs;
						int kind = 4;
						const int r = rand() % 100;
						if (!b.myKnown) kind = 4;
						else if (!b.mails.empty() && r < 25) kind = 5;
						else if (r < 50 && canSearch) kind = 0;
						else if (r < 70 && regDbId != 0 && b.myListings.size() < 10) kind = 1;
						else if (r < 92 && !b.otherListings.empty()) kind = 2;
						else if (r < 96 && !b.myListings.empty()) kind = 3;
						else if (canSearch) kind = 0;
						const bool bad = GConfig.chaos && rand() % 20 == 0;
						switch (kind)
						{
						case 0:
						{
							Protocol::CS_AUCTION_SEARCH_PACKET pkt;
							pkt.set_query(Protocol::AUCTION_QUERY_SEARCH);
							pkt.set_slot(bad ? 9 : 0);
							pkt.set_sort(static_cast<Protocol::AuctionSort>(rand() % 3));
							pkt.set_desc(rand() % 2 == 0);
							SendPacket(b, Protocol::CS_AUCTION_SEARCH, pkt);
							b.nextAuctionSearchUs = now + 5500 * 1000LL;   // 서버 AuctionSearchIntervalMs 5초
							break;
						}
						case 1:
						{
							Protocol::CS_AUCTION_REGISTER_PACKET pkt;
							pkt.set_npc_id(b.auctionNpcId);
							pkt.set_item_db_id(regDbId);
							pkt.set_count(regCount > 1 ? 1 + rand() % static_cast<int>(min<long long>(regCount, 5)) : 1);
							pkt.set_unit_price(bad ? 0 : 10 + rand() % 191);
							SendPacket(b, Protocol::CS_AUCTION_REGISTER, pkt);
							break;
						}
						case 2:
						{
							const Bot::Listing& l = b.otherListings[rand() % b.otherListings.size()];
							Protocol::CS_AUCTION_BUY_PACKET pkt;
							pkt.set_listing_id(bad ? 999999999 : l.id);
							long long cnt = 1 + rand() % static_cast<int>(min<long long>(l.count, 3));
							if (l.unitPrice > 0) cnt = max(1LL, min(cnt, b.gold / l.unitPrice));
							pkt.set_count(cnt);
							SendPacket(b, Protocol::CS_AUCTION_BUY, pkt);
							break;
						}
						case 3:
						{
							Protocol::CS_AUCTION_CANCEL_PACKET pkt;
							pkt.set_npc_id(b.auctionNpcId);
							pkt.set_listing_id(b.myListings[rand() % b.myListings.size()]);
							SendPacket(b, Protocol::CS_AUCTION_CANCEL, pkt);
							break;
						}
						case 5:
						{
							Protocol::CS_MAIL_CLAIM_PACKET pkt;
							pkt.set_npc_id(b.auctionNpcId);
							pkt.set_mail_id(b.mails.front());
							SendPacket(b, Protocol::CS_MAIL_CLAIM, pkt);
							break;
						}
						default:
							SendPacket(b, Protocol::CS_AUCTION_MY, Protocol::CS_AUCTION_MY_PACKET());
							break;
						}
						b.auctionPending = true;
						b.auctionPendingKind = kind;
						b.auctionSentUs = now;
						GStat.auctionSent[kind].fetch_add(1);
					}
				}
				if (now >= b.nextPingUs)
				{
					b.nextPingUs = now + GConfig.pingIntervalMs * 1000LL;
					Protocol::CS_PING_PACKET ping;
					ping.set_client_time(now);
					SendPacket(b, Protocol::CS_PING, ping);
					// chaos: 살아 있을 때·대기 시간 전에도 가끔 부활을 요청한다(서버가 조용히 버려야 한다)
					if (GConfig.chaos && rand() % 4 == 0)
					{
						Protocol::CS_REVIVE_PACKET revive;
						SendPacket(b, Protocol::CS_REVIVE, revive);
						GStat.reviveSent.fetch_add(1);
					}
				}
			}

			if (b.sock != INVALID_SOCKET && b.state != BotState::Closed)
			{
				WSAPOLLFD fd{};
				fd.fd = b.sock;
				fd.events = POLLRDNORM;
				if (b.state == BotState::Connecting || !b.pending.empty()) fd.events |= POLLWRNORM;
				fds.push_back(fd);
				owners.push_back(&b);
			}
		}

		if (fds.empty())
		{
			this_thread::sleep_for(chrono::milliseconds(5));
			continue;
		}

		int ready = WSAPoll(fds.data(), static_cast<ULONG>(fds.size()), 5);
		if (ready <= 0) continue;

		for (size_t i = 0; i < fds.size(); ++i)
		{
			Bot& b = *owners[i];
			const short ev = fds[i].revents;
			if (ev == 0) continue;

			if (b.state == BotState::Connecting)
			{
				int err = 0; int len = sizeof(err);
				getsockopt(b.sock, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&err), &len);
				if (err != 0 || (ev & (POLLERR | POLLHUP | POLLNVAL)))
				{
					GStat.connectFail.fetch_add(1);
					closesocket(b.sock); b.sock = INVALID_SOCKET; b.state = BotState::Closed;
					ScheduleRetry(b);
				}
				else if (ev & POLLWRNORM)
				{
					OnConnected(b);
				}
				continue;
			}

			if (ev & POLLRDNORM)
			{
				bool closed = false;
				while (true)
				{
					int n = recv(b.sock, reinterpret_cast<char*>(tmp.data()), static_cast<int>(tmp.size()), 0);
					if (n > 0)
					{
						GStat.bytesRecv.fetch_add(n);
						const int epoch = GStat.windowEpoch.load(std::memory_order_relaxed);
						if (b.windowEpoch != epoch) { b.windowEpoch = epoch; b.windowBytes = 0; }
						b.windowBytes += n;
						long long prevMax = GStat.maxBotBytesWindow.load(std::memory_order_relaxed);
						while (b.windowBytes > prevMax && !GStat.maxBotBytesWindow.compare_exchange_weak(prevMax, b.windowBytes)) {}
						b.rbuf.insert(b.rbuf.end(), tmp.begin(), tmp.begin() + n);
						if (n < static_cast<int>(tmp.size())) break;
					}
					else if (n == 0) { closed = true; break; }
					else
					{
						if (WSAGetLastError() != WSAEWOULDBLOCK) closed = true;
						break;
					}
				}
				if (!ProcessRecvBuffer(b, samples)) closed = true;
				if (closed) { CloseBot(b, true); continue; }
			}

			if ((ev & POLLWRNORM) && !FlushPending(b)) { CloseBot(b, true); continue; }
			if (ev & (POLLERR | POLLHUP | POLLNVAL)) CloseBot(b, true);
		}
	}

	for (Bot& b : bots)
		if (b.sock != INVALID_SOCKET) CloseBot(b, false);
}

// ---------------------------------------------------------------- 시작/종료
void BotEngine::Usage(const char* program)
{
	printf("%s --bots N --ramp-rate R --duration S [--host H] [--port P] [--threads T]\n"
		"         [--move-interval ms] [--ping-interval ms] [--profile idle|move|turn|chat|hunt|loot|trade|auction] [--mix idle=25,move=15,...] [--body male|female] [--chaos] [--equip-interval ms] [--give-gear] [--give-interval ms] [--slot-interval ms] [--shop-interval ms] [--portal-interval ms] [--party-size N] [--party-churn-sec S] [--dungeon] [--dungeon-stay-sec S] [--dungeon-map logic.json] [--aim] [--map logic.json] [--nav-dir dir] [--town-wander] [--out file.csv] [--server-proc name.exe]\n"
		"         [--zone x,y,r]   (hotspot: bots walk to (x,y) and stay within radius r cm)\n"
		"         [--login dummy|account] [--account-start N] [--login-retries N] [--login-timeout ms] [--data-dir dir] [--chat-interval ms] [--trade-interval ms] [--auction-interval ms]\n"
		"         (account = bot_<index+start>/1234 normal login, accounts made by PerfData/2026-11-24_A5.2/make_bot_accounts.py)\n",
		program);
}

static int ProfileFromName(const string& name)
{
	for (int k = 0; k < static_cast<int>(BotProfile::Count); ++k)
		if (name == kProfileNames[k]) return k;
	return -1;
}

// --mix "idle=25,move=15,..." → GConfig.mix(합 100). 잘못되면 false
static bool ParseMix(const string& text)
{
	int mix[static_cast<int>(BotProfile::Count)] = {};
	int sum = 0;
	size_t pos = 0;
	while (pos < text.size())
	{
		size_t comma = text.find(',', pos);
		if (comma == string::npos) comma = text.size();
		const string item = text.substr(pos, comma - pos);
		const size_t eq = item.find('=');
		if (eq == string::npos) return false;
		const int k = ProfileFromName(item.substr(0, eq));
		const int pct = atoi(item.substr(eq + 1).c_str());
		if (k < 0 || pct < 0 || pct > 100) return false;
		mix[k] += pct;
		sum += pct;
		pos = comma + 1;
	}
	if (sum != 100) return false;
	memcpy(GConfig.mix, mix, sizeof(mix));
	GConfig.mixText = text;
	return true;
}

bool BotEngine::ParseArgs(int argc, char** argv)
{
	for (int i = 1; i < argc; ++i)
	{
		string a = argv[i];
		auto next = [&](const char* what) -> const char* {
			if (i + 1 >= argc) { printf("missing value for %s\n", what); return nullptr; }
			return argv[++i];
		};
		const char* v = nullptr;
		if (a == "--help" || a == "-h") { Usage(argv[0]); return false; }
		else if (a == "--host") { if (!(v = next("--host"))) return false; GConfig.host = v; }
		else if (a == "--port") { if (!(v = next("--port"))) return false; GConfig.port = atoi(v); }
		else if (a == "--bots") { if (!(v = next("--bots"))) return false; GConfig.bots = atoi(v); }
		else if (a == "--ramp-rate") { if (!(v = next("--ramp-rate"))) return false; GConfig.rampRate = atoi(v); }
		else if (a == "--duration") { if (!(v = next("--duration"))) return false; GConfig.durationSec = atoi(v); }
		else if (a == "--threads") { if (!(v = next("--threads"))) return false; GConfig.threads = atoi(v); }
		else if (a == "--move-interval") { if (!(v = next("--move-interval"))) return false; GConfig.moveIntervalMs = atoi(v); }
		else if (a == "--ping-interval") { if (!(v = next("--ping-interval"))) return false; GConfig.pingIntervalMs = atoi(v); }
		else if (a == "--profile")
		{
			if (!(v = next("--profile"))) return false;
			const int k = ProfileFromName(v);
			if (k < 0) { printf("unknown profile: %s\n", v); return false; }
			memset(GConfig.mix, 0, sizeof(GConfig.mix));
			GConfig.mix[k] = 100;
			GConfig.mixText = v;
		}
		else if (a == "--mix") { if (!(v = next("--mix"))) return false; if (!ParseMix(v)) { printf("--mix must be profile=pct,... summing to 100 (profiles: idle move turn chat hunt loot trade auction)\n"); return false; } }
		else if (a == "--login")
		{
			if (!(v = next("--login"))) return false;
			if (string(v) == "account") GConfig.accountLogin = true;
			else if (string(v) == "dummy") GConfig.accountLogin = false;
			else { printf("--login must be dummy or account\n"); return false; }
		}
		else if (a == "--account-start") { if (!(v = next("--account-start"))) return false; GConfig.accountStart = atoi(v); }
		else if (a == "--login-retries") { if (!(v = next("--login-retries"))) return false; GConfig.loginRetries = atoi(v); }
		else if (a == "--login-timeout") { if (!(v = next("--login-timeout"))) return false; GConfig.loginTimeoutMs = max(1000, atoi(v)); }
		else if (a == "--data-dir") { if (!(v = next("--data-dir"))) return false; GConfig.dataDir = v; }
		else if (a == "--chat-interval") { if (!(v = next("--chat-interval"))) return false; GConfig.chatIntervalMs = max(600, atoi(v)); }
		else if (a == "--trade-interval") { if (!(v = next("--trade-interval"))) return false; GConfig.tradeIntervalMs = max(1000, atoi(v)); }
		else if (a == "--auction-interval") { if (!(v = next("--auction-interval"))) return false; GConfig.auctionIntervalMs = max(1200, atoi(v)); }
		else if (a == "--body") { if (!(v = next("--body"))) return false; GConfig.body = v; if (GConfig.body != "male" && GConfig.body != "female") { printf("--body must be male or female\n"); return false; } }
		else if (a == "--chaos") { GConfig.chaos = true; }
		else if (a == "--turn-send") { GConfig.turnSend = true; }
		else if (a == "--turn-interval") { if (!(v = next("--turn-interval"))) return false; GConfig.turnIntervalMs = max(200, atoi(v)); }
		else if (a == "--aim") GConfig.aim = true;
		else if (a == "--equip-interval") { if (!(v = next("--equip-interval"))) return false; GConfig.equipIntervalMs = atoi(v); }
		else if (a == "--give-gear") GConfig.giveGear = true;
		else if (a == "--slot-interval") { if (!(v = next("--slot-interval"))) return false; GConfig.slotIntervalMs = atoi(v); }
		else if (a == "--shop-interval") { if (!(v = next("--shop-interval"))) return false; GConfig.shopIntervalMs = atoi(v); }
		else if (a == "--portal-interval") { if (!(v = next("--portal-interval"))) return false; GConfig.portalIntervalMs = atoi(v); }
		else if (a == "--dungeon") GConfig.dungeon = true;
		else if (a == "--dungeon-stay-sec") { if (!(v = next("--dungeon-stay-sec"))) return false; GConfig.dungeonStaySec = atoi(v); }
		else if (a == "--dungeon-map") { if (!(v = next("--dungeon-map"))) return false; GConfig.dungeonMapLogic = v; }
		else if (a == "--party-size") { if (!(v = next("--party-size"))) return false; GConfig.partySize = atoi(v); }
		else if (a == "--party-churn-sec") { if (!(v = next("--party-churn-sec"))) return false; GConfig.partyChurnSec = atoi(v); }
		else if (a == "--give-interval") { if (!(v = next("--give-interval"))) return false; GConfig.giveIntervalMs = atoi(v); }
		else if (a == "--nav-dir") { if (!(v = next("--nav-dir"))) return false; GConfig.navDir = v; }
		else if (a == "--town-wander") GConfig.townWander = true;
		else if (a == "--map") { if (!(v = next("--map"))) return false; GConfig.mapLogic = v; }
		else if (a == "--out") { if (!(v = next("--out"))) return false; GConfig.outFile = v; }
		else if (a == "--server-proc") { if (!(v = next("--server-proc"))) return false; GConfig.serverProc = v; }
		else if (a == "--zone")
		{
			if (!(v = next("--zone"))) return false;
			if (sscanf_s(v, "%f,%f,%f", &GConfig.zoneX, &GConfig.zoneY, &GConfig.zoneR) != 3 || GConfig.zoneR <= 0.f)
			{
				printf("--zone must be x,y,r (cm, r > 0)\n");
				return false;
			}
			GConfig.zone = true;
		}
		else { printf("unknown option: %s\n", a.c_str()); Usage(argv[0]); return false; }
	}
	if (GConfig.bots < 1 || GConfig.rampRate < 1 || GConfig.durationSec < 1 || GConfig.threads < 1 ||
		GConfig.moveIntervalMs < 1 || GConfig.pingIntervalMs < 1)
	{
		printf("invalid option value\n");
		return false;
	}
	if (GConfig.threads > GConfig.bots) GConfig.threads = GConfig.bots;
	// 이 실행에 있는 프로필(시작·요약 출력과 맵 로드 판단용). 봇마다의 행동은 Bot::profile
	const int* m = GConfig.mix;
	GConfig.hunt = m[static_cast<int>(BotProfile::Hunt)] > 0 || m[static_cast<int>(BotProfile::Loot)] > 0;
	GConfig.turn = m[static_cast<int>(BotProfile::Turn)] > 0;
	GConfig.move = m[static_cast<int>(BotProfile::Idle)] < 100;
	return true;
}

// 맵 로직 JSON에서 몬스터 스폰 위치를 읽는다.
static void LoadMonsterSpawns()
{
	const char* candidates[] = { "../../MapResource/Export/Continent01/Logic/Continent01_Logic.json", "../MapResource/Export/Continent01/Logic/Continent01_Logic.json" };
	string path = GConfig.mapLogic;
	if (path.empty())
		for (const char* cand : candidates) { ifstream probe(cand); if (probe.is_open()) { path = cand; break; } }

	ifstream file(path);
	if (!path.empty() && file.is_open())
	{
		try
		{
			nlohmann::json root;
			file >> root;
			for (const auto& marker : root.at("markers"))
			{
				const string type = marker.at("type").get<string>();
				if (type == "MonsterSpawn")
					GMonsterSpawns.push_back({ marker.at("position").at("x").get<float>(), marker.at("position").at("y").get<float>() });
				else if (type == "SafeZone" && marker.value("area", string()) == "town")
					GTownZones.push_back({ marker.at("position").at("x").get<float>(), marker.at("position").at("y").get<float>(), marker.value("radius", 0.f) });
				else if (type == "Npc" && BotData::AuctionNpcTemplate() != 0 && marker.value("npcTemplateId", 0) == BotData::AuctionNpcTemplate() && !GAuctionNpcKnown)
				{
					GAuctionNpcKnown = true;
					GAuctionNpcX = marker.at("position").at("x").get<float>();
					GAuctionNpcY = marker.at("position").at("y").get<float>();
				}
			}
			if (GAuctionNpcKnown) printf("[bot] auction npc marker: (%.0f, %.0f)\n", GAuctionNpcX, GAuctionNpcY);
			printf("[bot] monster spawns loaded: %zu, town zones: %zu (%s)\n", GMonsterSpawns.size(), GTownZones.size(), path.c_str());
		}
		catch (const exception& e) { printf("[bot] map json error: %s\n", e.what()); }
	}
	else printf("[bot] map logic json not found; hunt bots will wander randomly (use --map <path>)\n");

	// 던전 레벨의 스폰(--dungeon-map)
	if (!GConfig.dungeonMapLogic.empty())
	{
		ifstream dungeonFile(GConfig.dungeonMapLogic);
		if (dungeonFile.is_open())
		{
			try
			{
				nlohmann::json root;
				dungeonFile >> root;
				for (const auto& marker : root.at("markers"))
					if (marker.at("type").get<string>() == "MonsterSpawn")
						GDungeonSpawns.push_back({ marker.at("position").at("x").get<float>(), marker.at("position").at("y").get<float>() });
				printf("[bot] dungeon spawns loaded: %zu (%s)\n", GDungeonSpawns.size(), GConfig.dungeonMapLogic.c_str());
			}
			catch (const exception& e) { printf("[bot] dungeon map json error: %s\n", e.what()); }
		}
		else printf("[bot] dungeon map logic json not found: %s\n", GConfig.dungeonMapLogic.c_str());
	}
}

void BotEngine::Start()
{
	WSADATA wsa;
	WSAStartup(MAKEWORD(2, 2), &wsa);
	srand(static_cast<unsigned>(time(nullptr)));

	BotData::Load(GConfig.dataDir);
	const bool townProfiles = GConfig.mix[static_cast<int>(BotProfile::Trade)] > 0 || GConfig.mix[static_cast<int>(BotProfile::Auction)] > 0;
	if (GConfig.hunt || GConfig.trackWorld || GConfig.townWander || townProfiles)
		LoadMonsterSpawns();

	// 봇 번호 → 프로필(A5.2, D-298): 번호 순서대로 비율에 가장 모자란 프로필을 준다(어느 시점에 세어도 비율이 맞고, 램프 중에도 고르게 섞임)
	GBotProfiles.assign(max(1, GConfig.bots), BotProfile::Move);
	{
		int given[static_cast<int>(BotProfile::Count)] = {};
		for (int i = 0; i < GConfig.bots; ++i)
		{
			int pick = -1;
			double bestLack = -1e18;
			for (int k = 0; k < static_cast<int>(BotProfile::Count); ++k)
			{
				if (GConfig.mix[k] <= 0) continue;
				const double lack = (i + 1) * GConfig.mix[k] / 100.0 - given[k];
				if (lack > bestLack) { bestLack = lack; pick = k; }
			}
			if (pick < 0) pick = static_cast<int>(BotProfile::Move);
			++given[pick];
			GBotProfiles[i] = static_cast<BotProfile>(pick);
			GStat.profileBots[pick].fetch_add(1);
		}
	}
	BotNav::SetDir(GConfig.navDir);

	GBotObjectIds = make_unique<atomic<int>[]>(max(1, GConfig.bots));
	for (int i = 0; i < GConfig.bots; ++i) GBotObjectIds[i].store(0);
	for (int i = 0; i < GConfig.threads; ++i) GSamples.push_back(make_unique<SampleBuffer>());
	for (int i = 0; i < GConfig.threads; ++i) GSnapshots.push_back(make_unique<SnapshotBuffer>());

	GStartUs = NowUs();
	GRunning.store(true);
	for (int i = 0; i < GConfig.threads; ++i)
		GWorkers.emplace_back(WorkerMain, i, GConfig.threads);
}

double BotEngine::ElapsedSec()
{
	return (NowUs() - GStartUs) / 1e6;
}

void BotEngine::UpdateRamp()
{
	GAllowed.store(static_cast<int>(min<double>(GConfig.bots, ElapsedSec() * GConfig.rampRate)) + 1);
}

void BotEngine::TakeSamples(vector<uint32_t>& pingUs, vector<uint32_t>& loginUs)
{
	for (auto& sb : GSamples)
	{
		lock_guard<mutex> lock(sb->m);
		pingUs.insert(pingUs.end(), sb->pingUs.begin(), sb->pingUs.end());
		loginUs.insert(loginUs.end(), sb->loginUs.begin(), sb->loginUs.end());
		sb->pingUs.clear(); sb->loginUs.clear();
	}
}

void BotEngine::TakeEnterSamples(vector<uint32_t>& enterUs)
{
	for (auto& sb : GSamples)
	{
		lock_guard<mutex> lock(sb->m);
		enterUs.insert(enterUs.end(), sb->enterUs.begin(), sb->enterUs.end());
		sb->enterUs.clear();
	}
}

void BotEngine::GetSnapshot(vector<BotView>& bots, vector<MonsterView>& monsters)
{
	bots.clear();
	monsters.clear();
	unordered_set<int> seen;
	for (auto& sb : GSnapshots)
	{
		lock_guard<mutex> lock(sb->m);
		bots.insert(bots.end(), sb->bots.begin(), sb->bots.end());
		for (const MonsterView& m : sb->monsters)
			if (seen.insert(m.id).second) monsters.push_back(m);
	}
}

void BotEngine::Stop()
{
	GShuttingDown.store(true);
	GRunning.store(false);
	for (auto& t : GWorkers) t.join();
	GWorkers.clear();
	WSACleanup();
}

// ---------------------------------------------------------------- 서버 프로세스 샘플링
static DWORD FindPid(const string& name)
{
	HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
	if (snap == INVALID_HANDLE_VALUE) return 0;
	PROCESSENTRY32 pe{ sizeof(pe) };
	DWORD pid = 0;
	wstring wname(name.begin(), name.end());
	if (Process32First(snap, &pe))
		do { if (_wcsicmp(pe.szExeFile, wname.c_str()) == 0) { pid = pe.th32ProcessID; break; } } while (Process32Next(snap, &pe));
	CloseHandle(snap);
	return pid;
}

ServerProcSampler::~ServerProcSampler()
{
	if (_handle) CloseHandle(_handle);
}

bool ServerProcSampler::Open(const string& exeName)
{
	DWORD pid = FindPid(exeName);
	if (pid) _handle = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
	if (_handle) { _lastCpu100ns = Cpu(); _lastWallUs = NowUs(); }
	return _handle != nullptr;
}

unsigned long long ServerProcSampler::Cpu()
{
	FILETIME c, e, k, u;
	if (!GetProcessTimes(_handle, &c, &e, &k, &u)) return 0;
	return ((static_cast<ULONGLONG>(k.dwHighDateTime) << 32) | k.dwLowDateTime) +
		((static_cast<ULONGLONG>(u.dwHighDateTime) << 32) | u.dwLowDateTime);
}

void ServerProcSampler::Sample(double& cpuPct, double& memMb)
{
	cpuPct = 0; memMb = 0;
	if (!_handle) return;
	ULONGLONG cpu = Cpu();
	long long wall = NowUs();
	if (wall > _lastWallUs) cpuPct = (cpu - _lastCpu100ns) / 10.0 / (wall - _lastWallUs) * 100.0;
	_lastCpu100ns = cpu; _lastWallUs = wall;

	PROCESS_MEMORY_COUNTERS pmc{};
	if (GetProcessMemoryInfo(_handle, &pmc, sizeof(pmc))) memMb = pmc.WorkingSetSize / (1024.0 * 1024.0);
}
