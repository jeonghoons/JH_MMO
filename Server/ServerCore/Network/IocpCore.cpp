#include "CorePch.h"
#include "Network/IocpCore.h"
#include "Network/IocpEvent.h"

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
		if (over == nullptr)
			return true;
		shared_ptr<IocpObject> iocpObject = over->_owner;
		if (iocpObject != nullptr)
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
			// 완료 패킷 없이 실패한 경우(over == nullptr — 포트 핸들 문제 등)에는 넘길 대상이 없다(감사 F7).
			if (over == nullptr)
			{
				LOG_ERROR("Network", "GetQueuedCompletionStatus 실패 (완료 패킷 없음, 오류 {})", errCode);
				return false;
			}
			// 실패한 I/O의 완료(상대가 끊음, CancelIoEx로 취소됨 등)는 그대로 넘긴다 — 세션은 0바이트 완료로 보고 참조를 풀고 끊는다.
			shared_ptr<IocpObject> iocpObject = over->_owner;
			if (iocpObject != nullptr)
				iocpObject->Dispatch(over, num_bytes);
			break;
		}
	}

	return true;
}