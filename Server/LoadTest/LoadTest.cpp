// 헤드리스 부하 테스트 클라이언트.
// 봇 N명을 램프업으로 접속시켜 로그인 -> 입장 -> (이동/핑) 을 반복하고, 지연/처리량/서버 CPU를 CSV와 콘솔로 기록한다.
// 사용법: LoadTest.exe --bots 1000 --ramp-rate 100 --duration 60 [--host 127.0.0.1] [--port 8888]
//         [--threads 4] [--move-interval 1000] [--ping-interval 1000] [--profile move|idle] [--out loadtest.csv]
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
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <memory>
#include <fstream>
#include <iostream>
#include <limits>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

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

// ---------------------------------------------------------------- 설정
struct Config
{
	string host = "127.0.0.1";
	int port = 8888;
	int bots = 100;
	int rampRate = 50;        // 초당 접속 시도 수
	int durationSec = 60;     // 램프 시작부터 종료까지의 전체 실행 시간
	int threads = 4;
	int moveIntervalMs = 1000;
	int pingIntervalMs = 1000;
	bool move = true;          // 무작위 이동
	string hero = "warrior";   // warrior: 논타겟 일반공격, archer: 타겟팅 일반공격(가입 후 로그인)
	bool chaos = false;         // 일부 공격 요청을 일부러 잘못된 값으로 보낸다(오류 테스트)
	bool hunt = false;         // 가장 가까운 몬스터에게 이동해 공격 (hunt 프로필)
	string outFile;
	string serverProc = "GameServer.exe";
	string mapLogic;          // 맵 로직 JSON(몬스터 스폰 위치). hunt 프로필의 봇이 가까운 스폰으로 이동하는 데 쓴다
};

static Config GConfig;
static atomic<bool> GRunning{ true };
static atomic<bool> GShuttingDown{ false };
static vector<pair<float, float>> GMonsterSpawns;   // --map 으로 읽은 몬스터 스폰 위치
static atomic<int> GAllowed{ 0 };   // 램프업으로 지금까지 접속이 허용된 봇 수

static long long NowUs()
{
	return chrono::duration_cast<chrono::microseconds>(Clock::now().time_since_epoch()).count();
}

// ---------------------------------------------------------------- 집계
struct Counters
{
	atomic<long long> connectAttempts{ 0 }, connectOk{ 0 }, connectFail{ 0 };
	atomic<long long> loginOk{ 0 }, unexpectedClose{ 0 }, protocolError{ 0 }, sendDropped{ 0 };
	atomic<long long> pktSent{ 0 }, pktRecv{ 0 }, bytesRecv{ 0 }, pongRecv{ 0 }, moveCorrected{ 0 };
	atomic<long long> monsterDead{ 0 }, monsterRemoved{ 0 }, monsterAdded{ 0 }, damageRecv{ 0 }, objectAdded{ 0 };
	atomic<int> connected{ 0 }, inGame{ 0 };
};
static Counters GStat;

// 스레드별 RTT 샘플 버퍼. 워커가 push, 리포터가 swap으로 가져간다.
struct SampleBuffer
{
	mutex m;
	vector<uint32_t> pingUs;
	vector<uint32_t> loginUs;
};
static vector<unique_ptr<SampleBuffer>> GSamples;

static uint32_t Percentile(const vector<uint32_t>& sorted, double p)
{
	if (sorted.empty()) return 0;
	size_t idx = static_cast<size_t>(p * (sorted.size() - 1) + 0.5);
	return sorted[min(idx, sorted.size() - 1)];
}

