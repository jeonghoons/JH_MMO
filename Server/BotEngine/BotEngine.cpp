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

static atomic<bool> GRunning{ true };
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
	long long loginSentUs = 0;
	long long nextMoveUs = 0;
	long long nextPingUs = 0;
	long long nextEquipUs = 0;
	// 인벤토리(A3.3): item_db_id -> (item_id, equip_part). SC_INVENTORY·SC_INVENTORY_UPDATE로 채운다
	unordered_map<long long, pair<int, int>> items;
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
	unordered_set<int> deadMonsters;   // SC_DEAD를 받았고 아직 제거되지 않은 몬스터(trackWorld)
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
};

// 봇 번호 → 오브젝트 id(로그인 응답으로 받은 값, 0 = 아직 모름). 파티 초대 대상을 찾는 데 쓴다(워커 스레드 사이 공유).
static unique_ptr<atomic<int>[]> GBotObjectIds;

static void SendPacket(Bot& b, uint16_t id, const google::protobuf::Message& msg);

// 서버의 속도 검증(MovementTable 허용치)에 걸리지 않도록, 마지막 이동 이후 경과 시간만큼만 움직인다(최대 1초 분량, 여유 10%).
static float MaxStep(const Bot& b, long long now)
{
	const double elapsedSec = min(1.0, (now - b.lastMoveSentUs) / 1e6);
	// 조준 중에는 서버가 AimMoveSpeedMul(활 0.4)을 곱하므로 그만큼만 움직인다
	const double aimMul = b.aiming ? 0.4 : 1.0;
	return static_cast<float>(b.moveSpeed * 0.9 * aimMul * elapsedSec);
}

