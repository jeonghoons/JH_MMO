#include "pch.h"
#include "IocpCore.h"
#include "IocpEvent.h"

IocpCore::IocpCore()
{
	iocpHandle = ::CreateIoCompletionPort(INVALID_HANDLE_VALUE, 0, 0, 0);
}

IocpCore::~IocpCore()
{
	::CloseHandle(iocpHandle);
}

bool IocpCore::Register(shared_ptr<IocpObject> iocpObject)
{
	// iocp핸들에 클라이언트 등록
	return ::CreateIoCompletionPort(iocpObject->GetHandle(), iocpHandle, 0, 0);
}

bool IocpCore::Dispatch(int timeouts)
{
	DWORD num_bytes{};
	ULONG_PTR key{};
	IocpEvent* over = nullptr;

	if (::GetQueuedCompletionStatus(iocpHandle, &num_bytes, &key,
		reinterpret_cast<LPOVERLAPPED*>(&over), timeouts))
	{
		shared_ptr<IocpObject> iocpObject = over->_owner;
		iocpObject->Dispatch(over, num_bytes);
		
	}
	else
	{
		int errCode = ::WSAGetLastError();
		switch (errCode)
		{
		case WAIT_TIMEOUT:
			return false;
		default:
			shared_ptr<IocpObject> iocpObject = over->_owner;
			iocpObject->Dispatch(over, num_bytes);
			break;
		}
	}

	return true;
}