// ---------------------------------------------------------------- 봇
enum class BotState { Idle, Connecting, LoggingIn, InGame, Closed };

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
	long long nextAttackUs = 0;
	int castCount = 0;
	int moveFailures = 0;   // 서버가 내 이동을 거부(보정)한 횟수. hunt 봇이 막혔는지 판단한다
	long long loginDueUs = 0;   // 가입 후 로그인을 보낼 시각(archer)
};

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

		b.objectId = pkt.object_info().id();
		b.x = pkt.object_info().position().x();
		b.y = pkt.object_info().position().y();
		b.z = pkt.object_info().position().z();
		GStat.loginOk.fetch_add(1);
		if (b.index == 0)
		{
			const auto& s = pkt.object_info().stat();
			printf("[loadtest] bot0 login: id=%d playerType=%d maxHp=%d attack=%d attackSpeed=%.2f moveSpeed=%.1f pos=(%.0f, %.0f, %.0f)\n",
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
		if (static_cast<int>(id) != b.objectId) return;

		Protocol::SC_MOVE_PACKET pkt;
		if (pkt.ParseFromArray(payload, len))
		{
			b.x = pkt.object_info().position().x();
			b.y = pkt.object_info().position().y();
			GStat.moveCorrected.fetch_add(1);
			b.moveFailures = min(b.moveFailures + 3, 8);
		}
		break;
	}
	case Protocol::SC_ADD_OBJECT:
	{
		if (!GConfig.hunt) return;   // 몬스터 목록은 hunt 프로필에서만 추적한다.
		Protocol::SC_ADD_OBJECT_PACKET pkt;
		if (!pkt.ParseFromArray(payload, len)) { GStat.protocolError.fetch_add(1); return; }
		GStat.objectAdded.fetch_add(1);
		if (b.index == 0)
			printf("[loadtest] bot0 sees object id=%d type=%d playerType=%d pos=(%.0f,%.0f)\n", pkt.object_info().id(),
				static_cast<int>(pkt.object_info().object_type()), static_cast<int>(pkt.object_info().player_type()),
				pkt.object_info().position().x(), pkt.object_info().position().y());
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
		break;
	}
	case Protocol::SC_DEAD:
	{
		Protocol::SC_DEAD_PACKET pkt;
		if (!pkt.ParseFromArray(payload, len)) { GStat.protocolError.fetch_add(1); return; }
		if (b.monsters.count(pkt.object_id())) GStat.monsterDead.fetch_add(1);
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

	while (GRunning.load())
	{
		const long long now = NowUs();
		const int allowed = GAllowed.load();

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

			if (b.state == BotState::InGame)
			{
				// hunt 프로필은 주변에 알려진 몬스터가 없을 때만 무작위로 돌아다니며 몬스터를 찾는다.
				if (GConfig.move && now >= b.nextMoveUs && (!GConfig.hunt || b.monsters.empty()))
				{
					b.nextMoveUs = now + GConfig.moveIntervalMs * 1000LL;
					Protocol::CS_MOVE_PACKET mv;
					Protocol::PositionInfo* p = mv.mutable_pos_info();
					const float step = 300.f;
					float nx = b.x, ny = b.y, yaw = 0.f;
					if (GConfig.hunt && !GMonsterSpawns.empty() && b.moveFailures < 3)
					{
						// 맵 정보가 있으면 봇마다 정해진 몬스터 스폰 쪽으로 한 걸음씩 다가간다. 막혀서 보정되면 잠시 무작위로 돌아다닌다.
						const auto& spawn = GMonsterSpawns[b.index % GMonsterSpawns.size()];
						const float dx = spawn.first - b.x, dy = spawn.second - b.y;
						const float dist = sqrtf(dx * dx + dy * dy);
						const float stride = min(500.f, dist);
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
					p->set_x(nx); p->set_y(ny); p->set_z(b.z); p->set_yaw(yaw);
					p->set_state(Protocol::MOVE_STATE_RUN);
					mv.set_force(false);
					SendPacket(b, Protocol::CS_MOVE, mv);
					b.x = nx; b.y = ny;   // 서버가 거부하면 보정 패킷(SC_MOVE_OBJECT)으로 되돌려진다.
				}
				if (GConfig.hunt && now >= b.nextAttackUs && !b.monsters.empty())
				{
					b.nextAttackUs = now + 700 * 1000LL;

					// 시야 안에서 가장 가까운 몬스터를 고른다. 이동은 클라이언트 권위이므로 몬스터 바로 앞으로 이동한 뒤 그 방향을 보고 공격한다.
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
					float nx = b.x, ny = b.y;
					if (dist > 120.f) { nx = tx - dx / dist * 100.f; ny = ty - dy / dist * 100.f; }

					Protocol::CS_MOVE_PACKET mv;
					Protocol::PositionInfo* p = mv.mutable_pos_info();
					p->set_x(nx); p->set_y(ny); p->set_z(b.z); p->set_yaw(yaw);
					p->set_state(Protocol::MOVE_STATE_IDLE);   // hunt 중에는 몬스터 바로 앞으로 이동
					mv.set_force(false);
					SendPacket(b, Protocol::CS_MOVE, mv);
					b.x = nx; b.y = ny;

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

// ---------------------------------------------------------------- 서버 프로세스 샘플링
struct ProcSampler
{
	HANDLE h = nullptr;
	ULONGLONG lastCpu100ns = 0;
	long long lastWallUs = 0;

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

	void Open(const string& name)
	{
		DWORD pid = FindPid(name);
		if (pid) h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
		if (h) { lastCpu100ns = Cpu(); lastWallUs = NowUs(); }
	}

	ULONGLONG Cpu()
	{
		FILETIME c, e, k, u;
		if (!GetProcessTimes(h, &c, &e, &k, &u)) return 0;
		return ((static_cast<ULONGLONG>(k.dwHighDateTime) << 32) | k.dwLowDateTime) +
			((static_cast<ULONGLONG>(u.dwHighDateTime) << 32) | u.dwLowDateTime);
	}

	// 서버 CPU 사용률(코어 1개 = 100%)과 메모리(MB)
	void Sample(double& cpuPct, double& memMb)
	{
		cpuPct = 0; memMb = 0;
		if (!h) return;
		ULONGLONG cpu = Cpu();
		long long wall = NowUs();
		if (wall > lastWallUs) cpuPct = (cpu - lastCpu100ns) / 10.0 / (wall - lastWallUs) * 100.0;
		lastCpu100ns = cpu; lastWallUs = wall;

		PROCESS_MEMORY_COUNTERS pmc{};
		if (GetProcessMemoryInfo(h, &pmc, sizeof(pmc))) memMb = pmc.WorkingSetSize / (1024.0 * 1024.0);
	}
};

// ---------------------------------------------------------------- 메인
static void Usage()
{
	printf("LoadTest --bots N --ramp-rate R --duration S [--host H] [--port P] [--threads T]\n"
		"         [--move-interval ms] [--ping-interval ms] [--profile move|idle|hunt] [--hero warrior|archer] [--chaos] [--out file.csv] [--server-proc name.exe]\n");
}

static bool ParseArgs(int argc, char** argv)
{
	for (int i = 1; i < argc; ++i)
	{
		string a = argv[i];
		auto next = [&](const char* what) -> const char* {
			if (i + 1 >= argc) { printf("missing value for %s\n", what); return nullptr; }
			return argv[++i];
		};
		const char* v = nullptr;
		if (a == "--help" || a == "-h") { Usage(); return false; }
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
		else { printf("unknown option: %s\n", a.c_str()); Usage(); return false; }
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

int main(int argc, char** argv)
{
	if (!ParseArgs(argc, argv)) return 1;

	if (GConfig.outFile.empty())
	{
		char name[64];
		snprintf(name, sizeof(name), "loadtest_%lld.csv", static_cast<long long>(time(nullptr)));
		GConfig.outFile = name;
	}

	WSADATA wsa;
	WSAStartup(MAKEWORD(2, 2), &wsa);
	srand(static_cast<unsigned>(time(nullptr)));

	printf("[loadtest] host=%s:%d bots=%d ramp=%d/s duration=%ds threads=%d profile=%s hero=%s chaos=%d out=%s\n",
		GConfig.host.c_str(), GConfig.port, GConfig.bots, GConfig.rampRate, GConfig.durationSec,
		GConfig.threads, GConfig.hunt ? "hunt" : (GConfig.move ? "move" : "idle"), GConfig.hero.c_str(), GConfig.chaos ? 1 : 0, GConfig.outFile.c_str());

	// hunt 프로필: 맵 로직 JSON에서 몬스터 스폰 위치를 읽는다.
	if (GConfig.hunt)
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
				printf("[loadtest] monster spawns loaded: %zu (%s)\n", GMonsterSpawns.size(), path.c_str());
			}
			catch (const exception& e) { printf("[loadtest] map json error: %s\n", e.what()); }
		}
		else printf("[loadtest] map logic json not found; hunt bots will wander randomly (use --map <path>)\n");
	}

	for (int i = 0; i < GConfig.threads; ++i) GSamples.push_back(make_unique<SampleBuffer>());

	vector<thread> workers;
	for (int i = 0; i < GConfig.threads; ++i)
		workers.emplace_back(WorkerMain, i, GConfig.threads);

	ProcSampler proc;
	proc.Open(GConfig.serverProc);
	if (!proc.h) printf("[loadtest] server process '%s' not found; server CPU/memory columns will be 0\n", GConfig.serverProc.c_str());

	ofstream csv(GConfig.outFile);
	csv << "t_sec,connected,in_game,connect_fail,unexpected_close,login_p50_ms,login_p95_ms,ping_p50_ms,ping_p95_ms,ping_p99_ms,ping_max_ms,"
		"sent_ps,recv_ps,recv_kbps,move_corrected,server_cpu_pct,server_mem_mb\n";

	vector<uint32_t> allPing, allLogin;
	const long long startUs = NowUs();
	long long lastReportUs = startUs;
	long long lastSent = 0, lastRecv = 0, lastBytes = 0, lastCorrected = 0;
	constexpr int REPORT_SEC = 5;
	long long nextReportUs = startUs + REPORT_SEC * 1000000LL;

	while (true)
	{
		this_thread::sleep_for(chrono::milliseconds(100));
		const long long now = NowUs();
		const double elapsed = (now - startUs) / 1e6;

		GAllowed.store(static_cast<int>(min<double>(GConfig.bots, elapsed * GConfig.rampRate)) + 1);

		const bool finished = elapsed >= GConfig.durationSec;
		if (now < nextReportUs && !finished) continue;

		// 구간 샘플 수집
		vector<uint32_t> ping, login;
		for (auto& sb : GSamples)
		{
			lock_guard<mutex> lock(sb->m);
			ping.insert(ping.end(), sb->pingUs.begin(), sb->pingUs.end());
			login.insert(login.end(), sb->loginUs.begin(), sb->loginUs.end());
			sb->pingUs.clear(); sb->loginUs.clear();
		}
		allPing.insert(allPing.end(), ping.begin(), ping.end());
		allLogin.insert(allLogin.end(), login.begin(), login.end());
		sort(ping.begin(), ping.end());
		sort(login.begin(), login.end());

		const double dt = max(0.001, (now - lastReportUs) / 1e6);
		long long sent = GStat.pktSent.load(), recv = GStat.pktRecv.load(), bytes = GStat.bytesRecv.load(), corrected = GStat.moveCorrected.load();
		double cpu, mem;
		proc.Sample(cpu, mem);

		char line[512];
		snprintf(line, sizeof(line),
			"%.0f,%d,%d,%lld,%lld,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.0f,%.0f,%.0f,%lld,%.0f,%.0f",
			elapsed, GStat.connected.load(), GStat.inGame.load(), GStat.connectFail.load(), GStat.unexpectedClose.load(),
			Percentile(login, 0.50) / 1000.0, Percentile(login, 0.95) / 1000.0,
			Percentile(ping, 0.50) / 1000.0, Percentile(ping, 0.95) / 1000.0, Percentile(ping, 0.99) / 1000.0,
			(ping.empty() ? 0.0 : ping.back() / 1000.0),
			(sent - lastSent) / dt, (recv - lastRecv) / dt, (bytes - lastBytes) / dt / 1024.0 * 8.0,
			corrected - lastCorrected, cpu, mem);
		csv << line << "\n";
		csv.flush();

		printf("[t=%3.0fs] connected=%d inGame=%d connFail=%lld closed=%lld | ping p50/p95/p99/max=%.1f/%.1f/%.1f/%.1f ms | sent/s=%.0f recv/s=%.0f | server cpu=%.0f%% mem=%.0fMB\n",
			elapsed, GStat.connected.load(), GStat.inGame.load(), GStat.connectFail.load(), GStat.unexpectedClose.load(),
			Percentile(ping, 0.50) / 1000.0, Percentile(ping, 0.95) / 1000.0, Percentile(ping, 0.99) / 1000.0,
			(ping.empty() ? 0.0 : ping.back() / 1000.0), (sent - lastSent) / dt, (recv - lastRecv) / dt, cpu, mem);

		lastReportUs = now; lastSent = sent; lastRecv = recv; lastBytes = bytes; lastCorrected = corrected;
		nextReportUs = now + REPORT_SEC * 1000000LL;

		if (finished) break;
	}

	GShuttingDown.store(true);
	GRunning.store(false);
	for (auto& t : workers) t.join();

	sort(allPing.begin(), allPing.end());
	sort(allLogin.begin(), allLogin.end());
	printf("\n[summary] attempts=%lld ok=%lld fail=%lld loginOk=%lld unexpectedClose=%lld protocolError=%lld sendDropped=%lld\n",
		GStat.connectAttempts.load(), GStat.connectOk.load(), GStat.connectFail.load(), GStat.loginOk.load(),
		GStat.unexpectedClose.load(), GStat.protocolError.load(), GStat.sendDropped.load());
	printf("[summary] login   p50/p95/p99/max = %.1f/%.1f/%.1f/%.1f ms (n=%zu)\n",
		Percentile(allLogin, 0.50) / 1000.0, Percentile(allLogin, 0.95) / 1000.0, Percentile(allLogin, 0.99) / 1000.0,
		(allLogin.empty() ? 0.0 : allLogin.back() / 1000.0), allLogin.size());
	printf("[summary] ping    p50/p95/p99/max = %.1f/%.1f/%.1f/%.1f ms (n=%zu)\n",
		Percentile(allPing, 0.50) / 1000.0, Percentile(allPing, 0.95) / 1000.0, Percentile(allPing, 0.99) / 1000.0,
		(allPing.empty() ? 0.0 : allPing.back() / 1000.0), allPing.size());
	printf("[summary] moveCorrected=%lld pongs=%lld totalRecvBytes=%lld\n",
		GStat.moveCorrected.load(), GStat.pongRecv.load(), GStat.bytesRecv.load());
	if (GConfig.hunt)
		printf("[summary] hunt: monsterAdded=%lld monsterDead(SC_DEAD)=%lld monsterRemoved(SC_REMOVE_OBJECT)=%lld damagePackets=%lld\n",
			GStat.monsterAdded.load(), GStat.monsterDead.load(), GStat.monsterRemoved.load(), GStat.damageRecv.load());

	WSACleanup();
	return 0;
}
