#pragma once
#include "Network/IocpCore.h"

enum class EventType : unsigned char
{
	Accept,
	Recv,
	Send,
	Job,
	DBResult,
	SendBatch,	// 잡 큐가 넘긴 "이 세션들의 송신 큐를 지금 보내라"(P1b — 실제 WSASend를 잡 큐 스레드 밖 워커에서 한다)
};

class IocpEvent : public OVERLAPPED
{

public:
	IocpEvent(EventType type);
	IocpEvent(EventType type, shared_ptr<IocpObject> owner);

	void Init();

public:
	EventType	_type;
	shared_ptr<IocpObject> _owner;
};

class Session;
class SendBuffer;

class AcceptEvent : public IocpEvent
{
public:
	AcceptEvent() : IocpEvent(EventType::Accept) {}

public:
	shared_ptr<Session> _session = nullptr;
};

class RecvEvent : public IocpEvent
{
public:
	RecvEvent() : IocpEvent(EventType::Recv) {}
};

class SendEvent : public IocpEvent
{
public:
	SendEvent() : IocpEvent(EventType::Send) {}

	vector<shared_ptr<SendBuffer>> sendBuffers;
};

// 잡 큐가 한 번에 많은 세션을 내보낼 때 세션 묶음을 워커에게 넘기는 이벤트(JobQueue::PostSendBatches).
// 포스트하는 쪽이 new, 처리한 쪽(JobQueue::Dispatch)이 delete한다.
class SendBatchEvent : public IocpEvent
{
public:
	SendBatchEvent() : IocpEvent(EventType::SendBatch) {}

	vector<shared_ptr<Session>> sessions;
};

class JobEvent : public IocpEvent
{
public:
	JobEvent() : IocpEvent(EventType::Job) {}
};

class DBEvent : public IocpEvent
{
public:
	DBEvent(shared_ptr<IocpObject> owner) : IocpEvent(EventType::DBResult, owner) {}
};

