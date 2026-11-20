#include "CorePch.h"
#include "Job/JobQueue.h"
#include "Network/Session.h"
#include "Network/NetStats.h"

namespace
{
	// 한 번의 ExecuteJobs가 워커를 붙잡는 상한(감사 F5). 넘으면 자기 자신을 다시 포스트하고 워커를 놓는다.
	constexpr int MAX_JOBS_PER_SLICE = 64;
	constexpr long long MAX_SLICE_MS = 2;

	// 한 번의 내보내기에서 이 스레드가 직접 WSASend하는 세션 수. 넘는 세션은 SEND_BATCH_SIZE개씩 묶어 IOCP 워커에게 넘긴다.
	// WSASend 1회가 수십 us라 세션 100개를 직접 보내면 이 큐가 7~10ms 막힌다(2026-11-19 측정).
	// 적을 때는 넘기는 비용(포스트 + 워커 깨움)이 더 크므로 직접 보낸다.
	constexpr int INLINE_FLUSH_MAX = 8;
	constexpr size_t SEND_BATCH_SIZE = 16;

	thread_local JobQueue* LCurrentJobQueue = nullptr;

	// 작업 실행 구간 표시. 예외로 빠져나가도 반드시 되돌린다.
	struct CurrentQueueScope
	{
		explicit CurrentQueueScope(JobQueue* queue) : _prev(LCurrentJobQueue) { LCurrentJobQueue = queue; }
		~CurrentQueueScope() { LCurrentJobQueue = _prev; }
		JobQueue* _prev;
	};

	long long NowMs()
	{
		return chrono::duration_cast<chrono::milliseconds>(chrono::steady_clock::now().time_since_epoch()).count();
	}
}

HANDLE JobQueue::GetHandle()
{
	return HANDLE();
}

void JobQueue::Dispatch(IocpEvent* iocpEvent, int numBytes)
{
	if (iocpEvent->_type == EventType::SendBatch)
	{
		// 잡 큐가 넘긴 세션 묶음을 이 워커에서 보낸다. 잡 큐의 상태는 건드리지 않으므로 작업 실행(_isProcessing)과 겹쳐도 된다.
		SendBatchEvent* batch = static_cast<SendBatchEvent*>(iocpEvent);
		for (shared_ptr<Session>& session : batch->sessions)
			session->TrySendNow();
		delete batch;
		return;
	}

	iocpEvent->_owner = nullptr;

	if (iocpEvent->_type == EventType::Job)
		ExecuteJobs();
}

JobQueue* JobQueue::Current()
{
	return LCurrentJobQueue;
}

void JobQueue::SetSendBatch(int intervalMs, function<void(int)> wakeFn)
{
	_sendWakeFn = std::move(wakeFn);
	// 깨워 줄 수단이 없으면 모아 둔 세션이 큐가 빈 뒤 영영 안 나갈 수 있으므로 간격을 쓰지 않는다.
	_sendIntervalMs = _sendWakeFn ? max(0, intervalMs) : 0;
}

void JobQueue::CloseSendBatch()
{
	_sendIntervalMs = 0;
	_sendWakeFn = nullptr;
	_sendWakeAtMs = 0;
	_nextSendDueMs = 0;	// 간격이 0이면 모든 세션의 보낼 시각이 0이다
	FlushDueSends(NowMs());
}

void JobQueue::AddPendingSend(shared_ptr<Session> session)
{
	const long long due = (_sendIntervalMs > 0) ? session->LastFlushMs() + _sendIntervalMs : 0;
	if (_pendingSends.empty() || due < _nextSendDueMs)
		_nextSendDueMs = due;
	_pendingSends.push_back(std::move(session));
}

bool JobQueue::FlushDueSends(long long nowMs)
{
	if (_sendWakeAtMs != 0 && nowMs >= _sendWakeAtMs)
		_sendWakeAtMs = 0;	// 예약한 깨우기 시각이 지났다(그 작업이 지금 돌았거나 곧 돈다)

	if (_pendingSends.empty() || nowMs < _nextSendDueMs)
		return false;

	int flushed = 0;
	long long nextDue = LLONG_MAX;
	// FlushSend가 실패해 Disconnect로 이어져도 이 큐에는 작업만 들어온다. 목록이 도중에 늘어도 안전하게 인덱스로 돈다.
	for (size_t i = 0; i < _pendingSends.size(); )
	{
		Session* session = _pendingSends[i].get();
		const long long due = (_sendIntervalMs > 0) ? session->LastFlushMs() + _sendIntervalMs : 0;
		if (due <= nowMs)
		{
			shared_ptr<Session> keep = std::move(_pendingSends[i]);
			_pendingSends[i] = std::move(_pendingSends.back());
			_pendingSends.pop_back();
			const bool sendNow = flushed < INLINE_FLUSH_MAX;
			keep->FlushSend(nowMs, sendNow);
			if (!sendNow)
				_deferredSends.push_back(std::move(keep));
			++flushed;
		}
		else
		{
			nextDue = min(nextDue, due);
			++i;
		}
	}
	_nextSendDueMs = nextDue;
	if (!_deferredSends.empty())
		PostSendBatches(_deferredSends);
	return flushed > 0;
}

