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
	std::string hero = "warrior";   // warrior: 논타겟 일반공격, archer: 타겟팅 일반공격(가입 후 로그인)
	bool chaos = false;         // 일부 공격 요청을 일부러 잘못된 값으로 보낸다(오류 테스트)
	bool hunt = false;         // 가장 가까운 몬스터에게 이동해 공격 (hunt 프로필)
	std::string outFile;
	std::string serverProc = "GameServer.exe";
	std::string mapLogic;          // 맵 로직 JSON(몬스터 스폰 위치). hunt 프로필의 봇이 가까운 스폰으로 이동하는 데 쓴다
	bool trackWorld = false;   // 시각화(DummyClient)용: 몬스터 위치·사망을 추적하고 스냅숏을 공개한다
};

// ---------------------------------------------------------------- 집계
struct BotCounters
{
	std::atomic<long long> connectAttempts{ 0 }, connectOk{ 0 }, connectFail{ 0 };
	std::atomic<long long> loginOk{ 0 }, loginFail{ 0 }, unexpectedClose{ 0 }, protocolError{ 0 }, sendDropped{ 0 };
	std::atomic<long long> pktSent{ 0 }, pktRecv{ 0 }, bytesRecv{ 0 }, pongRecv{ 0 }, moveCorrected{ 0 };
	std::atomic<long long> monsterDead{ 0 }, monsterRemoved{ 0 }, monsterAdded{ 0 }, damageRecv{ 0 }, objectAdded{ 0 };
	std::atomic<int> connected{ 0 }, inGame{ 0 };
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
