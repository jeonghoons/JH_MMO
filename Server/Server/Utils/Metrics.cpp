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
		"send_calls_ps,bufs_per_send,tick_late_p99_ms,tick_body_p99_ms,sendq_session_max,"
		"send_flush_ps,send_continue_ps,send_direct_ps,flush_gap_p50_ms,flush_gap_p90_ms,flush_gap_p99_ms,"
		"send_wake_ps,send_wake_late_avg_ms,send_wake_late_p99_ms,send_wake_late_max_ms,jobs_ps,job_repost_ps,"
		"job_wait_p50_ms,job_wait_p90_ms,job_wait_p99_ms,job_run_max_ms,flush_run_max_ms,"
		"job_dispatch_p50_ms,job_dispatch_p99_ms,job_busy_ms_ps,flush_busy_ms_ps,flush_runs_ps,send_kick_ps,"
		"session_objects,handles,sendq_total,idle_kicks,sendq_kicks";

	// 누적 카운터의 구간 증가량을 초당 값으로 바꾼다.
	struct RateCounter
	{
		explicit RateCounter(const std::atomic<long long>& src) : _src(src), _prev(src.load()) {}
		double Take()
		{
			const long long now = _src.load();
			const double ps = (now - _prev) / static_cast<double>(REPORT_INTERVAL_SEC);
			_prev = now;
			return ps;
		}
		const std::atomic<long long>& _src;
		long long _prev;
	};

	// ms 분포 칸(NetStats::MsBuckets)의 구간 분위수(ms). 마지막 칸(상한 이상)에 걸리면 상한 값이 나온다.
	struct MsBucketStat
	{
		explicit MsBucketStat(NetStats::MsBuckets& src) : _src(src) {}
		NetStats::MsBuckets& _src;
		long long prev[NetStats::MS_BUCKETS]{};
		double p50 = 0, p90 = 0, p99 = 0;

		void Take()
		{
			long long delta[NetStats::MS_BUCKETS];
			long long total = 0;
			for (int i = 0; i < NetStats::MS_BUCKETS; ++i)
			{
				const long long now = _src[i].load(std::memory_order_relaxed);
				delta[i] = now - prev[i];
				prev[i] = now;
				total += delta[i];
			}
			auto rank = [&](double p) {
				if (total <= 0) return 0.0;
				const long long need = static_cast<long long>(std::ceil(p * total));
				long long acc = 0;
				for (int i = 0; i < NetStats::MS_BUCKETS; ++i)
				{
					acc += delta[i];
					if (acc >= need) return static_cast<double>(i);
				}
				return static_cast<double>(NetStats::MS_BUCKETS - 1);
			};
			p50 = rank(0.50); p90 = rank(0.90); p99 = rank(0.99);
		}
	};

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
		RateCounter sendFlushes(NetStats::SendFlushes), sendContinues(NetStats::SendContinues), sendDirect(NetStats::SendDirect), sendKicks(NetStats::SendKicks);
		RateCounter sendWakes(NetStats::SendWakes), jobsExecuted(NetStats::JobsExecuted), jobReposts(NetStats::JobReposts);
		MsBucketStat flushGap(NetStats::FlushGapMs), jobWait(NetStats::JobWaitMs), jobDispatch(NetStats::JobDispatchMs);
		RateCounter jobRunSum(NetStats::JobRunSumUs), flushRunSum(NetStats::FlushRunSumUs), flushRuns(NetStats::FlushRuns);

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
			// 연결 정리(A5.0): Sessions는 Disconnect 순간 줄어들므로, 끊긴 세션이 실제로 사라졌는지는 객체 수·프로세스 핸들 수·큐 합계로 본다.
			const int sessionObjs = NetStats::SessionObjects.load();
			DWORD handleCount = 0;
			::GetProcessHandleCount(::GetCurrentProcess(), &handleCount);
			const long long sendQueuedTotal = NetStats::SendQueued.load();
			const long long idleKicks = NetStats::IdleKicks.load();
			const long long sendQueueKicks = NetStats::SendQueueKicks.load();
			const Metrics::Percentiles itExec = Metrics::ItemOpExecUs.TakeAndReset();
			const Metrics::Percentiles itTotal = Metrics::ItemOpTotalUs.TakeAndReset();
			const Metrics::Percentiles eqApply = Metrics::EquipApplyUs.TakeAndReset();
			const Metrics::Percentiles pgExec = Metrics::ProgressSaveExecUs.TakeAndReset();
			const Metrics::Percentiles pgTotal = Metrics::ProgressSaveTotalUs.TakeAndReset();

			// 송신 묶기(P1b) 조사 지표
			const double flushPs = sendFlushes.Take(), continuePs = sendContinues.Take(), directPs = sendDirect.Take(), kickPs = sendKicks.Take();
			const double wakePs = sendWakes.Take(), jobsPs = jobsExecuted.Take(), repostPs = jobReposts.Take();
			flushGap.Take();
			jobWait.Take();
			jobDispatch.Take();
			// 초당 us → 초당 ms(1,000이면 스레드 하나를 꽉 채운 것)
			const double jobBusyMs = jobRunSum.Take() / 1000.0, flushBusyMs = flushRunSum.Take() / 1000.0, flushRunsPs = flushRuns.Take();
			const double jobRunMaxMs = NetStats::JobRunMaxUs.exchange(0) / 1000.0;
			const double flushRunMaxMs = NetStats::FlushRunMaxUs.exchange(0) / 1000.0;
			const Metrics::Percentiles wakeLate = Metrics::SendWakeLate.TakeAndReset();

			LOG_INFO("metrics",
				"sessions={} rooms={} players={} monsters={} in/s={:.0f} out/s={:.0f} sendCalls/s={:.0f} bufsPerSend={:.2f} atk(req/ok per s)={:.1f}/{:.1f} zones(total created/hits)={}/{} "
				"move(total ok/speed/nav/bad/stale/rate)={}/{}/{}/{}/{}/{} "
				"tickLate(avg/p50/p95/p99/max ms)={:.1f}/{:.1f}/{:.1f}/{:.1f}/{:.1f} tickBody(avg/p50/p95/p99/max ms)={:.2f}/{:.2f}/{:.2f}/{:.2f}/{:.2f} "
				"viewPlayers(avg/p99/max)={:.1f}/{:.0f}/{:.0f} viewObjects(avg/p99/max)={:.1f}/{:.0f}/{:.0f} sendQ(total/sessionMax)={}/{} logDrop={} "
				"viewAudit(must/missing mustNot/extra)={}/{} {}/{} aoi(nodes/depth)={}/{} "
				"itemOp(total sent/fail/busy inflight)={}/{}/{} {} itemOpMs(n exec avg/p99/max total avg/p99/max)={} {:.1f}/{:.1f}/{:.1f} {:.1f}/{:.1f}/{:.1f} "
				"equipApplyMs(n avg/p50/p99/max)={} {:.1f}/{:.1f}/{:.1f}/{:.1f} "
				"progressSave(total sent/coalesced/fail inflight)={}/{}/{} {} progressSaveMs(n exec avg/p99/max total avg/p99/max)={} {:.1f}/{:.1f}/{:.1f} {:.1f}/{:.1f}/{:.1f} "
				"aim(total on/reject/off/castClear/timeout)={}/{}/{}/{}/{} atkReject(total sent/earlyOk)={}/{} "
				"revive(total deaths/ok/reject/deadMove)={}/{}/{}/{} portal(total ok/reject)={}/{} "
				"login(total kick/busy/forced)={}/{}/{} "
				"drops(now kills/created/expired/evicted)={} {}/{}/{}/{} pickup(picked/discarded)={}/{} trades={} "
				"sendPath(flush/continue/direct/kick per s)={:.0f}/{:.0f}/{:.0f}/{:.0f} flushGap(p50/p90/p99 ms)={:.0f}/{:.0f}/{:.0f} "
				"sendWake(per s, late avg/p50/p99/max ms)={:.0f} {:.1f}/{:.1f}/{:.1f}/{:.1f} jobs(exec/repost per s)={:.0f}/{:.0f} jobWait(p50/p90/p99 ms)={:.0f}/{:.0f}/{:.0f} jobRunMax={:.1f}ms flushRunMax={:.1f}ms "
				"jobDispatch(p50/p90/p99 ms)={:.0f}/{:.0f}/{:.0f} busy(job/flush ms per s)={:.0f}/{:.0f} flushRuns/s={:.0f} conn(sessionObjs/handles idleKick/sendqKick)={}/{} {}/{}",
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
				Metrics::AimOn.load(), Metrics::AimReject.load(), Metrics::AimOff.load(), Metrics::AimCastClear.load(), Metrics::AimTimeout.load(), Metrics::AttackRejected.load(), Metrics::AttackEarlyAccepted.load(),
				Metrics::PlayerDeaths.load(), Metrics::Revives.load(), Metrics::ReviveReject.load(), Metrics::MoveDroppedDead.load(), Metrics::PortalUsed.load(), Metrics::PortalReject.load(),
				Metrics::LoginKicks.load(), Metrics::LoginBusy.load(), Metrics::LoginCloseForced.load(),
				Metrics::Drops.load(), Metrics::MonsterKills.load(), Metrics::DropsCreated.load(), Metrics::DropsExpired.load(), Metrics::DropsEvicted.load(),
				Metrics::DropsPicked.load(), Metrics::DropsDiscarded.load(), Metrics::TradesDone.load(),
				flushPs, continuePs, directPs, kickPs, flushGap.p50, flushGap.p90, flushGap.p99,
				wakePs, wakeLate.avg / 1000.0, wakeLate.p50 / 1000.0, wakeLate.p99 / 1000.0, wakeLate.max / 1000.0, jobsPs, repostPs,
				jobWait.p50, jobWait.p90, jobWait.p99, jobRunMaxMs, flushRunMaxMs,
				jobDispatch.p50, jobDispatch.p90, jobDispatch.p99, jobBusyMs, flushBusyMs, flushRunsPs,
				sessionObjs, handleCount, idleKicks, sendQueueKicks);

			auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
			csv << runId << "," << now << "," << NetStats::Sessions.load() << "," << rooms << "," << players << ","
				<< inPs << "," << outPs << "," << late.avg / 1000.0 << "," << late.max / 1000.0 << ","
				<< body.avg / 1000.0 << "," << body.max / 1000.0 << ","
				<< sendCallsPs << "," << bufsPerSend << "," << late.p99 / 1000.0 << "," << body.p99 / 1000.0 << "," << sendQueueMax << ","
				<< flushPs << "," << continuePs << "," << directPs << "," << flushGap.p50 << "," << flushGap.p90 << "," << flushGap.p99 << ","
				<< wakePs << "," << wakeLate.avg / 1000.0 << "," << wakeLate.p99 / 1000.0 << "," << wakeLate.max / 1000.0 << ","
				<< jobsPs << "," << repostPs << ","
				<< jobWait.p50 << "," << jobWait.p90 << "," << jobWait.p99 << "," << jobRunMaxMs << "," << flushRunMaxMs << ","
				<< jobDispatch.p50 << "," << jobDispatch.p99 << "," << jobBusyMs << "," << flushBusyMs << "," << flushRunsPs << "," << kickPs << ","
				<< sessionObjs << "," << handleCount << "," << sendQueuedTotal << "," << idleKicks << "," << sendQueueKicks << "\n";
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
