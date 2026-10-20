#pragma once
#include <atomic>

// 부하 테스트용 서버 지표. 핫패스에서는 atomic 증가만 하고, 집계/출력은 Metrics::StartReporter 스레드가 한다.
namespace Metrics
{
	struct Stat
	{
		std::atomic<long long> sumUs{ 0 };
		std::atomic<long long> count{ 0 };
		std::atomic<long long> maxUs{ 0 };

		void Add(long long us)
		{
			sumUs.fetch_add(us);
			count.fetch_add(1);

			long long prev = maxUs.load();
			while (us > prev && !maxUs.compare_exchange_weak(prev, us)) {}
		}
	};

	inline std::atomic<int>       Sessions{ 0 };      // 현재 연결된 세션 수
	inline std::atomic<int>       Monsters{ 0 };      // 현재 월드에 살아 있거나 시체로 남아 있는 몬스터 수
	inline std::atomic<long long> PacketsIn{ 0 };     // 처리한 수신 패킷 수(누적)
	inline std::atomic<long long> AttackRequests{ 0 };  // CS_ATTACK 수신 수(누적)
	inline std::atomic<long long> AttackAccepted{ 0 };  // 쿨타임·상태 검사를 통과한 플레이어 공격 수(누적)
	inline std::atomic<long long> ZonesCreated{ 0 };   // 생성된 지면 장판 수(누적)
	inline std::atomic<long long> ZoneHits{ 0 };       // 장판이 입힌 피해 횟수(누적)
	inline std::atomic<long long> MessagesOut{ 0 };   // Session::Send 호출 수(누적, 브로드캐스트는 수신자 수만큼 증가)

	inline Stat TickLate;   // Room 틱이 예정보다 늦어진 시간 (Timer/JobQueue/IOCP 대기 지연)
	inline Stat TickBody;   // Room::Update 본문 처리 시간

	// 5초마다 한 줄을 콘솔과 metrics.csv에 기록하는 스레드를 시작한다.
	void StartReporter();
}
