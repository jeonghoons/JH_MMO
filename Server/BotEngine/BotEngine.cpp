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
	int objectId = -1;
	float x = 0, y = 0, z = 0;
	vector<uint8_t> rbuf;   // 수신 누적 버퍼
	size_t rpos = 0;
	vector<uint8_t> pending; // 아직 못 보낸 송신 바이트
	unordered_map<int, pair<float, float>> monsters; // 시야 안의 몬스터 id -> (x, y)
	unordered_set<int> deadMonsters;   // SC_DEAD를 받았고 아직 제거되지 않은 몬스터(trackWorld)
	long long nextAttackUs = 0;
	int castCount = 0;
	int moveFailures = 0;   // 서버가 내 이동을 거부(보정)한 횟수. hunt 봇이 막혔는지 판단한다
	int spawnTarget = -1;   // hunt 봇이 찾아가는 몬스터 스폰(GMonsterSpawns 인덱스). 처음엔 가장 가까운 곳
	int blockedCount = 0;   // 지금 목표로 가다가 막힌 횟수. 많이 막히면 다른 스폰으로 바꾼다
	long long loginDueUs = 0;   // 가입 후 로그인을 보낼 시각(archer)
	bool spawnKnown = false;    // 입장 후 내 SC_ADD_OBJECT로 실제 시작 위치를 받았는지(로그인 응답의 위치는 0)
	float moveSpeed = 0.f;      // 서버 스탯 move_speed. 서버가 이 속도로 이동을 검증한다
	long long lastMoveSentUs = 0;
	uint32_t correctionSeq = 0; // 서버가 보낸 마지막 보정 순번. CS_MOVE에 실어 보낸다
};

static void SendPacket(Bot& b, uint16_t id, const google::protobuf::Message& msg);

// 서버의 속도 검증(MovementTable 허용치)에 걸리지 않도록, 마지막 이동 이후 경과 시간만큼만 움직인다(최대 1초 분량, 여유 10%).
static float MaxStep(const Bot& b, long long now)
{
	const double elapsedSec = min(1.0, (now - b.lastMoveSentUs) / 1e6);
	return static_cast<float>(b.moveSpeed * 0.9 * elapsedSec);
}

