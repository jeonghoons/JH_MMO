#pragma once
// LoadTest(콘솔)와 DummyClient(시각화)가 함께 쓰는 봇 엔진.
// 봇 N명을 램프업으로 접속시켜 로그인 -> 입장 -> (이동/사냥/핑)을 반복하고, 지연·처리량을 집계한다.
// 네트워크는 논블로킹 소켓 + WSAPoll 워커 스레드(봇은 클라이언트 측이라 ServerCore를 쓰지 않는다, DECISIONS 2026-10-30).
#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

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
	bool move = true;          // 무작위 이동
	std::string body = "male";     // 체형. male: dummy 계정 로그인, female: fem_* 계정 가입 후 로그인 (무기는 MC3.2 전까지 없음 = 맨손)
	bool chaos = false;         // 일부 공격 요청을 일부러 잘못된 값으로 보낸다(오류 테스트)
	bool hunt = false;         // 가장 가까운 몬스터에게 이동해 공격 (hunt 프로필)
	bool turn = false;         // turn 프로필(MC4.1b): 직선으로 걷다 turnIntervalMs마다 90° 꺾음 — 원격 이동 표시 측정용(--move-interval 200과 함께)
	int turnIntervalMs = 1500;
	bool turnSend = false;     // --turn-send: 꺾는 순간 모서리 위치를 바로 보내고 새 방향은 60ms 뒤부터(클라 '방향 바뀌면 즉시 송신' 모사, MC4.1b)
	bool aim = false;          // --aim (MC4.2d-1): 활을 장착하고, 1번 스킬은 조준을 켜고 300~800ms 느리게 움직인 뒤 쏜다(10%는 취소). hunt와 함께
	int equipIntervalMs = 0;   // >0이면 이 간격(±50%)마다 무작위 장비 교체 요청, 10%는 바로 한 번 더 보냄(빈도 제한 시험). 장비 저장 측정용(MC3.3)
	std::string outFile;
	std::string serverProc = "GameServer.exe";
	std::string mapLogic;          // 맵 로직 JSON(몬스터 스폰 위치). hunt 프로필의 봇이 가까운 스폰으로 이동하는 데 쓴다
	bool trackWorld = false;   // 시각화(DummyClient)용: 몬스터 위치·사망을 추적하고 스냅숏을 공개한다
	bool zone = false;         // 핫존: 봇이 (zoneX, zoneY)로 걸어가 반경 zoneR 안에서만 움직인다 (--zone x,y,r)
	float zoneX = 0, zoneY = 0, zoneR = 0;
};

// ---------------------------------------------------------------- 집계
struct BotCounters
{
	std::atomic<long long> connectAttempts{ 0 }, connectOk{ 0 }, connectFail{ 0 };
	std::atomic<long long> loginOk{ 0 }, loginFail{ 0 }, unexpectedClose{ 0 }, protocolError{ 0 }, sendDropped{ 0 };
	std::atomic<long long> pktSent{ 0 }, pktRecv{ 0 }, bytesRecv{ 0 }, pongRecv{ 0 }, moveCorrected{ 0 };
	std::atomic<long long> monsterDead{ 0 }, monsterRemoved{ 0 }, monsterAdded{ 0 }, damageRecv{ 0 }, objectAdded{ 0 };
	std::atomic<long long> aimSent{ 0 }, aimRecv{ 0 }, aimOwnOff{ 0 };   // CS_SKILL_AIM / 받은 SC_SKILL_AIM / 내 조준의 거부·시간 초과
	std::atomic<long long> equipSent{ 0 }, equipOk{ 0 }, equipRejected{ 0 };   // 장착 요청 / 내 SC_CHANGE_EQUIP / SC_CHANGE_EQUIP_RESULT
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
extern std::vector<std::pair<float, float>> GMonsterSpawns;   // 맵 로직 JSON에서 읽은 몬스터 스폰 위치

namespace BotEngine
{
	long long NowUs();
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
