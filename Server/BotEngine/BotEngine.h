#pragma once
// LoadTest(콘솔)와 DummyClient(시각화)가 함께 쓰는 봇 엔진.
// 봇 N명을 램프업으로 접속시켜 로그인 -> 입장 -> (이동/사냥/핑)을 반복하고, 지연·처리량을 집계한다.
// 네트워크는 논블로킹 소켓 + WSAPoll 워커 스레드(봇은 클라이언트 측이라 ServerCore를 쓰지 않는다, DECISIONS 2026-10-30).
#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

// ---------------------------------------------------------------- 프로필 (A5.2, D-298)
// 봇마다 하나. --profile X는 모두 같게, --mix idle=25,move=15,...는 번호 순서대로 비율에 맞게 섞는다.
// Idle = 잠수(제자리, 30~90초마다 몇 m), Move = 무작위 이동, Turn = 90° 꺾기(MC4.1b), Chat = 이동 + 채팅, Hunt = 사냥,
// Loot = 사냥 + 드랍 줍기·버리기, Trade = 마을에서 다른 거래 봇과 1:1 거래, Auction = 경매 NPC 앞에서 검색·등록·구매·수령·취소
enum class BotProfile : int { Idle = 0, Move, Turn, Chat, Hunt, Loot, Trade, Auction, Count };