static void SendMove(Bot& b, long long now, float nx, float ny, float yaw, Protocol::MoveState state)
{
	Protocol::CS_MOVE_PACKET mv;
	Protocol::PositionInfo* p = mv.mutable_pos_info();
	p->set_x(nx); p->set_y(ny); p->set_z(b.z); p->set_yaw(yaw);
	p->set_state(state);
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

	if (GConfig.hero == "archer")
	{
		// 궁수 계정은 먼저 가입(이미 있으면 서버가 무시)하고 잠시 뒤 일반 로그인한다.
		Protocol::CS_SIGNUP_PACKET signup;
		signup.set_account_id("arc_" + to_string(b.index));
		signup.set_account_pw("1234");
		signup.set_player_type(Protocol::PLAYER_TYPE_ARCHER);
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
			// 서버가 벽 통과를 막으므로 직선으로 못 가는 스폰이 있다. 같은 목표로 여러 번 막히면 다른 스폰을 고른다.
			if (++b.blockedCount >= 6 && !GMonsterSpawns.empty())
			{
				b.spawnTarget = rand() % static_cast<int>(GMonsterSpawns.size());
				b.blockedCount = 0;
			}
		}
		break;
	}
	case Protocol::SC_ADD_OBJECT:
	{
		// 몬스터 목록은 hunt 프로필과 시각화에서만 추적한다. 그 외에는 내 시작 위치를 받을 때까지만 본다.
		if (!GConfig.hunt && !GConfig.trackWorld && b.spawnKnown) return;
		Protocol::SC_ADD_OBJECT_PACKET pkt;
		if (!pkt.ParseFromArray(payload, len)) { GStat.protocolError.fetch_add(1); return; }
		GStat.objectAdded.fetch_add(1);
		if (b.index == 0)
			printf("[bot] bot0 sees object id=%d type=%d playerType=%d pos=(%.0f,%.0f)\n", pkt.object_info().id(),
				static_cast<int>(pkt.object_info().object_type()), static_cast<int>(pkt.object_info().player_type()),
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
		break;
	}
	case Protocol::SC_DAMAGE:
		GStat.damageRecv.fetch_add(1);
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
				login.set_account_id("arc_" + to_string(b.index));
				login.set_account_pw("1234");
				b.loginSentUs = now;
				SendPacket(b, Protocol::CS_LOGIN, login);
			}

			if (b.state == BotState::InGame && b.spawnKnown)
			{
				// hunt 프로필은 주변에 알려진 몬스터가 없을 때만 무작위로 돌아다니며 몬스터를 찾는다.
				if (GConfig.move && now >= b.nextMoveUs && (!GConfig.hunt || b.monsters.empty()))
				{
					b.nextMoveUs = now + GConfig.moveIntervalMs * 1000LL;
					const float step = min(300.f, MaxStep(b, now));
					float nx = b.x, ny = b.y, yaw = 0.f;
					if (GConfig.hunt && !GMonsterSpawns.empty() && b.moveFailures < 3)
					{
						// 맵 정보가 있으면 가장 가까운 몬스터 스폰 쪽으로 한 걸음씩 다가간다. 막혀서 보정되면 잠시 무작위로 돌아다니고, 계속 막히면 목표를 바꾼다.
						if (b.spawnTarget < 0)
						{
							float best = 1e30f;
							for (int i = 0; i < static_cast<int>(GMonsterSpawns.size()); ++i)
							{
								const float sx = GMonsterSpawns[i].first - b.x, sy = GMonsterSpawns[i].second - b.y;
								if (sx * sx + sy * sy < best) { best = sx * sx + sy * sy; b.spawnTarget = i; }
							}
						}
						const auto& spawn = GMonsterSpawns[b.spawnTarget];
						const float dx = spawn.first - b.x, dy = spawn.second - b.y;
						const float dist = sqrtf(dx * dx + dy * dy);
						const float stride = min({ 500.f, dist, MaxStep(b, now) });
						if (dist > 1.f) { nx = b.x + dx / dist * stride; ny = b.y + dy / dist * stride; }
						yaw = atan2f(dy, dx) * 180.f / 3.14159265f;
					}
					else
					{
						switch (rand() % 4)
						{
						case 0: nx -= step; yaw = 180.f; break;
						case 1: nx += step; yaw = 0.f; break;
						case 2: ny -= step; yaw = -90.f; break;
						default: ny += step; yaw = 90.f; break;
						}
						if (b.moveFailures > 0) --b.moveFailures;   // 무작위 이동을 몇 번 하면 다시 스폰 쪽으로 향한다
					}
					SendMove(b, now, nx, ny, yaw, Protocol::MOVE_STATE_RUN);
				}
				if (GConfig.hunt && now >= b.nextAttackUs && !b.monsters.empty())
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
					// warrior: 논타겟 -> 몬스터 방향(aim_yaw). archer: 일반공격은 target_id, 1번 스킬은 몬스터 위치(aim_x/y)에 장판.
					Protocol::CS_ATTACK_PACKET atk;
					const int slot = (++b.castCount % 3 == 0) ? 1 : 0;
					atk.set_skill_slot(slot);
					atk.set_target_id(GConfig.hero == "archer" && slot == 0 ? targetId : 0);
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
						default: atk.set_skill_slot(2); break;                       // 비어 있는 슬롯
						}
					}
					SendPacket(b, Protocol::CS_ATTACK, atk);
				}
				if (now >= b.nextPingUs)
				{
					b.nextPingUs = now + GConfig.pingIntervalMs * 1000LL;
					Protocol::CS_PING_PACKET ping;
					ping.set_client_time(now);
					SendPacket(b, Protocol::CS_PING, ping);
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
		"         [--move-interval ms] [--ping-interval ms] [--profile move|idle|hunt] [--hero warrior|archer] [--chaos] [--map logic.json] [--out file.csv] [--server-proc name.exe]\n",
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
		else if (a == "--profile") { if (!(v = next("--profile"))) return false; GConfig.hunt = (string(v) == "hunt"); GConfig.move = (string(v) == "move" || GConfig.hunt); }
		else if (a == "--hero") { if (!(v = next("--hero"))) return false; GConfig.hero = v; if (GConfig.hero != "warrior" && GConfig.hero != "archer") { printf("--hero must be warrior or archer\n"); return false; } }
		else if (a == "--chaos") { GConfig.chaos = true; }
		else if (a == "--map") { if (!(v = next("--map"))) return false; GConfig.mapLogic = v; }
		else if (a == "--out") { if (!(v = next("--out"))) return false; GConfig.outFile = v; }
		else if (a == "--server-proc") { if (!(v = next("--server-proc"))) return false; GConfig.serverProc = v; }
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
}

void BotEngine::Start()
{
	WSADATA wsa;
	WSAStartup(MAKEWORD(2, 2), &wsa);
	srand(static_cast<unsigned>(time(nullptr)));

	if (GConfig.hunt || GConfig.trackWorld)
		LoadMonsterSpawns();

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
