#include "pch.h"
#include "Utils/Metrics.h"
#include "Network/NetStats.h"
#include "Game/Room/Room.h"
#include <fstream>
#include <filesystem>
#include <thread>

namespace
{
	constexpr int REPORT_INTERVAL_SEC = 5;

	// 정렬된 표본에서 p(0~1) 분위수 (nearest-rank)
	double Rank(const std::vector<long long>& sorted, double p)
	{
		if (sorted.empty()) return 0.0;
		size_t idx = static_cast<size_t>(std::ceil(p * sorted.size()));
		idx = std::clamp<size_t>(idx, 1, sorted.size()) - 1;
		return static_cast<double>(sorted[idx]);
	}

	constexpr const char* CSV_PATH = "metrics.csv";
	constexpr const char* CSV_HEADER =
		"run_id,time,sessions,rooms,players,packets_in_ps,messages_out_ps,tick_late_avg_ms,tick_late_max_ms,tick_body_avg_ms,tick_body_max_ms,"
		"send_calls_ps,bufs_per_send,tick_late_p99_ms,tick_body_p99_ms,sendq_session_max";

	// metrics.csv를 이어쓰기로 연다. 헤더는 파일이 없거나 비었을 때만 쓴다(재시작마다 헤더가 다시 들어가던 문제 — P1a).
	// 열 구성이 다른 옛 파일은 이름을 바꿔 남기고 새로 시작한다. 실행은 run_id 열(= 로그 파일의 시작 시각)로 구분한다.
	std::ofstream OpenCsv()
	{
		namespace fs = std::filesystem;
		std::error_code ec;
		bool needHeader = true;
		if (fs::exists(CSV_PATH, ec) && fs::file_size(CSV_PATH, ec) > 0)
		{
			std::string firstLine;
			{
				std::ifstream in(CSV_PATH);
				std::getline(in, firstLine);
			}
			if (!firstLine.empty() && firstLine.back() == '\r') firstLine.pop_back();

			if (firstLine == CSV_HEADER)
			{
				needHeader = false;
			}
			else
			{
				const std::string oldName = std::format("metrics_old_{}.csv", Logger::StartStamp());
				fs::rename(CSV_PATH, oldName, ec);
				if (ec)
					LOG_WARN("metrics", "metrics.csv의 열 구성이 다른데 이름을 바꾸지 못했습니다({}) — 그대로 이어 씁니다", ec.message());
				else
					LOG_INFO("metrics", "metrics.csv의 열 구성이 달라 {}로 옮기고 새로 시작합니다", oldName);
			}
		}

		std::ofstream csv(CSV_PATH, std::ios::app);
		if (needHeader)
			csv << CSV_HEADER << "\n";
		return csv;
	}

