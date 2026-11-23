#pragma once
#include "Job/Job.h"
#include "Network/IocpEvent.h"
#include <concurrent_queue.h>

class Session;

// 오너(Room·AuthLobby)의 작업을 한 번에 한 스레드에서만 순서대로 실행하는 큐.
//
// 송신 묶기(P1b, D-206): 이 큐의 작업을 실행 중인 스레드에서 부른 Session::Send는 바로 WSASend하지 않고
// 세션 송신 큐에만 넣은 뒤 이 큐의 "보낼 세션 목록"에 등록한다. 작업 하나가 끝날 때마다 목록에서
// "마지막 송신 뒤 송신 간격(SetSendBatch)이 지난 세션"만 세션당 WSASend 1회로 보낸다(세션별 송신 빈도 제한).
// 한동안 받은 것이 없던 세션은 그 작업이 끝나자마자 나가고, 방금 보낸 세션에 가는 메시지만 간격만큼 모인다.
// 큐가 비었는데 아직 때가 안 된 세션이 남으면 깨우기 함수로 타이머 작업을 예약해 그때 보낸다.
// 목록은 작업을 실행 중인 스레드만 만진다(_isProcessing이 한 번에 한 스레드를 보장).
class JobQueue : public IocpObject
{
public:
	JobQueue(HANDLE iocpHandle) : _iocpHandle(iocpHandle) {}

	virtual HANDLE GetHandle() override;
	virtual void Dispatch(class IocpEvent* iocpEvent, int numBytes = 0) override;

	void Push(shared_ptr<Job> job)
	{
		job->_enqueueUs = NowUs();
		job->_aheadAtPush = _queuedCount.fetch_add(1, memory_order_relaxed);
		_jobQueue.push(job);
		RegisterJobs();
	}

	template<typename T, typename... Arguments>
	void Push(shared_ptr<T> owner, void(T::* memFunc)(Arguments...), Arguments&&... args)
	{
		shared_ptr<Job> job = make_shared<Job>(owner, memFunc, std::forward<Arguments>(args)...);
		job->_enqueueUs = NowUs();
		job->_aheadAtPush = _queuedCount.fetch_add(1, memory_order_relaxed);
		_jobQueue.push(job);
		RegisterJobs();
	}

	void ExecuteJobs();

	// 송신 간격(ms)과 깨우기 함수를 정한다. 작업을 넣기 전에(오너 초기화 때) 한 번만 부른다.
	// wakeFn(delayMs): delayMs 뒤에 이 큐에서 아무 작업(빈 작업이면 됨)이나 실행되게 예약한다 — 그 작업 뒤의 송신 검사가 남은 세션을 보낸다.
	// 부르지 않았거나 wakeFn이 없으면 간격 0(작업이 끝날 때마다 바로 보냄)으로 동작한다.
	void SetSendBatch(int intervalMs, function<void(int)> wakeFn);

	// 오너를 없애기 직전에 부르는 닫기 절차(DG3c). 이 큐의 작업 안에서만 부른다(Current() == this).
	// 송신 간격을 0으로 바꾸고 깨우기를 떼어 낸 뒤 보낼 세션 목록을 지금 전부 내보낸다. 이 뒤로 이 큐에서 한 Send는
	// 그 작업이 끝날 때 바로 나가므로, 목록에 남아 _flushPending이 켜진 채 멈추는 세션이 생기지 않는다.
	void CloseSendBatch();

	static long long NowUs()
	{
		return chrono::duration_cast<chrono::microseconds>(chrono::steady_clock::now().time_since_epoch()).count();
	}

	// 이 스레드가 지금 실행 중인 잡 큐(작업 실행 중이 아니면 nullptr). Session::Send가 묶을지 판단하는 데 쓴다.
	static JobQueue* Current();
	// 이 스레드가 지금 실행 중인 작업(작업 실행 중이 아니면 nullptr). 작업 안에서 자기 대기 시간(_enqueueUs)·
	// 넣을 때 앞에 있던 작업 수(_aheadAtPush)를 읽는 데 쓴다(Room::Update의 틱 지연 분해, A5.4a).
	static const Job* CurrentJob();

	// Session::Send 전용: 이 큐가 작업을 실행 중인 스레드에서만 부른다(Current() == this).
	void AddPendingSend(shared_ptr<Session> session);

private:
	void RegisterJobs()
	{
		bool expected = false;
		if (_isProcessing.compare_exchange_strong(expected, true)){
			_jobEvent.Init();
			_jobEvent._owner = shared_from_this();
			_postedUs.store(NowUs(), memory_order_relaxed);
			PostQueuedCompletionStatus(_iocpHandle, 0, 0, &_jobEvent);
		}
	}

	// 보낼 때가 된 세션을 보낸다(세션당 WSASend 1회). 때가 안 된 세션은 목록에 남긴다.
	// 하나라도 보냈으면 true.
	bool FlushDueSends(long long nowMs);
	// 세션들의 WSASend를 IOCP 워커에게 묶음으로 넘긴다(포스트에 실패한 묶음은 여기서 바로 보낸다). sessions는 비워진다.
	void PostSendBatches(vector<shared_ptr<Session>>& sessions);
	// 목록에 세션이 남아 있고 예약한 깨우기가 없으면 가장 이른 시각에 깨우기를 예약한다.
	void ArmSendWake(long long nowMs);

private:
	HANDLE						_iocpHandle;
	JobEvent						_jobEvent;
	concurrency::concurrent_queue<shared_ptr<Job>> _jobQueue;
	atomic<bool> _isProcessing = false;
	atomic<long long> _postedUs = 0;	// 마지막으로 IOCP에 실행을 포스트한 시각(지표용)
	atomic<int> _queuedCount = 0;		// 넣었지만 아직 시작하지 않은 작업 수(지표용 — Push에서 +1, 꺼낼 때 -1)

	// 송신 묶기 상태 — 작업을 실행 중인 스레드만 접근한다.
	vector<shared_ptr<Session>>	_pendingSends;
	vector<shared_ptr<Session>>	_deferredSends;	// FlushDueSends가 워커에게 넘길 세션을 잠깐 모으는 곳(호출이 끝나면 빔)
	long long					_nextSendDueMs = 0;	// 목록에서 가장 이른 "보낼 시각"(목록이 비면 의미 없음)
	long long					_sendWakeAtMs = 0;	// 예약해 둔 깨우기 시각(0 = 없음)
	int							_sendIntervalMs = 0;
	function<void(int)>			_sendWakeFn;
};