// ---------------------------------------------------------------- 설정
struct BotConfig
{
	std::string host = "127.0.0.1";
	int port = 8888;
	int bots = 100;
	int rampRate = 50;        // 초당 접속 시도 수
	int durationSec = 60;     // 램프 시작부터 종료까지의 전체 실행 시간
	int threads = 4;
	int moveIntervalMs = 1000;
	int pingIntervalMs = 1000;
	// 아래 셋은 "이 실행에 그 프로필이 하나라도 있는가"(ParseArgs가 --profile·--mix로 채움). 봇마다의 행동은 Bot::profile로 정한다
	bool move = true;          // 움직이는 프로필(Idle 외)이 있다
	std::string body = "male";     // 체형. male: dummy 계정 로그인, female: fem_* 계정 가입 후 로그인 (무기는 MC3.2 전까지 없음 = 맨손)
	bool chaos = false;         // 일부 공격 요청을 일부러 잘못된 값으로 보낸다(오류 테스트)
	bool hunt = false;         // 사냥하는 프로필(Hunt·Loot)이 있다
	bool turn = false;         // turn 프로필(MC4.1b): 직선으로 걷다 turnIntervalMs마다 90° 꺾음 — 원격 이동 표시 측정용(--move-interval 200과 함께)
	int turnIntervalMs = 1500;
	bool turnSend = false;     // --turn-send: 꺾는 순간 모서리 위치를 바로 보내고 새 방향은 60ms 뒤부터(클라 '방향 바뀌면 즉시 송신' 모사, MC4.1b)
	bool aim = false;          // --aim (MC4.2d-1): 활을 장착하고, 1번 스킬은 조준을 켜고 300~800ms 느리게 움직인 뒤 쏜다(10%는 취소). hunt와 함께
	int equipIntervalMs = 0;   // >0이면 이 간격(±50%)마다 무작위 장비 교체 요청(내 인벤토리의 item_db_id 기준, A3.3), 10%는 바로 한 번 더 보냄(빈도 제한 시험)
	bool giveGear = false;     // --give-gear: 입장 후 판금 세트·활을 개발 지급 명령으로 받는다(서버 [Debug] DevCommands=true, 개별 계정 필요). 장착 시험 준비
	int slotIntervalMs = 0;    // >0이면 이 간격(±50%)마다 인벤토리 아이템 하나를 무작위 칸(0 ~ 서버가 알려 준 칸 수 - 1)으로 옮긴다(CS_MOVE_ITEM, A6.1)
	int shopIntervalMs = 0;    // >0이면 이 간격(±50%)마다 시야 안의 상점 NPC에 구매(물약 1~3개)와 판매(인벤토리 행 하나에서 1개)를 번갈아 요청한다(L4). NPC 근처에 있어야 하므로 --zone과 함께 쓴다
	int portalIntervalMs = 0;  // >0이면 이 간격(±50%)마다 가장 가까운 포털에 CS_PORTAL을 보낸다(L5). 반경 밖이면 서버가 버리므로 --zone으로 포털 앞에 세운다
	int partySize = 0;         // --party-size N (DG2): 봇을 번호순으로 N명씩 묶어 파티를 맺는다(묶음의 첫 봇이 나머지를 오브젝트 id로 초대, 받은 초대는 모두 수락)
	// --dungeon (DG3c): 사냥터 Room에 있으면 던전 입구(포털 100)를 요청한다(파티 중이면 파티장만 — 파티원은 따라 들어간다). 입구 반경 안에 있어야 하므로
	// 서버를 [Debug] DummyRoom=field + DummySpawnZone=<입구>로 띄우거나 PortalNoRange=true로 띄운다.
	bool dungeon = false;
	int dungeonStaySec = 0;    // --dungeon-stay-sec S: 던전에 들어간 뒤 이 시간(±25%)이 지나면 출구 포털을 요청하고 다시 들어간다(서버 PortalNoRange=true 필요). 0 = 나오지 않음
	std::string dungeonMapLogic;   // --dungeon-map: 던전 레벨의 로직 JSON(몬스터 스폰 위치) — 던전 안의 hunt 봇이 찾아갈 곳
	int partyChurnSec = 0;     // --party-churn-sec S: 이 간격(±50%)마다 파티를 탈퇴한다(다시 초대돼 들어감 — 위임·해산·거부 경로 부하)
	int giveIntervalMs = 0;    // >0이면 이 간격(±50%)마다 개발 지급 요청(물약 1~5개 또는 골드). 아이템 작업 부하·스택 시험(A3.3)
	std::string outFile;
	std::string serverProc = "GameServer.exe";
	std::string mapLogic;          // 맵 로직 JSON(몬스터 스폰 위치). hunt 프로필의 봇이 가까운 스폰으로 이동하는 데 쓴다
	bool trackWorld = false;   // 시각화(DummyClient)용: 몬스터 위치·사망을 추적하고 스냅숏을 공개한다
	std::string navDir;        // --nav-dir: 레벨 내비메시(<레벨>_Geo.nav) 폴더. 비면 작업 폴더 기준 후보에서 찾는다(A5.1, BotNav)
	bool townWander = false;   // --town-wander: 마을 Room의 사냥 봇이 포털로 나가지 않고 마을 안전 구역 안을 걷는다(서버 DummyRoom=spread의 마을 비율 유지, A5.1)
	int mix[static_cast<int>(BotProfile::Count)] = { 0, 100 };   // 프로필별 비율(%) — 기본 = 모두 Move
	std::string mixText = "move";                                // 시작 줄에 찍을 이름
	// 로그인(A5.2, D-296·D-300): false = is_dummy(공용 dummy 계정), true = bot_<번호 + accountStart> / 1234 일반 로그인(SQL로 미리 만든 계정)
	bool accountLogin = false;
	int accountStart = 0;
	int loginRetries = 5;      // 로그인 실패·응답 없음·연결 실패 때 다시 시도하는 횟수(1·2·4…30초 지수 백오프)
	int loginTimeoutMs = 15000;
	std::string dataDir;       // --data-dir: Common/DataTable 폴더(SkillTable·NpcTable). 비면 작업 폴더 기준 후보
	int chatIntervalMs = 15000;    // Chat 봇의 채팅 간격(±50%)
	int tradeIntervalMs = 20000;   // Trade 봇이 다음 거래를 시작하기까지(±50%)
	int auctionIntervalMs = 3000;  // Auction 봇의 요청 사이 간격(±50%, 서버 간격 AuctionActionIntervalMs 1초보다 길게)
	bool zone = false;         // 핫존: 봇이 (zoneX, zoneY)로 걸어가 반경 zoneR 안에서만 움직인다 (--zone x,y,r)
	float zoneX = 0, zoneY = 0, zoneR = 0;
};