	void ReportLoop()
	{
		std::ofstream csv = OpenCsv();
		const std::string runId = Logger::StartStamp();

		long long prevIn = NetStats::PacketsIn.load();
		long long prevOut = NetStats::MessagesOut.load();
		long long prevSendCalls = NetStats::SendCalls.load();
		long long prevSendBufs = NetStats::SendBufs.load();
		long long prevAtkReq = Metrics::AttackRequests.load();
		long long prevAtkOk = Metrics::AttackAccepted.load();

		while (true)
		{
			std::this_thread::sleep_for(std::chrono::seconds(REPORT_INTERVAL_SEC));

			long long nowIn = NetStats::PacketsIn.load();
			long long nowOut = NetStats::MessagesOut.load();
			double inPs = (nowIn - prevIn) / static_cast<double>(REPORT_INTERVAL_SEC);
			double outPs = (nowOut - prevOut) / static_cast<double>(REPORT_INTERVAL_SEC);
			prevIn = nowIn;
			prevOut = nowOut;

			// WSASend 호출 수/s와 호출 한 번에 실린 메시지 수(WSABUF 평균) — 송신 묶기(P1b)의 효과 지표
			const long long nowSendCalls = NetStats::SendCalls.load();
			const long long nowSendBufs = NetStats::SendBufs.load();
			const double sendCallsPs = (nowSendCalls - prevSendCalls) / static_cast<double>(REPORT_INTERVAL_SEC);
			const double bufsPerSend = (nowSendCalls > prevSendCalls)
				? (nowSendBufs - prevSendBufs) / static_cast<double>(nowSendCalls - prevSendCalls) : 0.0;
			prevSendCalls = nowSendCalls;
			prevSendBufs = nowSendBufs;

			const long long atkReq = Metrics::AttackRequests.load();
			const long long atkOk = Metrics::AttackAccepted.load();
			const double atkReqPs = (atkReq - prevAtkReq) / static_cast<double>(REPORT_INTERVAL_SEC);
			const double atkOkPs = (atkOk - prevAtkOk) / static_cast<double>(REPORT_INTERVAL_SEC);
			prevAtkReq = atkReq;
			prevAtkOk = atkOk;

			int rooms = 0;
			int players = 0;
			{
				RWLock::ReadGuard lock(GRoomManager->_lock);
				rooms = static_cast<int>(GRoomManager->_rooms.size());
				for (const auto& [id, room] : GRoomManager->_rooms)
					players += room->NumPlayers();
			}

			// 틱 표본은 us 단위 → ms로 출력
			const Metrics::Percentiles late = Metrics::TickLate.TakeAndReset();
			const Metrics::Percentiles body = Metrics::TickBody.TakeAndReset();
			const Metrics::Percentiles viewP = Metrics::ViewPlayers.TakeAndReset();
			const Metrics::Percentiles viewO = Metrics::ViewObjects.TakeAndReset();
			const int sendQueueMax = NetStats::SendQueueMax.exchange(0);
			const Metrics::Percentiles itExec = Metrics::ItemOpExecUs.TakeAndReset();
			const Metrics::Percentiles itTotal = Metrics::ItemOpTotalUs.TakeAndReset();
			const Metrics::Percentiles eqApply = Metrics::EquipApplyUs.TakeAndReset();
			const Metrics::Percentiles pgExec = Metrics::ProgressSaveExecUs.TakeAndReset();
			const Metrics::Percentiles pgTotal = Metrics::ProgressSaveTotalUs.TakeAndReset();

			LOG_INFO("metrics",
				"sessions={} rooms={} players={} monsters={} in/s={:.0f} out/s={:.0f} sendCalls/s={:.0f} bufsPerSend={:.2f} atk(req/ok per s)={:.1f}/{:.1f} zones(total created/hits)={}/{} "
				"move(total ok/speed/nav/bad/stale/rate)={}/{}/{}/{}/{}/{} "
				"tickLate(avg/p50/p95/p99/max ms)={:.1f}/{:.1f}/{:.1f}/{:.1f}/{:.1f} tickBody(avg/p50/p95/p99/max ms)={:.2f}/{:.2f}/{:.2f}/{:.2f}/{:.2f} "
				"viewPlayers(avg/p99/max)={:.1f}/{:.0f}/{:.0f} viewObjects(avg/p99/max)={:.1f}/{:.0f}/{:.0f} sendQ(total/sessionMax)={}/{} logDrop={} "
				"viewAudit(must/missing mustNot/extra)={}/{} {}/{} aoi(nodes/depth)={}/{} "
				"itemOp(total sent/fail/busy inflight)={}/{}/{} {} itemOpMs(n exec avg/p99/max total avg/p99/max)={} {:.1f}/{:.1f}/{:.1f} {:.1f}/{:.1f}/{:.1f} "
				"equipApplyMs(n avg/p50/p99/max)={} {:.1f}/{:.1f}/{:.1f}/{:.1f} "
				"progressSave(total sent/coalesced/fail inflight)={}/{}/{} {} progressSaveMs(n exec avg/p99/max total avg/p99/max)={} {:.1f}/{:.1f}/{:.1f} {:.1f}/{:.1f}/{:.1f} "
				"aim(total on/reject/off/castClear/timeout)={}/{}/{}/{}/{} "
				"revive(total deaths/ok/reject/deadMove)={}/{}/{}/{} portal(total ok/reject)={}/{} "
				"login(total kick/busy/forced)={}/{}/{} "
				"drops(now kills/created/expired/evicted)={} {}/{}/{}/{} pickup(picked/discarded)={}/{} trades={}",
				NetStats::Sessions.load(), rooms, players, Metrics::Monsters.load(), inPs, outPs, sendCallsPs, bufsPerSend, atkReqPs, atkOkPs, Metrics::ZonesCreated.load(), Metrics::ZoneHits.load(),
				Metrics::MoveAccepted.load(), Metrics::MoveRejectSpeed.load(), Metrics::MoveRejectNav.load(), Metrics::MoveRejectInvalid.load(),
				Metrics::MoveDroppedStale.load(), Metrics::MoveDroppedRate.load(),
				late.avg / 1000.0, late.p50 / 1000.0, late.p95 / 1000.0, late.p99 / 1000.0, late.max / 1000.0,
				body.avg / 1000.0, body.p50 / 1000.0, body.p95 / 1000.0, body.p99 / 1000.0, body.max / 1000.0,
				viewP.avg, viewP.p99, viewP.max, viewO.avg, viewO.p99, viewO.max,
				NetStats::SendQueued.load(), sendQueueMax, Logger::Dropped(),
				Metrics::ViewAuditMust.load(), Metrics::ViewAuditMissing.load(), Metrics::ViewAuditMustNot.load(), Metrics::ViewAuditExtra.load(),
				Metrics::AoiNodes.exchange(0), Metrics::AoiDepth.exchange(0),
				Metrics::ItemOpSent.load(), Metrics::ItemOpFailed.load(), Metrics::ItemOpBusy.load(),
				Metrics::ItemOpInFlight.load(), itExec.count,
				itExec.avg / 1000.0, itExec.p99 / 1000.0, itExec.max / 1000.0, itTotal.avg / 1000.0, itTotal.p99 / 1000.0, itTotal.max / 1000.0,
				eqApply.count, eqApply.avg / 1000.0, eqApply.p50 / 1000.0, eqApply.p99 / 1000.0, eqApply.max / 1000.0,
				Metrics::ProgressSaveSent.load(), Metrics::ProgressSaveCoalesced.load(), Metrics::ProgressSaveFailed.load(),
				Metrics::ProgressSaveInFlight.load(), pgExec.count,
				pgExec.avg / 1000.0, pgExec.p99 / 1000.0, pgExec.max / 1000.0, pgTotal.avg / 1000.0, pgTotal.p99 / 1000.0, pgTotal.max / 1000.0,
				Metrics::AimOn.load(), Metrics::AimReject.load(), Metrics::AimOff.load(), Metrics::AimCastClear.load(), Metrics::AimTimeout.load(),
				Metrics::PlayerDeaths.load(), Metrics::Revives.load(), Metrics::ReviveReject.load(), Metrics::MoveDroppedDead.load(), Metrics::PortalUsed.load(), Metrics::PortalReject.load(),
				Metrics::LoginKicks.load(), Metrics::LoginBusy.load(), Metrics::LoginCloseForced.load(),
				Metrics::Drops.load(), Metrics::MonsterKills.load(), Metrics::DropsCreated.load(), Metrics::DropsExpired.load(), Metrics::DropsEvicted.load(),
				Metrics::DropsPicked.load(), Metrics::DropsDiscarded.load(), Metrics::TradesDone.load());

			auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
			csv << runId << "," << now << "," << NetStats::Sessions.load() << "," << rooms << "," << players << ","
				<< inPs << "," << outPs << "," << late.avg / 1000.0 << "," << late.max / 1000.0 << ","
				<< body.avg / 1000.0 << "," << body.max / 1000.0 << ","
				<< sendCallsPs << "," << bufsPerSend << "," << late.p99 / 1000.0 << "," << body.p99 / 1000.0 << "," << sendQueueMax << "\n";
			csv.flush();
		}
	}
}

Metrics::Percentiles Metrics::SampleStat::TakeAndReset()
{
	std::vector<long long> samples;
	{
		std::lock_guard<std::mutex> lock(_mutex);
		samples.swap(_samples);
	}

	Percentiles r;
	if (samples.empty()) return r;
	std::sort(samples.begin(), samples.end());
	long long sum = 0;
	for (long long v : samples) sum += v;
	r.count = static_cast<long long>(samples.size());
	r.avg = sum / static_cast<double>(samples.size());
	r.p50 = Rank(samples, 0.50);
	r.p95 = Rank(samples, 0.95);
	r.p99 = Rank(samples, 0.99);
	r.max = static_cast<double>(samples.back());
	return r;
}

void Metrics::StartReporter()
{
	std::thread(ReportLoop).detach();
}
