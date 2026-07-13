#pragma once
#include "IocpCore.h"

enum class EventType : unsigned char
{
	Accept,
	Recv,
	Send,
	Job,
	DBResult,
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

