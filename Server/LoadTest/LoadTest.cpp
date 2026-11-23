// 헤드리스 부하 테스트 클라이언트. 봇 동작은 BotEngine이 맡고, 여기서는 지연/처리량/서버 CPU를 CSV와 콘솔로 기록한다.
// 사용법: LoadTest.exe --bots 1000 --ramp-rate 100 --duration 60 [--host 127.0.0.1] [--port 8888]
//         [--threads 4] [--move-interval 1000] [--ping-interval 1000] [--profile move|idle|hunt|turn] [--out loadtest.csv]
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

	printf("[loadtest] host=%s:%d bots=%d ramp=%d/s duration=%ds threads=%d profile=%s login=%s body=%s chaos=%d out=%s\n",
		GConfig.host.c_str(), GConfig.port, GConfig.bots, GConfig.rampRate, GConfig.durationSec,
		GConfig.threads, GConfig.mixText.c_str(), GConfig.accountLogin ? "account" : "dummy", GConfig.body.c_str(), GConfig.chaos ? 1 : 0, GConfig.outFile.c_str());

	BotEngine::Start();

	ServerProcSampler proc;
	if (!proc.Open(GConfig.serverProc)) printf("[loadtest] server process '%s' not found; server CPU/memory columns will be 0\n", GConfig.serverProc.c_str());

	ofstream csv(GConfig.outFile);
	csv << "t_sec,connected,in_game,connect_fail,unexpected_close,login_p50_ms,login_p95_ms,ping_p50_ms,ping_p95_ms,ping_p99_ms,ping_max_ms,"
		"sent_ps,recv_ps,recv_kbps,move_corrected,server_cpu_pct,server_mem_mb,max_bot_recv_KBps,bot_cpu_pct,room_change,move_rejected\n";

	vector<uint32_t> allPing, allLogin;
	double peakBotKBps = 0.0;   // 구간별 봇 1명 최대 수신량 중 가장 큰 값
	const long long startUs = NowUs();
	long long lastReportUs = startUs;
	long long lastSent = 0, lastRecv = 0, lastBytes = 0, lastCorrected = 0, lastRoomChange = 0, lastMoveRejected = 0;
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
		const long long roomChange = GStat.roomChange.load(), moveRejected = GStat.moveRejected.load();
		double cpu, mem;
		proc.Sample(cpu, mem);
		const double botCpu = SelfCpuPct();
		// 이번 구간에 가장 많이 받은 봇 한 명의 수신량(KB/s). 읽고 나서 구간을 넘긴다.
		const double maxBotKBps = GStat.maxBotBytesWindow.exchange(0) / dt / 1024.0;
		GStat.windowEpoch.fetch_add(1);
		peakBotKBps = max(peakBotKBps, maxBotKBps);

		char line[512];
		snprintf(line, sizeof(line),
			"%.0f,%d,%d,%lld,%lld,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.0f,%.0f,%.0f,%lld,%.0f,%.0f,%.1f,%.0f,%lld,%lld",
			elapsed, GStat.connected.load(), GStat.inGame.load(), GStat.connectFail.load(), GStat.unexpectedClose.load(),
			Percentile(login, 0.50) / 1000.0, Percentile(login, 0.95) / 1000.0,
			Percentile(ping, 0.50) / 1000.0, Percentile(ping, 0.95) / 1000.0, Percentile(ping, 0.99) / 1000.0,
			(ping.empty() ? 0.0 : ping.back() / 1000.0),
			(sent - lastSent) / dt, (recv - lastRecv) / dt, (bytes - lastBytes) / dt / 1024.0 * 8.0,
			corrected - lastCorrected, cpu, mem, maxBotKBps, botCpu, roomChange - lastRoomChange, moveRejected - lastMoveRejected);
		csv << line << "\n";
		csv.flush();

		printf("[t=%3.0fs] connected=%d inGame=%d connFail=%lld closed=%lld | ping p50/p95/p99/max=%.1f/%.1f/%.1f/%.1f ms | sent/s=%.0f recv/s=%.0f | server cpu=%.0f%% mem=%.0fMB | maxBot=%.1fKB/s botCpu=%.0f%%\n",
			elapsed, GStat.connected.load(), GStat.inGame.load(), GStat.connectFail.load(), GStat.unexpectedClose.load(),
			Percentile(ping, 0.50) / 1000.0, Percentile(ping, 0.95) / 1000.0, Percentile(ping, 0.99) / 1000.0,
			(ping.empty() ? 0.0 : ping.back() / 1000.0), (sent - lastSent) / dt, (recv - lastRecv) / dt, cpu, mem, maxBotKBps, botCpu);

		lastReportUs = now; lastSent = sent; lastRecv = recv; lastBytes = bytes; lastCorrected = corrected;
		lastRoomChange = roomChange; lastMoveRejected = moveRejected;
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
	printf("[summary] nav: pathSearch=%lld pathFail=%lld\n", GStat.pathSearch.load(), GStat.pathFail.load());
	// A5.2 프로필별 인원·로그인 재시도·행동 집계
	printf("[summary] profiles:");
	for (int k = 0; k < static_cast<int>(BotProfile::Count); ++k)
		if (GStat.profileBots[k].load() > 0) printf(" %s=%d", BotEngine::ProfileName(static_cast<BotProfile>(k)), GStat.profileBots[k].load());
	printf("\n");
	{
		static const char* const kLoginName[8] = { "NONE", "SUCCESS", "NO_ACCOUNT", "WRONG_PASSWORD", "ALREADY_LOGGED_IN", "SERVER_ERROR", "BUSY", "7" };
		printf("[summary] login: retry=%lld giveUp=%lld timeout=%lld fail by LoginResult:", GStat.loginRetry.load(), GStat.loginGiveUp.load(), GStat.loginTimeout.load());
		for (int i = 0; i < 8; ++i)
			if (GStat.loginFailBy[i].load() > 0) printf(" %s=%lld", kLoginName[i], GStat.loginFailBy[i].load());
		printf("\n");
	}
	if (GConfig.hunt)
		printf("[summary] lock: moveHeldByLock=%lld\n", GStat.moveHeldByLock.load());
	if (GStat.chatSent.load() > 0 || GStat.chatRecv.load() > 0)
		printf("[summary] chat: sent=%lld recv(SC_CHAT)=%lld\n", GStat.chatSent.load(), GStat.chatRecv.load());
	if (GStat.dropSeen.load() > 0 || GStat.pickupSent.load() > 0)
		printf("[summary] loot: dropSeen=%lld pickupSent=%lld pickupOk=%lld dropItemSent=%lld\n",
			GStat.dropSeen.load(), GStat.pickupSent.load(), GStat.pickupOk.load(), GStat.dropItemSent.load());
	if (GStat.tradeRequestSent.load() > 0 || GStat.tradeRequestRecv.load() > 0)
	{
		static const char* const kTradeName[24] = { "NONE", "SUCCESS", "NO_TARGET", "TOO_FAR", "TARGET_BUSY", "DECLINED", "TIMEOUT", "CANCELLED", "DEAD", "DISCONNECTED",
			"LOCKED", "INVALID_OFFER", "INVENTORY_FULL", "GOLD_FULL", "BUSY", "NOT_ALLOWED", "DB_ERROR", "MOVED", "18", "19", "20", "21", "22", "23" };
		printf("[summary] trade: requestSent=%lld requestRecv=%lld offerSent=%lld confirmSent=%lld cancelSent=%lld end=%lld\n",
			GStat.tradeRequestSent.load(), GStat.tradeRequestRecv.load(), GStat.tradeOfferSent.load(), GStat.tradeConfirmSent.load(), GStat.tradeCancelSent.load(), GStat.tradeEnd.load());
		printf("[summary] trade end by TradeResult:");
		for (int i = 0; i < 24; ++i)
			if (GStat.tradeEndBy[i].load() > 0) printf(" %s=%lld", kTradeName[i], GStat.tradeEndBy[i].load());
		printf("\n[summary] trade rejected by TradeResult:");
		for (int i = 0; i < 24; ++i)
			if (GStat.tradeRejectBy[i].load() > 0) printf(" %s=%lld", kTradeName[i], GStat.tradeRejectBy[i].load());
		static const char* const kTradeReq[8] = { "REQUEST", "RESPOND", "SET_OFFER", "LOCK", "CONFIRM", "CANCEL", "6", "7" };
		printf("\n[summary] trade rejected by request:");
		for (int i = 0; i < 8; ++i)
			if (GStat.tradeRejectReq[i].load() > 0) printf(" %s=%lld", kTradeReq[i], GStat.tradeRejectReq[i].load());
		printf("\n");
	}
	if (GStat.auctionSent[0].load() + GStat.auctionSent[4].load() > 0)
	{
		static const char* const kAuctionReq[6] = { "search", "register", "buy", "cancel", "my", "claim" };
		static const char* const kAuctionName[32] = { "NONE", "SUCCESS", "NOT_IN_ROOM", "DEAD", "BUSY", "TOO_FREQUENT", "NOT_ALLOWED", "NO_LISTING", "SOLD", "OWN_ITEM",
			"NOT_ENOUGH_GOLD", "LISTING_LIMIT", "BAD_PRICE", "NO_ITEM", "IN_TRADE", "INVENTORY_FULL", "GOLD_FULL", "DB_ERROR", "TOO_FAR", "IN_DUNGEON", "TRY_LATER", "NO_MAIL",
			"NOT_ENOUGH_COUNT", "LISTING_BUSY", "ALREADY_CLOSED", "25", "26", "27", "28", "29", "30", "31" };
		printf("[summary] auction sent/ok:");
		for (int i = 0; i < 6; ++i) printf(" %s=%lld/%lld", kAuctionReq[i], GStat.auctionSent[i].load(), GStat.auctionOk[i].load());
		printf(" listRecv=%lld myRecv=%lld mailNotice=%lld\n[summary] auction result by AuctionResult:", GStat.auctionListRecv.load(), GStat.auctionMyRecv.load(), GStat.mailNoticeRecv.load());
		for (int i = 0; i < 32; ++i)
			if (GStat.auctionResultBy[i].load() > 0) printf(" %s=%lld", kAuctionName[i], GStat.auctionResultBy[i].load());
		printf("\n");
	}
	if (GConfig.hunt)
		printf("[summary] hunt: monsterAdded=%lld monsterDead(SC_DEAD)=%lld monsterRemoved(SC_REMOVE_OBJECT)=%lld damagePackets=%lld\n",
			GStat.monsterAdded.load(), GStat.monsterDead.load(), GStat.monsterRemoved.load(), GStat.damageRecv.load());
	if (GStat.attackSent.load() > 0)
	{
		printf("[summary] attack: sent=%lld ok(own SC_ATTACK)=%lld rejected(SC_ATTACK_RESULT)=%lld by AttackResult:",
			GStat.attackSent.load(), GStat.attackOk.load(), GStat.attackRejected.load());
		for (int i = 0; i < 16; ++i)
			if (GStat.attackRejectedBy[i].load() > 0) printf(" %d=%lld", i, GStat.attackRejectedBy[i].load());
		printf("\n");
	}
	if (GStat.mpChangeRecv.load() > 0 || GStat.expRecv.load() > 0)
		printf("[summary] growth: mpChangeRecv=%lld expRecv=%lld\n", GStat.mpChangeRecv.load(), GStat.expRecv.load());
	if (GStat.selfDead.load() > 0 || GStat.reviveSent.load() > 0)
		printf("[summary] revive: selfDead(own SC_DEAD)=%lld reviveSent(CS_REVIVE)=%lld selfRevived(own SC_REVIVE)=%lld\n",
			GStat.selfDead.load(), GStat.reviveSent.load(), GStat.selfRevived.load());
	if (GConfig.aim)
		printf("[summary] aim: sent=%lld recv(SC_SKILL_AIM)=%lld ownOff(reject/timeout)=%lld\n",
			GStat.aimSent.load(), GStat.aimRecv.load(), GStat.aimOwnOff.load());
	if (GConfig.equipIntervalMs > 0)
	{
		printf("[summary] equip: sent=%lld ok(own SC_CHANGE_EQUIP)=%lld rejected(SC_CHANGE_EQUIP_RESULT)=%lld\n",
			GStat.equipSent.load(), GStat.equipOk.load(), GStat.equipRejected.load());
		printf("[summary] equip rejected by EquipResult:");
		for (int i = 0; i < 16; ++i)
			if (GStat.equipRejectedBy[i].load() > 0) printf(" %d=%lld", i, GStat.equipRejectedBy[i].load());
		printf("\n");
	}
	if (GStat.inventoryRecv.load() > 0 || GStat.giveSent.load() > 0)
	{
		printf("[summary] inventory: SC_INVENTORY=%lld SC_INVENTORY_UPDATE=%lld giveSent=%lld slotMoveSent=%lld itemRejected(SC_ITEM_RESULT)=%lld by ItemResult:",
			GStat.inventoryRecv.load(), GStat.inventoryUpdateRecv.load(), GStat.giveSent.load(), GStat.slotMoveSent.load(), GStat.itemRejected.load());
		for (int i = 0; i < 24; ++i)
			if (GStat.itemRejectedBy[i].load() > 0) printf(" %d=%lld", i, GStat.itemRejectedBy[i].load());
		printf("\n");
		if (GStat.shopBuySent.load() > 0 || GStat.shopSellSent.load() > 0)
			printf("[summary] shop: buySent=%lld sellSent=%lld\n", GStat.shopBuySent.load(), GStat.shopSellSent.load());
	}
	if (GStat.partyInviteSent.load() > 0 || GStat.partyStateRecv.load() > 0)
	{
		printf("[summary] party: inParty(now)=%d inviteSent=%lld inviteRecv(accepted)=%lld stateRecv=%lld leaveSent=%lld result(SC_PARTY_RESULT)=%lld by PartyResult:",
			GStat.partyIn.load(), GStat.partyInviteSent.load(), GStat.partyInviteRecv.load(), GStat.partyStateRecv.load(), GStat.partyLeaveSent.load(), GStat.partyResult.load());
		static const char* const kPartyResultName[16] = { "NONE", "NO_TARGET", "OTHER_CHANNEL", "TARGET_IN_PARTY", "TARGET_BUSY", "PARTY_FULL", "NOT_LEADER", "IN_DUNGEON",
			"DECLINED", "TIMEOUT", "NO_INVITE", "NOT_IN_PARTY", "TOO_FREQUENT", "13", "14", "15" };
		for (int i = 0; i < 16; ++i)
			if (GStat.partyResultBy[i].load() > 0) printf(" %s=%lld", kPartyResultName[i], GStat.partyResultBy[i].load());
		printf("\n");
	}
	if (GStat.portalSent.load() > 0)
		printf("[summary] portal: sent=%lld moved(SC_TELEPORT)=%lld\n", GStat.portalSent.load(), GStat.portalMoved.load());
	if (GStat.dungeonEnterSent.load() > 0 || GStat.dungeonEntered.load() > 0)
		printf("[summary] dungeon: enterSent=%lld entered=%lld exitSent=%lld cleared=%lld inDungeonNow=%d\n",
			GStat.dungeonEnterSent.load(), GStat.dungeonEntered.load(), GStat.dungeonExitSent.load(), GStat.dungeonCleared.load(), GStat.inDungeon.load());
	if (GStat.roomChange.load() > 0 || GStat.moveRejected.load() > 0)
	{
		printf("[summary] room: roomChange(SC_ROOM_CHANGE)=%lld moveRejected(SC_MOVE_RESULT)=%lld by MoveResult:",
			GStat.roomChange.load(), GStat.moveRejected.load());
		static const char* const kMoveResultName[8] = { "NONE", "DUNGEON_FULL", "NOT_LEADER", "DUNGEON_CLEARED", "TIMEOUT", "NOT_AVAILABLE", "6", "7" };
		for (int i = 0; i < 8; ++i)
			if (GStat.moveRejectedBy[i].load() > 0) printf(" %s=%lld", kMoveResultName[i], GStat.moveRejectedBy[i].load());
		printf("\n");
	}

	return 0;
}
