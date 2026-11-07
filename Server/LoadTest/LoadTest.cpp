// 헤드리스 부하 테스트 클라이언트. 봇 동작은 BotEngine이 맡고, 여기서는 지연/처리량/서버 CPU를 CSV와 콘솔로 기록한다.
// 사용법: LoadTest.exe --bots 1000 --ramp-rate 100 --duration 60 [--host 127.0.0.1] [--port 8888]
//         [--threads 4] [--move-interval 1000] [--ping-interval 1000] [--profile move|idle|hunt] [--out loadtest.csv]
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include "../BotEngine/BotEngine.h"

using namespace std;
using BotEngine::NowUs;
using BotEngine::Percentile;

// 이 프로세스(봇)의 CPU 사용률(코어 1개 = 100%). 봇 쪽이 한계에 닿았는지 서버 병목과 구분하는 데 쓴다.
static double SelfCpuPct()
{
	static unsigned long long lastCpu = 0;
	static long long lastWallUs = 0;
	FILETIME c, e, k, u;
	if (!GetProcessTimes(GetCurrentProcess(), &c, &e, &k, &u)) return 0.0;
	const unsigned long long cpu100ns = ((unsigned long long)k.dwHighDateTime << 32 | k.dwLowDateTime) +
		((unsigned long long)u.dwHighDateTime << 32 | u.dwLowDateTime);
	const long long wallUs = NowUs();
	double pct = 0.0;
	if (lastWallUs != 0 && wallUs > lastWallUs)
		pct = (cpu100ns - lastCpu) / 10.0 / (wallUs - lastWallUs) * 100.0;
	lastCpu = cpu100ns;
	lastWallUs = wallUs;
	return pct;
}