void JobQueue::PostSendBatches(vector<shared_ptr<Session>>& sessions)
{
	NetStats::SendKicks.fetch_add(static_cast<long long>(sessions.size()), memory_order_relaxed);

	for (size_t begin = 0; begin < sessions.size(); begin += SEND_BATCH_SIZE)
	{
		const size_t end = min(sessions.size(), begin + SEND_BATCH_SIZE);
		SendBatchEvent* batch = new SendBatchEvent();
		batch->_owner = shared_from_this();
		batch->sessions.assign(std::make_move_iterator(sessions.begin() + begin), std::make_move_iterator(sessions.begin() + end));
		if (PostQueuedCompletionStatus(_iocpHandle, 0, 0, batch))
			continue;

		// 넘기지 못했으면 이 스레드에서 보낸다(세션은 이미 목록에서 빠졌으므로 여기서 안 보내면 남는다).
		for (shared_ptr<Session>& session : batch->sessions)
			session->TrySendNow();
		delete batch;
	}
	sessions.clear();
}

void JobQueue::ArmSendWake(long long nowMs)
{
	if (_pendingSends.empty() || !_sendWakeFn)
		return;

	// 이미 예약한 깨우기가 더 이르거나 같으면 그것으로 충분하다. 더 이른 세션이 뒤늦게 등록됐으면 한 번 더 예약한다
	// (늦은 쪽 깨우기는 그대로 울리고, 그때 보낼 것이 없으면 아무 일도 없다).
	const int delayMs = static_cast<int>(max<long long>(1, _nextSendDueMs - nowMs));
	const long long wakeAtMs = nowMs + delayMs;
	if (_sendWakeAtMs != 0 && wakeAtMs >= _sendWakeAtMs)
		return;

	_sendWakeAtMs = wakeAtMs;
	NetStats::SendWakes.fetch_add(1, memory_order_relaxed);
	_sendWakeFn(delayMs);
}

void JobQueue::ExecuteJobs()
{
	CurrentQueueScope scope(this);

	const long long startMs = NowMs();
	NetStats::AddMs(NetStats::JobDispatchMs, (NowUs() - _postedUs.load(memory_order_relaxed)) / 1000);
	int executed = 0;

	while (true) {
		shared_ptr<Job> job;
		while (_jobQueue.try_pop(job)) {
			const long long runStartUs = NowUs();
			NetStats::AddMs(NetStats::JobWaitMs, (runStartUs - job->_enqueueUs) / 1000);
			job->Execute();
			job = nullptr;
			NetStats::JobsExecuted.fetch_add(1, memory_order_relaxed);

			const long long runEndUs = NowUs();
			NetStats::UpdateMax(NetStats::JobRunMaxUs, runEndUs - runStartUs);
			NetStats::JobRunSumUs.fetch_add(runEndUs - runStartUs, memory_order_relaxed);
			const long long nowMs = runEndUs / 1000;
			if (FlushDueSends(nowMs))
			{
				const long long flushUs = NowUs() - runEndUs;
				NetStats::UpdateMax(NetStats::FlushRunMaxUs, flushUs);
				NetStats::FlushRunSumUs.fetch_add(flushUs, memory_order_relaxed);
				NetStats::FlushRuns.fetch_add(1, memory_order_relaxed);
			}

			// 상한(감사 F5): 큐가 빌 때까지 한 워커를 붙잡지 않는다. _isProcessing은 켠 채로 자기 자신을 다시 포스트하므로
			// 다른 스레드가 끼어들 수 없고, 먼저 쌓인 다른 완료 통지(수신·송신 완료·다른 큐)가 그 사이에 처리된다.
			// 때가 안 된 세션이 목록에 남아 있어도 다시 포스트된 실행이 작업을 하나 이상 처리한 뒤 검사하므로 깨우기는 필요 없다.
			if ((++executed >= MAX_JOBS_PER_SLICE || nowMs - startMs >= MAX_SLICE_MS) && !_jobQueue.empty())
			{
				_jobEvent.Init();
				_jobEvent._owner = shared_from_this();
				_postedUs.store(NowUs(), memory_order_relaxed);
				if (PostQueuedCompletionStatus(_iocpHandle, 0, 0, &_jobEvent))
				{
					NetStats::JobReposts.fetch_add(1, memory_order_relaxed);
					return;
				}

				// 포스트에 실패하면 플래그가 켜진 채 이 큐가 멈추므로, 워커를 놓지 않고 계속 처리한다.
				_jobEvent._owner = nullptr;
				LOG_ERROR("JobQueue", "재포스트 실패(err={}) — 이 워커에서 계속 처리합니다", ::GetLastError());
			}
		}

		// 큐가 비었다. 보내기와 깨우기 예약은 플래그를 내리기 전에 한다 — 송신 실패로 Disconnect → 방 퇴장 작업이
		// 이 큐로 들어와도, 플래그가 켜져 있어 다른 스레드가 동시에 실행하지 못한다(감사 §7-4).
		const long long nowMs = NowMs();
		FlushDueSends(nowMs);
		ArmSendWake(nowMs);

		// 플래그를 내린 뒤 큐를 다시 확인한다. 드레인과 플래그 해제 사이에 push된 잡은
		// RegisterJobs가 플래그가 아직 true라서 이벤트를 포스트하지 않으므로 여기서 이어서 처리한다.
		_isProcessing.store(false);
		if (_jobQueue.empty())
			break;

		bool expected = false;
		if (_isProcessing.compare_exchange_strong(expected, true) == false)
			break; // 다른 스레드가 이미 재등록했다.
	}
}
