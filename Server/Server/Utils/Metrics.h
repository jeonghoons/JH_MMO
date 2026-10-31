#pragma once
#include <atomic>
#include <mutex>
#include <vector>

// 부하 테스트용 서버 지표(게임 쪽). 세션·패킷 수는 네트워크 계층의 NetStats에 있다. 핫패스에서는 atomic 증가만 하고, 집계/출력은 Metrics::StartReporter 스레드가 한다.
namespace Metrics
{
	// 구간 분위수용 표본 모음. 표본이 적은 지표(Room 틱: 방마다 초당 10개, 시야 인원: 플레이어마다 초당 1개)에만 쓴다.
	// 잠금 한 번 + push_back이라 핫패스(패킷마다)에는 쓰지 않는다.
	struct Percentiles
	{
		long long count = 0;
		double avg = 0, p50 = 0, p95 = 0, p99 = 0, max = 0;
	};

	class SampleStat
	{
	public:
		void Add(long long v)
		{
			std::lock_guard<std::mutex> lock(_mutex);
			_samples.push_back(v);
		}
		// 지금까지 모은 표본으로 분위수를 계산하고 비운다(리포터가 5초마다 호출).
		Percentiles TakeAndReset();

	private:
		std::mutex _mutex;
		std::vector<long long> _samples;
	};

	inline std::atomic<int>       Monsters{ 0 };      // 현재 월드에 살아 있거나 시체로 남아 있는 몬스터 수
	inline std::atomic<long long> AttackRequests{ 0 };  // CS_ATTACK 수신 수(누적)
	inline std::atomic<long long> AttackAccepted{ 0 };  // 쿨타임·상태 검사를 통과한 플레이어 공격 수(누적)
	inline std::atomic<long long> ZonesCreated{ 0 };   // 생성된 지면 장판 수(누적)
	inline std::atomic<long long> ZoneHits{ 0 };       // 장판이 입힌 피해 횟수(누적)

	// 플레이어 이동 검증 결과(누적, Room::PlayerMove). 보정을 보낸 경우 = Speed + Nav + Invalid
	inline std::atomic<long long> MoveAccepted{ 0 };
	inline std::atomic<long long> MoveRejectSpeed{ 0 };    // 속도 예산 초과
	inline std::atomic<long long> MoveRejectNav{ 0 };      // 내비메시로 갈 수 없는 위치
	inline std::atomic<long long> MoveRejectInvalid{ 0 };  // NaN/Inf
	inline std::atomic<long long> MoveDroppedStale{ 0 };   // 보정 전에 보낸 이동(옛 순번)
	inline std::atomic<long long> MoveDroppedRate{ 0 };    // 너무 잦은 이동

	inline SampleStat TickLate;     // Room 틱이 예정보다 늦어진 시간(us) (Timer/JobQueue/IOCP 대기 지연)
	inline SampleStat TickBody;     // Room::Update 본문 처리 시간(us)
	inline SampleStat ViewPlayers;  // 플레이어 한 명의 시야 안 다른 플레이어 수 (1초마다 플레이어별 표본)
	inline SampleStat ViewObjects;  // 플레이어 한 명의 시야 안 전체 오브젝트 수(플레이어 + 몬스터)

	// 시야 감사([Debug] ViewAudit, 누적). 플레이어(보는 쪽)와 다른 캐릭터 한 쌍마다 실제 거리로 판정한다.
	inline std::atomic<long long> ViewAuditMust{ 0 };      // 거리 <= R 인 쌍(반드시 보여야 함)
	inline std::atomic<long long> ViewAuditMissing{ 0 };   // 그중 시야 목록에 없는 쌍(누락)
	inline std::atomic<long long> ViewAuditMustNot{ 0 };   // 거리 > R+H 인 쌍(보이면 안 됨)
	inline std::atomic<long long> ViewAuditExtra{ 0 };     // 그중 시야 목록에 있는 쌍(초과)

	// 5초마다 한 줄을 콘솔과 metrics.csv에 기록하는 스레드를 시작한다.
	void StartReporter();
}