static void SendMove(Bot& b, long long now, float nx, float ny, float yaw, Protocol::MoveState state)
{
	Protocol::CS_MOVE_PACKET mv;
	Protocol::PositionInfo* p = mv.mutable_pos_info();
	p->set_x(nx); p->set_y(ny); p->set_z(b.z); p->set_yaw(yaw);
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
	b.x = nx; b.y = ny;   // 서버가 거부하면 보정 패킷(SC_MOVE_OBJECT)으로 되돌려진다.
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
static const array<float, 3>* HuntTownPortal(const Bot& b)
{
	if (!GConfig.hunt || b.inField) return nullptr;
	if (b.roomKind == static_cast<int>(Protocol::ROOM_KIND_FIELD) || b.roomKind == static_cast<int>(Protocol::ROOM_KIND_DUNGEON)) return nullptr;
	if (b.roomKind == static_cast<int>(Protocol::ROOM_KIND_NONE) && !b.monsters.empty()) return nullptr;
	return NearestTownPortal(b);
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
	b.pending.clear();
	b.monsters.clear();
	b.deadMonsters.clear();
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
	if (b.sock == INVALID_SOCKET) { GStat.connectFail.fetch_add(1); b.state = BotState::Closed; return; }

	u_long nonBlocking = 1;
	ioctlsocket(b.sock, FIONBIO, &nonBlocking);
	BOOL noDelay = TRUE;
	setsockopt(b.sock, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&noDelay), sizeof(noDelay));

	sockaddr_in addr{};
	addr.sin_family = AF_INET;
	addr.sin_port = htons(static_cast<u_short>(GConfig.port));
	inet_pton(AF_INET, GConfig.host.c_str(), &addr.sin_addr);

	b.connectStartUs = NowUs();
	b.state = BotState::Connecting;
	if (connect(b.sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR &&
		WSAGetLastError() != WSAEWOULDBLOCK)
	{
		GStat.connectFail.fetch_add(1);
		closesocket(b.sock);
		b.sock = INVALID_SOCKET;
		b.state = BotState::Closed;
	}
}

static void OnConnected(Bot& b)
{
	GStat.connectOk.fetch_add(1);
	GStat.connected.fetch_add(1);
	b.state = BotState::LoggingIn;

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
	login.set_is_dummy(true);   // 서버가 dummy 계정으로 치환한다. (A5.1에서 제거 예정)
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
			if (b.index == 0) printf("[bot] bot0 login failed: %s\n", Protocol::LoginResult_Name(pkt.result()).c_str());
			return;
		}

		b.objectId = pkt.object_info().id();
		if (GBotObjectIds) GBotObjectIds[b.index].store(b.objectId);
		b.x = pkt.object_info().position().x();
		b.y = pkt.object_info().position().y();
		b.z = pkt.object_info().position().z();
		b.moveSpeed = pkt.object_info().stat().move_speed();
		GStat.loginOk.fetch_add(1);
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
	case Protocol::SC_MOVE_OBJECT:
	{
		// 모든 이동 브로드캐스트를 파싱하면 비용이 크므로, object_info.id만 먼저 훑어서 내 것일 때만 파싱한다.
		// SC_MOVE_PACKET = { 1: object_info(LEN) { 1: id(varint) ... } }
		if (len < 4 || payload[0] != 0x0A || payload[2] != 0x08) return;
		uint32_t id = 0; int shift = 0; int i = 3;
		while (i < len && shift < 32) { id |= (payload[i] & 0x7F) << shift; if (!(payload[i++] & 0x80)) break; shift += 7; }
		if (static_cast<int>(id) != b.objectId)
		{
			// 시각화 중에는 시야 안 몬스터의 이동도 따라간다.
			if (!GConfig.trackWorld) return;
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
			b.correctionSeq = pkt.correction_seq();
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
		if (!GConfig.hunt && !GConfig.trackWorld && GConfig.shopIntervalMs <= 0 && b.spawnKnown) return;
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
			b.x = pkt.object_info().position().x();
			b.y = pkt.object_info().position().y();
			b.z = pkt.object_info().position().z();
		}
		if (pkt.object_info().object_type() == Protocol::OBJECT_TYPE_NPC)
			b.shopNpcId = pkt.object_info().id();
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
		b.deadMonsters.erase(pkt.object_id());
		break;
	}
	case Protocol::SC_DEAD:
	{
		Protocol::SC_DEAD_PACKET pkt;
		if (!pkt.ParseFromArray(payload, len)) { GStat.protocolError.fetch_add(1); return; }
		if (b.monsters.count(pkt.object_id()))
		{
			GStat.monsterDead.fetch_add(1);
			if (GConfig.trackWorld) b.deadMonsters.insert(pkt.object_id());
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
		GStat.portalMoved.fetch_add(1);
		break;
	}
	case Protocol::SC_DAMAGE:
		GStat.damageRecv.fetch_add(1);
		break;
	case Protocol::SC_ATTACK:
	{
		// 내 시전이 받아들여졌는지만 센다(사냥 프로필일 때만 파싱 — 이동 측정의 봇 비용을 늘리지 않게).
		if (!GConfig.hunt) break;
		Protocol::SC_ATTACK_PACKET pkt;
		if (!pkt.ParseFromArray(payload, len)) { GStat.protocolError.fetch_add(1); return; }
		if (pkt.attacker_id() == b.objectId && pkt.seq() != 0) GStat.attackOk.fetch_add(1);
		break;
	}
	case Protocol::SC_ATTACK_RESULT:
	{
		GStat.attackRejected.fetch_add(1);
		Protocol::SC_ATTACK_RESULT_PACKET pkt;
		if (!pkt.ParseFromArray(payload, len)) { GStat.protocolError.fetch_add(1); return; }
		const int code = static_cast<int>(pkt.result());
		if (code >= 0 && code < 16) GStat.attackRejectedBy[code].fetch_add(1);
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
			b.items[item.item_db_id()] = { item.item_id(), static_cast<int>(item.equip_part()) };
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
			b.items[item.item_db_id()] = { item.item_id(), static_cast<int>(item.equip_part()) };
		for (const long long removed : pkt.removed())
			b.items.erase(removed);
		break;
	}
	case Protocol::SC_ITEM_RESULT:
	{
		Protocol::SC_ITEM_RESULT_PACKET pkt;
		if (!pkt.ParseFromArray(payload, len)) { GStat.protocolError.fetch_add(1); return; }
		GStat.itemRejected.fetch_add(1);
		const int code = static_cast<int>(pkt.result());
		if (code >= 0 && code < 24) GStat.itemRejectedBy[code].fetch_add(1);
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
			if (b.state == BotState::Idle && b.index < allowed)
				StartConnect(b);

			if (b.state == BotState::Connecting && now - b.connectStartUs > 10 * 1000 * 1000LL)
			{
				GStat.connectFail.fetch_add(1);
				closesocket(b.sock); b.sock = INVALID_SOCKET; b.state = BotState::Closed;
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

				// hunt 프로필은 주변에 알려진 몬스터가 없을 때만 무작위로 돌아다니며 몬스터를 찾는다.
				if (canAct && GConfig.move && now >= b.nextMoveUs && (!GConfig.hunt || b.monsters.empty() || (GConfig.dungeon && b.roomKind != static_cast<int>(Protocol::ROOM_KIND_DUNGEON))))
				{
					b.nextMoveUs = now + GConfig.moveIntervalMs * 1000LL;
					const float step = min(300.f, MaxStep(b, now));
					float nx = b.x, ny = b.y, yaw = 0.f;
					const float zdx = GConfig.zoneX - b.x, zdy = GConfig.zoneY - b.y;
					const float zoneDist = sqrtf(zdx * zdx + zdy * zdy);
					if (const array<float, 3>* tp = HuntTownPortal(b); tp && b.moveFailures < 3)
						{
							// 마을 Room의 사냥 봇(DG3b): 가장 가까운 마을 포털 앞(150cm)까지 직선으로 걸어간다. 요청은 아래 포털 블록이 보낸다.
							// 막혀서 보정이 쌓이면(moveFailures 3 이상) 이 분기를 건너뛰고 무작위 이동으로 비킨다.
							const float dx = (*tp)[0] - b.x, dy = (*tp)[1] - b.y;
							const float dist = sqrtf(dx * dx + dy * dy);
							if (dist > kPortalStopCm)
							{
								const float stride = min({ 500.f, dist - kPortalStopCm, MaxStep(b, now) });
								nx = b.x + dx / dist * stride; ny = b.y + dy / dist * stride;
							}
							yaw = atan2f(dy, dx) * 180.f / 3.14159265f;
						}
						else if (GConfig.turn)
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
						const float stride = min({ 500.f, zoneDist, MaxStep(b, now) });
						nx = b.x + zdx / zoneDist * stride; ny = b.y + zdy / zoneDist * stride;
						yaw = atan2f(zdy, zdx) * 180.f / 3.14159265f;
					}
					else if ((!GConfig.zone || b.roomKind == static_cast<int>(Protocol::ROOM_KIND_DUNGEON)) && GConfig.hunt && !HuntSpawns(b).empty() && b.moveFailures < 3)
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
						const float stride = min({ 500.f, dist, MaxStep(b, now) });
						if (dist > 1.f) { nx = b.x + dx / dist * stride; ny = b.y + dy / dist * stride; }
						yaw = atan2f(dy, dx) * 180.f / 3.14159265f;
					}
					else
					{
						// 목표 지점으로 걷는다. 도착·시간 초과(10초)·막힘(보정)이면 새 목표: 10~30m 떨어진 임의 지점,
						// 핫존 안이면 존 반경 안의 임의 지점(길 찾기는 없다 — 막히면 다른 목표로 바꿀 뿐).
						float gdx = b.goalX - b.x, gdy = b.goalY - b.y;
						float goalDist = sqrtf(gdx * gdx + gdy * gdy);
						if (!b.hasGoal || goalDist < 100.f || now >= b.goalUntilUs)
						{
							const float angle = (rand() % 3600) * 0.1f * 3.14159265f / 180.f;
							if (GConfig.zone && zoneDist <= GConfig.zoneR)
							{
								const float d = GConfig.zoneR * sqrtf((rand() % 1000) / 1000.f);
								b.goalX = GConfig.zoneX + cosf(angle) * d;
								b.goalY = GConfig.zoneY + sinf(angle) * d;
							}
							else
							{
								const float d = 1000.f + (rand() % 2000);
								b.goalX = b.x + cosf(angle) * d;
								b.goalY = b.y + sinf(angle) * d;
							}
							b.hasGoal = true;
							b.goalUntilUs = now + 10'000'000LL;
							gdx = b.goalX - b.x; gdy = b.goalY - b.y;
							goalDist = sqrtf(gdx * gdx + gdy * gdy);
						}
						if (goalDist > 1.f)
						{
							const float stride = min(goalDist, step);
							nx = b.x + gdx / goalDist * stride; ny = b.y + gdy / goalDist * stride;
							yaw = atan2f(gdy, gdx) * 180.f / 3.14159265f;
						}
						if (b.moveFailures > 0) --b.moveFailures;   // 무작위 이동을 몇 번 하면 다시 스폰(또는 핫존) 쪽으로 향한다
					}
					SendMove(b, now, nx, ny, yaw, Protocol::MOVE_STATE_RUN);
				}
				// 사냥 봇의 마을 포털 요청(DG3b): 포털 앞 200cm 안에 오면 CS_PORTAL을 보낸다. 서버는 거리·재사용 대기가 안 맞으면 응답 없이 버리므로
					// 응답(SC_ROOM_CHANGE)이 올 때까지 1초마다 다시 보낸다. 방이 바뀌면 SC_ROOM_CHANGE가 이 시각을 지운다.
					if (canAct && now >= b.nextHuntPortalUs)
					{
						if (const array<float, 3>* tp = HuntTownPortal(b); tp)
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
					if (canAct && GConfig.hunt && now >= b.nextAttackUs && !b.monsters.empty() && !(GConfig.dungeon && b.roomKind != static_cast<int>(Protocol::ROOM_KIND_DUNGEON)))   // --dungeon: outside the dungeon, do not attack (the portal request is dropped while casting)
				{
					b.nextAttackUs = now + 700 * 1000LL;

					// 시야 안에서 가장 가까운 몬스터를 고른다. 서버가 허용하는 속도만큼 몬스터 앞으로 다가간 뒤 그 방향을 보고 공격한다.
					int targetId = -1; float tx = 0, ty = 0, best = 1e30f;
					for (const auto& [id, pos] : b.monsters)
					{
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
						SendMove(b, now, b.x + dx / dist * stride, b.y + dy / dist * stride, yaw,
							stride < dist - 100.f ? Protocol::MOVE_STATE_RUN : Protocol::MOVE_STATE_IDLE);
					}

					// 일반공격(슬롯 0)을 보내고, 3번에 한 번은 1번 스킬(슬롯 1)을 쓴다.
					// 몬스터 방향(aim_yaw)·위치(aim_x/y)와 대상(target_id)을 함께 보내고, 서버가 장착 무기의 스킬에 맞는 값만 쓴다.
					// (MC3.1: 무기가 없어 맨손 기본 공격만 있고 1번 슬롯은 서버가 거부한다 — MC3.2 장착 후 검·활 스킬)
					// SK2(D-278): 6번에 한 번은 2번 스킬(슬롯 2)도 쓴다 — 기본 공격 4 : 스킬1 1 : 스킬2 1.
					Protocol::CS_ATTACK_PACKET atk;
					const int castNo = ++b.castCount;
					int slot = (castNo % 6 == 0) ? 2 : (castNo % 3 == 0) ? 1 : 0;
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
					if (!skipAttack)
					{
						atk.set_seq(++b.attackSeq);
						GStat.attackSent.fetch_add(1);
						SendPacket(b, Protocol::CS_ATTACK, atk);
					}
				}
				// --aim: 처음 한 번 활을 장착한다(조준할 수 있는 스킬 3002). 장착은 내 인벤토리의 item_db_id로 요청하므로(A3.3)
				// 활(6102)이 없으면 개발 지급 명령으로 먼저 받는다(서버 [Debug] DevCommands=true 필요).
				if (!b.dead && GConfig.aim && !b.aimBowSent && b.inventoryKnown)
				{
					long long bowDbId = 0;
					for (const auto& [dbId, item] : b.items)
						if (item.first == 6102 && item.second == 0) { bowDbId = dbId; break; }
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
								if (item.second != 0 || item.first / 1000 != part) continue;
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
							if (item.second != 0) continue;
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
						if (item.second != 0) continue;
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
		"         [--move-interval ms] [--ping-interval ms] [--profile move|idle|hunt] [--body male|female] [--chaos] [--equip-interval ms] [--give-gear] [--give-interval ms] [--slot-interval ms] [--shop-interval ms] [--portal-interval ms] [--party-size N] [--party-churn-sec S] [--dungeon] [--dungeon-stay-sec S] [--dungeon-map logic.json] [--aim] [--map logic.json] [--out file.csv] [--server-proc name.exe]\n"
		"         [--zone x,y,r]   (hotspot: bots walk to (x,y) and stay within radius r cm)\n",
		program);
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
		else if (a == "--profile") { if (!(v = next("--profile"))) return false; GConfig.hunt = (string(v) == "hunt"); GConfig.turn = (string(v) == "turn"); GConfig.move = (string(v) == "move" || GConfig.hunt || GConfig.turn); }
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
	return true;
}

// 맵 로직 JSON에서 몬스터 스폰 위치를 읽는다.
static void LoadMonsterSpawns()
{
	const char* candidates[] = { "../../MapResource/Export/ParagonSample/Logic/ParagonSample_Logic.json", "../MapResource/Export/ParagonSample/Logic/ParagonSample_Logic.json" };
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
				if (marker.at("type").get<string>() == "MonsterSpawn")
					GMonsterSpawns.push_back({ marker.at("position").at("x").get<float>(), marker.at("position").at("y").get<float>() });
			printf("[bot] monster spawns loaded: %zu (%s)\n", GMonsterSpawns.size(), path.c_str());
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

	if (GConfig.hunt || GConfig.trackWorld)
		LoadMonsterSpawns();

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
