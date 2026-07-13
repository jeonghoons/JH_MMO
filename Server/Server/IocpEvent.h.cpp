#include "pch.h"
#include "IocpEvent.h"

IocpEvent::IocpEvent(EventType type) : _type(type)
{
	Init();
}

IocpEvent::IocpEvent(EventType type, shared_ptr<IocpObject> owner) : _type(type), _owner(owner)
{
	Init();
}

void IocpEvent::Init()
{
	OVERLAPPED::Internal = 0;
	OVERLAPPED::InternalHigh = 0;
	OVERLAPPED::Offset = 0;
	OVERLAPPED::OffsetHigh = 0;
	OVERLAPPED::hEvent = nullptr;
}
