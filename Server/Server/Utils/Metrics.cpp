#include "pch.h"
#include "Utils/Metrics.h"
#include "Network/NetStats.h"
#include "Game/Room/Room.h"
#include <fstream>
#include <thread>

namespace
{
	constexpr int REPORT_INTERVAL_SEC = 5;

	struct Snapshot
	{
		double avgMs = 0.0;
		double maxMs = 0.0;
	};

	// 구간 통계를 읽으면서 0으로 되돌린다.
	Snapshot TakeAndReset(Metrics::Stat& stat)
	{
		long long sum = stat.sumUs.exchange(0);
		long long count = stat.count.exchange(0);
		long long max = stat.maxUs.exchange(0);

		Snapshot s;
		if (count > 0)
			s.avgMs = (sum / static_cast<double>(count)) / 1000.0;
		s.maxMs = max / 1000.0;
		return s;
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

			Snapshot late = TakeAndReset(Metrics::TickLate);
			Snapshot body = TakeAndReset(Metrics::TickBody);

			char line[384];
			snprintf(line, sizeof(line),
				"[metrics] sessions=%d rooms=%d players=%d monsters=%d in/s=%.0f out/s=%.0f atk(req/ok per s)=%.1f/%.1f zones(total created/hits)=%lld/%lld tickLate(avg/max ms)=%.1f/%.1f tickBody(avg/max ms)=%.2f/%.2f",
				NetStats::Sessions.load(), rooms, players, Metrics::Monsters.load(), inPs, outPs, atkReqPs, atkOkPs, Metrics::ZonesCreated.load(), Metrics::ZoneHits.load(), late.avgMs, late.maxMs, body.avgMs, body.maxMs);
			cout << line << endl;

			auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
			csv << now << "," << NetStats::Sessions.load() << "," << rooms << "," << players << ","
				<< inPs << "," << outPs << "," << late.avgMs << "," << late.maxMs << ","
				<< body.avgMs << "," << body.maxMs << "\n";
			csv.flush();
		}
	}
}

void Metrics::StartReporter()
{
	std::thread(ReportLoop).detach();
}
