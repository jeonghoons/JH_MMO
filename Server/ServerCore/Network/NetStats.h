#pragma once
#include <atomic>

// 네트워크 계층 지표. Session이 핫패스에서 증가만 하고, 게임 쪽 Metrics 리포터가 읽어 출력한다.
namespace NetStats
{
	inline std::atomic<int>       Sessions{ 0 };      // 현재 연결된 세션 수
	inline std::atomic<long long> PacketsIn{ 0 };     // 처리한 수신 패킷 수(누적)
	inline std::atomic<long long> MessagesOut{ 0 };   // Session::Send 호출 수(누적, 브로드캐스트는 수신자 수만큼 증가)
	inline std::atomic<long long> SendQueued{ 0 };    // 모든 세션의 송신 큐에 쌓여 아직 WSASend에 넘기지 않은 패킷 수(현재)
	inline std::atomic<int>       SendQueueMax{ 0 };  // 한 세션의 송신 큐가 가장 길었던 값(구간 최대, 리포터가 읽으며 0으로)
}