// ---------------------------------------------------------------- 집계
struct BotCounters
{
	std::atomic<long long> connectAttempts{ 0 }, connectOk{ 0 }, connectFail{ 0 };
	std::atomic<long long> loginOk{ 0 }, loginFail{ 0 }, unexpectedClose{ 0 }, protocolError{ 0 }, sendDropped{ 0 };
	std::atomic<long long> pktSent{ 0 }, pktRecv{ 0 }, bytesRecv{ 0 }, pongRecv{ 0 }, moveCorrected{ 0 }, moveBatchRecv{ 0 };
	std::atomic<long long> monsterDead{ 0 }, monsterRemoved{ 0 }, monsterAdded{ 0 }, damageRecv{ 0 }, objectAdded{ 0 };
	std::atomic<long long> aimSent{ 0 }, aimRecv{ 0 }, aimOwnOff{ 0 };   // CS_SKILL_AIM / 받은 SC_SKILL_AIM / 내 조준의 거부·시간 초과
	std::atomic<long long> equipSent{ 0 }, equipOk{ 0 }, equipRejected{ 0 };   // 장착 요청 / 내 SC_CHANGE_EQUIP / SC_CHANGE_EQUIP_RESULT
	// 공격(CP1): CS_ATTACK 송신 / 내 SC_ATTACK(순번 일치) / SC_ATTACK_RESULT(거부), AttackResult 값별
	std::atomic<long long> attackSent{ 0 }, attackOk{ 0 }, attackRejected{ 0 };
	std::atomic<long long> attackRejectedBy[16]{};                             // AttackResult 0~15 (A2.3b NOT_ENOUGH_MP = 11까지)
	std::atomic<long long> equipRejectedBy[16]{};                              // SC_CHANGE_EQUIP_RESULT를 EquipResult 값별로
	std::atomic<long long> mpChangeRecv{ 0 }, expRecv{ 0 };                    // 성장·MP(A2.3b): 받은 SC_CHANGE_MP / SC_GAIN_EXP·SC_LEVEL_UP
	// 인벤토리(A3.3): 받은 SC_INVENTORY / SC_INVENTORY_UPDATE, 지급 요청 / SC_ITEM_RESULT(거부) / ItemResult 값별
	std::atomic<long long> inventoryRecv{ 0 }, inventoryUpdateRecv{ 0 }, giveSent{ 0 }, itemRejected{ 0 }, slotMoveSent{ 0 };
	std::atomic<long long> itemRejectedBy[24]{};
	std::atomic<long long> shopBuySent{ 0 }, shopSellSent{ 0 };   // 상점 구매·판매 요청 (L4)
	std::atomic<long long> roomChange{ 0 }, moveRejected{ 0 };    // 내 SC_ROOM_CHANGE(Room 이동) / SC_MOVE_RESULT(이동 거부, DG3b)
		std::atomic<long long> moveRejectedBy[8]{};                   // SC_MOVE_RESULT를 MoveResult 값별로(4 = TIMEOUT, 5 = NOT_AVAILABLE)
		// 던전(DG3c): 입구 요청 / 던전 Room 도착 / 출구 요청 / 받은 SC_DUNGEON_STATE(CLEARED)
		std::atomic<long long> dungeonEnterSent{ 0 }, dungeonEntered{ 0 }, dungeonExitSent{ 0 }, dungeonCleared{ 0 };
		std::atomic<int> inDungeon{ 0 };   // 지금 던전 Room에 있는 봇 수
		std::atomic<long long> portalSent{ 0 }, portalMoved{ 0 };     // 포털 요청 / 내 SC_TELEPORT (L5)
	// 파티(DG2): 보낸 초대 / 받은 초대(= 수락 송신) / 받은 SC_PARTY_STATE / 탈퇴 송신 / SC_PARTY_RESULT, PartyResult 값별
	std::atomic<long long> partyInviteSent{ 0 }, partyInviteRecv{ 0 }, partyStateRecv{ 0 }, partyLeaveSent{ 0 }, partyResult{ 0 };
	std::atomic<long long> partyResultBy[16]{};
	std::atomic<int> partyIn{ 0 };   // 지금 파티에 든 봇 수(마지막으로 받은 상태 기준)
	std::atomic<long long> pathSearch{ 0 }, pathFail{ 0 };   // 길찾기(A5.1, BotNav) 횟수 / 실패(시작점을 메시에서 못 찾음 등 — 그때는 직선)
	std::atomic<long long> selfDead{ 0 }, reviveSent{ 0 }, selfRevived{ 0 };   // 내 SC_DEAD / CS_REVIVE / 내 SC_REVIVE (A2.4)
	// A5.2 프로필
	std::atomic<int> profileBots[static_cast<int>(BotProfile::Count)]{};   // 프로필별 봇 수(정해진 수, 접속 여부와 무관)
	std::atomic<long long> loginRetry{ 0 }, loginGiveUp{ 0 }, loginTimeout{ 0 };
	std::atomic<long long> loginFailBy[8]{};                               // LoginResult 값별
	std::atomic<long long> moveHeldByLock{ 0 }, attackHeld{ 0 };           // 이동 잠금 때문에 보내지 않은 이동 / 쿨타임·동작 중이라 미룬 공격
	std::atomic<long long> chatSent{ 0 }, chatRecv{ 0 };
	std::atomic<long long> dropSeen{ 0 }, pickupSent{ 0 }, pickupOk{ 0 }, dropItemSent{ 0 };
	std::atomic<long long> tradeRequestSent{ 0 }, tradeRequestRecv{ 0 }, tradeOfferSent{ 0 }, tradeConfirmSent{ 0 }, tradeCancelSent{ 0 }, tradeEnd{ 0 };
	std::atomic<long long> tradeEndBy[24]{}, tradeRejectBy[24]{};         // SC_TRADE_END / SC_TRADE_RESULT 를 TradeResult 값별로
	std::atomic<long long> tradeRejectReq[8]{};                           // SC_TRADE_RESULT 를 거부된 요청별로(0 REQUEST 1 RESPOND 2 SET_OFFER 3 LOCK 4 CONFIRM 5 CANCEL)
	// 경매: 요청별 송신(0 검색 1 등록 2 구매 3 취소 4 내 목록 5 수령) / SC_AUCTION_RESULT를 AuctionResult 값별로
	std::atomic<long long> auctionSent[6]{}, auctionOk[6]{}, auctionResultBy[32]{};
	std::atomic<long long> auctionListRecv{ 0 }, auctionMyRecv{ 0 }, mailNoticeRecv{ 0 };
	std::atomic<int> connected{ 0 }, inGame{ 0 };
	// 구간(리포트 간격) 동안 봇 한 명이 받은 최대 바이트. 리포터가 읽으며 0으로 되돌리고 windowEpoch를 올린다.
	std::atomic<long long> maxBotBytesWindow{ 0 };
	std::atomic<int> windowEpoch{ 0 };
};