int main(int argc, char** argv)
{
	if (!BotEngine::ParseArgs(argc, argv)) return 1;

	if (GConfig.outFile.empty())
	{
		char name[64];
		snprintf(name, sizeof(name), "loadtest_%lld.csv", static_cast<long long>(time(nullptr)));
		GConfig.outFile = name;
	}

	printf("[loadtest] host=%s:%d bots=%d ramp=%d/s duration=%ds threads=%d profile=%s body=%s chaos=%d out=%s\n",
		GConfig.host.c_str(), GConfig.port, GConfig.bots, GConfig.rampRate, GConfig.durationSec,
		GConfig.threads, GConfig.hunt ? "hunt" : (GConfig.move ? "move" : "idle"), GConfig.body.c_str(), GConfig.chaos ? 1 : 0, GConfig.outFile.c_str());

	BotEngine::Start();

	ServerProcSampler proc;
	if (!proc.Open(GConfig.serverProc)) printf("[loadtest] server process '%s' not found; server CPU/memory columns will be 0\n", GConfig.serverProc.c_str());

	ofstream csv(GConfig.outFile);
	csv << "t_sec,connected,in_game,connect_fail,unexpected_close,login_p50_ms,login_p95_ms,ping_p50_ms,ping_p95_ms,ping_p99_ms,ping_max_ms,"
		"sent_ps,recv_ps,recv_kbps,move_corrected,server_cpu_pct,server_mem_mb,max_bot_recv_KBps,bot_cpu_pct\n";

	vector<uint32_t> allPing, allLogin;
	double peakBotKBps = 0.0;   // 구간별 봇 1명 최대 수신량 중 가장 큰 값
	const long long startUs = NowUs();
	long long lastReportUs = startUs;
	long long lastSent = 0, lastRecv = 0, lastBytes = 0, lastCorrected = 0;
	constexpr int REPORT_SEC = 5;
	long long nextReportUs = startUs + REPORT_SEC * 1000000LL;

	while (true)
	{
		this_thread::sleep_for(chrono::milliseconds(100));
		const long long now = NowUs();
		const double elapsed = BotEngine::ElapsedSec();

		BotEngine::UpdateRamp();

		const bool finished = elapsed >= GConfig.durationSec;
		if (now < nextReportUs && !finished) continue;

		// 구간 샘플 수집
		vector<uint32_t> ping, login;
		BotEngine::TakeSamples(ping, login);
		allPing.insert(allPing.end(), ping.begin(), ping.end());
		allLogin.insert(allLogin.end(), login.begin(), login.end());
		sort(ping.begin(), ping.end());
		sort(login.begin(), login.end());

		const double dt = max(0.001, (now - lastReportUs) / 1e6);
		long long sent = GStat.pktSent.load(), recv = GStat.pktRecv.load(), bytes = GStat.bytesRecv.load(), corrected = GStat.moveCorrected.load();
		double cpu, mem;
		proc.Sample(cpu, mem);
		const double botCpu = SelfCpuPct();
		// 이번 구간에 가장 많이 받은 봇 한 명의 수신량(KB/s). 읽고 나서 구간을 넘긴다.
		const double maxBotKBps = GStat.maxBotBytesWindow.exchange(0) / dt / 1024.0;
		GStat.windowEpoch.fetch_add(1);
		peakBotKBps = max(peakBotKBps, maxBotKBps);

		char line[512];
		snprintf(line, sizeof(line),
			"%.0f,%d,%d,%lld,%lld,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.0f,%.0f,%.0f,%lld,%.0f,%.0f,%.1f,%.0f",
			elapsed, GStat.connected.load(), GStat.inGame.load(), GStat.connectFail.load(), GStat.unexpectedClose.load(),
			Percentile(login, 0.50) / 1000.0, Percentile(login, 0.95) / 1000.0,
			Percentile(ping, 0.50) / 1000.0, Percentile(ping, 0.95) / 1000.0, Percentile(ping, 0.99) / 1000.0,
			(ping.empty() ? 0.0 : ping.back() / 1000.0),
			(sent - lastSent) / dt, (recv - lastRecv) / dt, (bytes - lastBytes) / dt / 1024.0 * 8.0,
			corrected - lastCorrected, cpu, mem, maxBotKBps, botCpu);
		csv << line << "\n";
		csv.flush();

		printf("[t=%3.0fs] connected=%d inGame=%d connFail=%lld closed=%lld | ping p50/p95/p99/max=%.1f/%.1f/%.1f/%.1f ms | sent/s=%.0f recv/s=%.0f | server cpu=%.0f%% mem=%.0fMB | maxBot=%.1fKB/s botCpu=%.0f%%\n",
			elapsed, GStat.connected.load(), GStat.inGame.load(), GStat.connectFail.load(), GStat.unexpectedClose.load(),
			Percentile(ping, 0.50) / 1000.0, Percentile(ping, 0.95) / 1000.0, Percentile(ping, 0.99) / 1000.0,
			(ping.empty() ? 0.0 : ping.back() / 1000.0), (sent - lastSent) / dt, (recv - lastRecv) / dt, cpu, mem, maxBotKBps, botCpu);

		lastReportUs = now; lastSent = sent; lastRecv = recv; lastBytes = bytes; lastCorrected = corrected;
		nextReportUs = now + REPORT_SEC * 1000000LL;

		if (finished) break;
	}

	BotEngine::Stop();

	sort(allPing.begin(), allPing.end());
	sort(allLogin.begin(), allLogin.end());
	printf("\n[summary] attempts=%lld ok=%lld fail=%lld loginOk=%lld loginFail=%lld unexpectedClose=%lld protocolError=%lld sendDropped=%lld\n",
		GStat.connectAttempts.load(), GStat.connectOk.load(), GStat.connectFail.load(), GStat.loginOk.load(), GStat.loginFail.load(),
		GStat.unexpectedClose.load(), GStat.protocolError.load(), GStat.sendDropped.load());
	printf("[summary] login   p50/p95/p99/max = %.1f/%.1f/%.1f/%.1f ms (n=%zu)\n",
		Percentile(allLogin, 0.50) / 1000.0, Percentile(allLogin, 0.95) / 1000.0, Percentile(allLogin, 0.99) / 1000.0,
		(allLogin.empty() ? 0.0 : allLogin.back() / 1000.0), allLogin.size());
	printf("[summary] ping    p50/p95/p99/max = %.1f/%.1f/%.1f/%.1f ms (n=%zu)\n",
		Percentile(allPing, 0.50) / 1000.0, Percentile(allPing, 0.95) / 1000.0, Percentile(allPing, 0.99) / 1000.0,
		(allPing.empty() ? 0.0 : allPing.back() / 1000.0), allPing.size());
	printf("[summary] moveCorrected=%lld pongs=%lld totalRecvBytes=%lld peakBotRecv=%.1fKB/s\n",
		GStat.moveCorrected.load(), GStat.pongRecv.load(), GStat.bytesRecv.load(), peakBotKBps);
	if (GConfig.hunt)
		printf("[summary] hunt: monsterAdded=%lld monsterDead(SC_DEAD)=%lld monsterRemoved(SC_REMOVE_OBJECT)=%lld damagePackets=%lld\n",
			GStat.monsterAdded.load(), GStat.monsterDead.load(), GStat.monsterRemoved.load(), GStat.damageRecv.load());

	return 0;
}
