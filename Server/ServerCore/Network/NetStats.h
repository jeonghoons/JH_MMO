#pragma once
#include <atomic>

// 네트워크 계층 지표. Session이 핫패스에서 증가만 하고, 게임 쪽 Metrics 리포터가 읽어 출력한다.
namespace NetStats
{
	inline std::atomic<int>       Sessions{ 0 };      // 현재 연결된 세션 수
	inline std::atomic<long long> PacketsIn{ 0 };     // 처리한 수신 패킷 수(누적)
	inline std::atomic<long long> MessagesOut{ 0 };   // Session::Send 호출 수(누적, 브로드캐스트는 수신자 수만큼 증가)
	inline std::atomic<long long> SendCalls{ 0 };     // WSASend 호출 수(누적)
	inline std::atomic<long long> SendBufs{ 0 };      // WSASend에 넘긴 WSABUF 수(누적) — SendBufs ÷ SendCalls = 호출당 메시지 수(송신 묶임 정도)
	inline std::atomic<long long> SendQueued{ 0 };    // 모든 세션의 송신 큐에 쌓여 아직 WSASend에 넘기지 않은 패킷 수(현재)
	inline std::atomic<int>       SendQueueMax{ 0 };  // 한 세션의 송신 큐가 가장 길었던 값(구간 최대, 리포터가 읽으며 0으로)

	// 송신 묶기(P1b) 조사 지표. 모두 누적.
	// WSASend가 어느 경로에서 나갔는지: 잡 큐가 모아서(Flushes) / 송신 완료 직후 큐에 남은 것을 이어서(Continues — 송신 간격을 거치지 않음)
	// / 잡 큐 밖 스레드에서 바로(Direct). Flushes·Direct는 시도 횟수라 이미 송신 중이면 WSASend로 이어지지 않는다.
	inline std::atomic<long long> SendFlushes{ 0 };
	inline std::atomic<long long> SendContinues{ 0 };
	inline std::atomic<long long> SendDirect{ 0 };
	inline std::atomic<long long> SendKicks{ 0 };     // Flushes 가운데 WSASend를 IOCP 워커에게 넘긴 세션 수
	inline std::atomic<long long> SendWakes{ 0 };     // 잡 큐가 예약한 송신 깨우기 수
	inline std::atomic<long long> JobsExecuted{ 0 };  // 잡 큐가 실행한 작업 수
	inline std::atomic<long long> JobReposts{ 0 };    // 독점 상한(잡 수·시간)에 걸려 잡 큐가 자기 자신을 다시 포스트한 수

	// ms 단위 분포 칸. 칸 i = i ms, 마지막 칸은 그 이상 전부.
	constexpr int MS_BUCKETS = 128;
	using MsBuckets = std::atomic<long long>[MS_BUCKETS];
	inline void AddMs(MsBuckets& buckets, long long ms)
	{
		buckets[ms < 0 ? 0 : (ms >= MS_BUCKETS ? MS_BUCKETS - 1 : ms)].fetch_add(1, std::memory_order_relaxed);
	}
	inline void UpdateMax(std::atomic<long long>& target, long long v)
	{
		long long prev = target.load(std::memory_order_relaxed);
		while (v > prev && !target.compare_exchange_weak(prev, v, std::memory_order_relaxed)) {}
	}

	inline MsBuckets FlushGapMs{};   // 한 세션의 연이은 FlushSend 사이 간격
	inline MsBuckets JobWaitMs{};    // 작업이 잡 큐에 들어간 뒤 실행을 시작하기까지 기다린 시간
	inline std::atomic<long long> JobRunMaxUs{ 0 };    // 작업 하나의 실행 시간 최대(구간 최대, 리포터가 읽으며 0으로)
	inline MsBuckets JobDispatchMs{}; // 잡 큐가 IOCP에 실행을 포스트한 뒤 워커가 ExecuteJobs에 들어오기까지 걸린 시간
	inline std::atomic<long long> JobRunSumUs{ 0 };    // 작업 실행 시간 합(누적)
	inline std::atomic<long long> FlushRunSumUs{ 0 };  // 송신 내보내기(FlushDueSends) 시간 합(누적)
	inline std::atomic<long long> FlushRuns{ 0 };      // 세션을 하나 이상 내보낸 FlushDueSends 횟수(누적)
	inline std::atomic<long long> FlushRunMaxUs{ 0 };  // 잡 큐의 송신 내보내기(FlushDueSends) 한 번의 시간 최대(구간 최대)
}
