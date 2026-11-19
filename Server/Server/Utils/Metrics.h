#pragma once
#include <atomic>
#include <chrono>
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

	inline long long NowUs()
	{
		return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
	}

	// 아이템 작업(A3.3, Room::StartItemOp → DatabaseWorker::ExecItemOp → Room::OnItemOpDone). 지급·장착·로그인 보정
	inline std::atomic<long long> ItemOpSent{ 0 };       // DB 작업으로 보낸 수(누적)
	inline std::atomic<long long> ItemOpFailed{ 0 };     // 롤백·DB 오류(누적, [Debug] DbItemFailEvery의 강제 롤백 포함)
	inline std::atomic<long long> ItemOpBusy{ 0 };       // 진행 중인 작업이 있어 BUSY로 거부한 요청(누적)
	inline std::atomic<int>       ItemOpInFlight{ 0 };   // 지금 DB에 가 있는 작업 수
	inline SampleStat ItemOpExecUs;    // DB 실행 시간(us)
	inline SampleStat ItemOpTotalUs;   // 요청 → 결과 적용(us, DB 큐 대기 + Room 잡 복귀 포함)
	inline SampleStat EquipApplyUs;    // 장착 요청 → 적용(us) — 기준 p99 50ms(D-163)

	// 진행 상황 저장(A2.5, Room::RequestProgressSave → DatabaseWorker::SavePlayerProgress). 값이 달라진 것만 보낸다
	inline std::atomic<long long> ProgressSaveSent{ 0 };       // DB 작업으로 보낸 수(누적)
	inline std::atomic<long long> ProgressSaveCoalesced{ 0 };  // 진행 중인 저장이 있어 합쳐진 요청(누적)
	inline std::atomic<long long> ProgressSaveFailed{ 0 };     // DB 실패(누적)
	inline std::atomic<int>       ProgressSaveInFlight{ 0 };   // 지금 DB에 가 있는 저장 수
	inline SampleStat ProgressSaveExecUs;    // DB 실행 시간(us)
	inline SampleStat ProgressSaveTotalUs;   // 요청 → 완료(us, DB 큐 대기 포함)

	// 중복 접속 차단(A2.7, AuthLobby)
	inline std::atomic<long long> LoginKicks{ 0 };        // 새 로그인 때문에 기존 접속을 끊은(또는 끊긴 접속의 저장을 기다린) 횟수(누적)
	inline std::atomic<long long> LoginBusy{ 0 };         // LOGIN_RESULT_BUSY 응답(대기 시간 초과·더 늦은 로그인에 밀림, 누적)
	inline std::atomic<long long> LoginCloseForced{ 0 };  // 정리 신호가 오지 않아 접속 목록에서 강제로 뺀 횟수(누적, 0이어야 정상)

	// 스킬 조준(MC4.2d-1, Room::PlayerSkillAim). 모두 누적
	inline std::atomic<long long> AimOn{ 0 };         // 켜기 수락
	inline std::atomic<long long> AimReject{ 0 };     // 켜기 거부
	inline std::atomic<long long> AimOff{ 0 };        // 클라 취소로 해제
	inline std::atomic<long long> AimCastClear{ 0 };  // CS_ATTACK으로 해제 (시전 성공·실패 모두)
	inline std::atomic<long long> AimTimeout{ 0 };    // AimMaxMs 초과로 서버가 해제

	// 바닥 드랍(A3.4, Room::CreateDrop/RemoveDrop)
	inline std::atomic<int>       Drops{ 0 };            // 지금 바닥에 있는 드랍 수(모든 방)
	inline std::atomic<long long> MonsterKills{ 0 };     // 드랍을 굴린 몬스터 처치 수(누적)
	inline std::atomic<long long> DropsCreated{ 0 };     // 만든 드랍 수(누적)
	inline std::atomic<long long> DropsExpired{ 0 };     // DropLifeSec가 지나 지운 수(누적)
	inline std::atomic<long long> DropsEvicted{ 0 };     // MaxDropsPerRoom을 넘어 오래된 것부터 지운 수(누적)
	inline std::atomic<long long> DropsPicked{ 0 };      // 주워서 지운 수(누적, A3.5)
	inline std::atomic<long long> DropsDiscarded{ 0 };   // 플레이어가 버려서 만든 수(누적, DropsCreated에도 들어간다, A3.5)
	inline std::atomic<long long> TradesDone{ 0 };       // 성사된 1:1 거래 수(누적, A4.2)

	// 전역 서비스(DG3a): WorldService가 명부·채널이 바뀔 때 적는다
	inline std::atomic<int>       WorldChannels{ 0 };   // 만들어진 채널 수
	inline std::atomic<int>       WorldRoster{ 0 };     // 접속자 명부 크기(로그인 ~ 정리 완료, 더미 봇 포함)
	inline std::atomic<int>       WorldChannelMax{ 0 }; // 인원이 가장 많은 채널의 인원
	inline std::atomic<int>       Monsters{ 0 };      // 현재 월드에 살아 있거나 시체로 남아 있는 몬스터 수
	inline std::atomic<long long> AttackRequests{ 0 };  // CS_ATTACK 수신 수(누적)
	inline std::atomic<long long> AttackAccepted{ 0 };  // 쿨타임·상태 검사를 통과한 플레이어 공격 수(누적)
	inline std::atomic<long long> AttackRejected{ 0 };  // Room::CastSkill이 거부해 SC_ATTACK_RESULT를 보낸 수(누적, CP1)
	inline std::atomic<long long> AttackEarlyAccepted{ 0 };  // 허용 오차(AttackEarlyToleranceMs)로 받은 공격 수(누적, D-213)
	inline std::atomic<long long> ZonesCreated{ 0 };   // 생성된 지면 장판 수(누적)
	inline std::atomic<long long> ZoneHits{ 0 };       // 장판이 입힌 피해 횟수(누적)

	// 플레이어 이동 검증 결과(누적, Room::PlayerMove). 보정을 보낸 경우 = Speed + Nav + Invalid
	inline std::atomic<long long> MoveAccepted{ 0 };
	inline std::atomic<long long> MoveRejectSpeed{ 0 };    // 속도 예산 초과
	inline std::atomic<long long> MoveRejectNav{ 0 };      // 내비메시로 갈 수 없는 위치
	inline std::atomic<long long> MoveRejectInvalid{ 0 };  // NaN/Inf
	inline std::atomic<long long> MoveDroppedStale{ 0 };   // 보정 전에 보낸 이동(옛 순번)
	inline std::atomic<long long> MoveDroppedRate{ 0 };    // 너무 잦은 이동
	inline std::atomic<long long> MoveDroppedDead{ 0 };    // 사망 중 이동 (A2.4)

	// 플레이어 사망·부활(A2.4, Room::PlayerRevive). 모두 누적
	inline std::atomic<long long> PlayerDeaths{ 0 };
	inline std::atomic<long long> Revives{ 0 };        // 부활 수락
	inline std::atomic<long long> ReviveReject{ 0 };   // 부활 거부(살아 있음·대기 시간 전)

	// 포털 이동(L5, Room::PlayerUsePortal). 모두 누적
	inline std::atomic<long long> PortalUsed{ 0 };     // 이동 수락
	inline std::atomic<long long> PortalReject{ 0 };   // 거부(없는 포털·도착지 없음·거리 밖·사망·시전 중·너무 잦음)

	inline SampleStat TickLate;     // Room 틱이 예정보다 늦어진 시간(us) (Timer/JobQueue/IOCP 대기 지연)
	inline SampleStat TickBody;     // Room::Update 본문 처리 시간(us)
	inline SampleStat SendWakeLate; // 송신 깨우기 작업(Room::OnSendWake)이 예약 시각보다 늦게 돈 시간(us) (P1b — Timer 해상도 + IOCP 대기)
	inline SampleStat ViewPlayers;  // 플레이어 한 명의 시야 안 다른 플레이어 수 (1초마다 플레이어별 표본)
	inline SampleStat ViewObjects;  // 플레이어 한 명의 시야 안 전체 오브젝트 수(플레이어 + 몬스터)

	// AOI 쿼드트리 상태(구간 최대, 방마다 1초에 한 번 Room 잡 안에서 기록, 리포터가 읽으며 0으로)
	inline std::atomic<int> AoiNodes{ 0 };
	inline std::atomic<int> AoiDepth{ 0 };

	// 시야 감사([Debug] ViewAudit, 누적). 플레이어(보는 쪽)와 다른 캐릭터 한 쌍마다 실제 거리로 판정한다.
	inline std::atomic<long long> ViewAuditMust{ 0 };      // 거리 <= R 인 쌍(반드시 보여야 함)
	inline std::atomic<long long> ViewAuditMissing{ 0 };   // 그중 시야 목록에 없는 쌍(누락)
	inline std::atomic<long long> ViewAuditMustNot{ 0 };   // 거리 > R+H 인 쌍(보이면 안 됨)
	inline std::atomic<long long> ViewAuditExtra{ 0 };     // 그중 시야 목록에 있는 쌍(초과)

	// 5초마다 한 줄을 콘솔과 metrics.csv에 기록하는 스레드를 시작한다.
	void StartReporter();
}
