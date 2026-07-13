#pragma once

class IocpObject : public enable_shared_from_this<IocpObject>
{
public:
	virtual HANDLE GetHandle() abstract;
	virtual void Dispatch(class IocpEvent* iocpEvent, int numBytes = 0) abstract;

};

class IocpCore
{
public:
	IocpCore();
	~IocpCore();

	HANDLE GetHandle() { return iocpHandle; }
	bool Register(shared_ptr<IocpObject> iocpObject);

	bool Dispatch(int timeouts = INFINITE);

private:
	HANDLE	iocpHandle;
	
};