// ---------------------------------------------------------------- 시각화 스냅숏 (trackWorld일 때만 채워진다)
enum class BotState { Idle, Connecting, LoggingIn, InGame, Closed };

struct BotView
{
	int index = 0;
	BotState state = BotState::Idle;
	float x = 0, y = 0;
};

struct MonsterView
{
	int id = 0;
	float x = 0, y = 0;
	bool dead = false;
};

extern BotConfig GConfig;
extern BotCounters GStat;
extern std::vector<std::pair<float, float>> GDungeonSpawns;   // --dungeon-map 의 몬스터 스폰 위치
extern std::vector<std::pair<float, float>> GMonsterSpawns;   // 맵 로직 JSON에서 읽은 몬스터 스폰 위치
struct TownZone { float x = 0, y = 0, r = 0; };
extern std::vector<TownZone> GTownZones;   // 맵 로직 JSON의 마을(area town) 안전 구역 — --town-wander 봇이 걷는 곳

namespace BotEngine
{
	long long NowUs();
	const char* ProfileName(BotProfile p);
	uint32_t Percentile(const std::vector<uint32_t>& sorted, double p);

	// 명령줄 인자를 GConfig에 채운다. 잘못됐거나 --help면 false.
	void Usage(const char* program);
	bool ParseArgs(int argc, char** argv);

	// 소켓 초기화, (hunt·trackWorld면) 맵 스폰 로드, 워커 스레드 시작. 시작 시각부터 램프업이 계산된다.
	void Start();
	// 시작 후 경과 시간에 맞춰 접속을 허용할 봇 수를 갱신한다. 주기적으로 호출한다.
	void UpdateRamp();
	double ElapsedSec();
	// 마지막 호출 이후 쌓인 RTT 샘플(마이크로초)을 가져간다.
	void TakeSamples(std::vector<uint32_t>& pingUs, std::vector<uint32_t>& loginUs);
	// 마지막 호출 이후 쌓인 접속 시간 표본(첫 연결 시작 → 방 입장, 마이크로초, A5.4b). 부르지 않는 프런트엔드는 쌓이기만 한다.
	void TakeEnterSamples(std::vector<uint32_t>& enterUs);
	// 워커들이 마지막으로 공개한 봇·몬스터 상태를 모은다(몬스터는 id로 중복 제거). 워커는 50ms마다 공개한다.
	void GetSnapshot(std::vector<BotView>& bots, std::vector<MonsterView>& monsters);
	// 워커를 멈추고 소켓을 닫는다.
	void Stop();
}

// 서버 프로세스의 CPU·메모리 샘플링
class ServerProcSampler
{
public:
	~ServerProcSampler();
	bool Open(const std::string& exeName);
	bool IsOpen() const { return _handle != nullptr; }
	// 서버 CPU 사용률(코어 1개 = 100%)과 메모리(MB)
	void Sample(double& cpuPct, double& memMb);

private:
	unsigned long long Cpu();

	void* _handle = nullptr;
	unsigned long long _lastCpu100ns = 0;
	long long _lastWallUs = 0;
};
