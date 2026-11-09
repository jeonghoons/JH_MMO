#include "pch.h"
#include "Utils/Metrics.h"
#include "Network/NetStats.h"
#include "Game/Room/Room.h"
#include <fstream>
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

	void ReportLoop()
	{
		std::ofstream csv("metrics.csv", std::ios::app);
		if (csv.tellp() == 0)
			csv << "time,sessions,rooms,players,packets_in_ps,messages_out_ps,tick_late_avg_ms,tick_late_max_ms,tick_body_avg_ms,tick_body_max_ms\n";

		long long prevIn = NetStats::PacketsIn.load();
		long long prevOut = NetStats::MessagesOut.load();
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
			const Metrics::Percentiles eqExec = Metrics::EquipSaveExecUs.TakeAndReset();
			const Metrics::Percentiles eqTotal = Metrics::EquipSaveTotalUs.TakeAndReset();

			LOG_INFO("metrics",
				"sessions={} rooms={} players={} monsters={} in/s={:.0f} out/s={:.0f} atk(req/ok per s)={:.1f}/{:.1f} zones(total created/hits)={}/{} "
				"move(total ok/speed/nav/bad/stale/rate)={}/{}/{}/{}/{}/{} "
				"tickLate(avg/p50/p95/p99/max ms)={:.1f}/{:.1f}/{:.1f}/{:.1f}/{:.1f} tickBody(avg/p50/p95/p99/max ms)={:.2f}/{:.2f}/{:.2f}/{:.2f}/{:.2f} "
				"viewPlayers(avg/p99/max)={:.1f}/{:.0f}/{:.0f} viewObjects(avg/p99/max)={:.1f}/{:.0f}/{:.0f} sendQ(total/sessionMax)={}/{} logDrop={} "
				"viewAudit(must/missing mustNot/extra)={}/{} {}/{} aoi(nodes/depth)={}/{} "
				"equipSave(total req/sent/coalesced/fail inflight)={}/{}/{}/{} {} equipSaveMs(n exec avg/p99/max total avg/p99/max)={} {:.1f}/{:.1f}/{:.1f} {:.1f}/{:.1f}/{:.1f} "
				"aim(total on/reject/off/castClear/timeout)={}/{}/{}/{}/{}",
				NetStats::Sessions.load(), rooms, players, Metrics::Monsters.load(), inPs, outPs, atkReqPs, atkOkPs, Metrics::ZonesCreated.load(), Metrics::ZoneHits.load(),
				Metrics::MoveAccepted.load(), Metrics::MoveRejectSpeed.load(), Metrics::MoveRejectNav.load(), Metrics::MoveRejectInvalid.load(),
				Metrics::MoveDroppedStale.load(), Metrics::MoveDroppedRate.load(),
				late.avg / 1000.0, late.p50 / 1000.0, late.p95 / 1000.0, late.p99 / 1000.0, late.max / 1000.0,
				body.avg / 1000.0, body.p50 / 1000.0, body.p95 / 1000.0, body.p99 / 1000.0, body.max / 1000.0,
				viewP.avg, viewP.p99, viewP.max, viewO.avg, viewO.p99, viewO.max,
				NetStats::SendQueued.load(), sendQueueMax, Logger::Dropped(),
				Metrics::ViewAuditMust.load(), Metrics::ViewAuditMissing.load(), Metrics::ViewAuditMustNot.load(), Metrics::ViewAuditExtra.load(),
				Metrics::AoiNodes.exchange(0), Metrics::AoiDepth.exchange(0),
				Metrics::EquipSaveRequested.load(), Metrics::EquipSaveSent.load(), Metrics::EquipSaveCoalesced.load(), Metrics::EquipSaveFailed.load(),
				Metrics::EquipSaveInFlight.load(), eqExec.count,
				eqExec.avg / 1000.0, eqExec.p99 / 1000.0, eqExec.max / 1000.0, eqTotal.avg / 1000.0, eqTotal.p99 / 1000.0, eqTotal.max / 1000.0,
				Metrics::AimOn.load(), Metrics::AimReject.load(), Metrics::AimOff.load(), Metrics::AimCastClear.load(), Metrics::AimTimeout.load());

			auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
			csv << now << "," << NetStats::Sessions.load() << "," << rooms << "," << players << ","
				<< inPs << "," << outPs << "," << late.avg / 1000.0 << "," << late.max / 1000.0 << ","
				<< body.avg / 1000.0 << "," << body.max / 1000.0 << "\n";
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
