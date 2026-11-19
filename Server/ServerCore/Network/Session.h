#pragma once
#include "Network/RecvBuffer.h"
#include "Network/IocpCore.h"
#include "Network/NetAddress.h"
#include "Network/IocpEvent.h"
#include "Network/SendBuffer.h"

class ServerService;

class Session : public IocpObject
{
	
public:
	Session();
	virtual ~Session();

	virtual HANDLE GetHandle() override;
	virtual void Dispatch(class IocpEvent* iocpEvent, int numBytes = 0) override;


public:
	void		SetService(shared_ptr<ServerService> service) { _service = service; }
	shared_ptr<ServerService> GetService() 	{return _service.lock();	}
	SOCKET	GetSocket() { return _socket; }
	void		SetNetAddress(NetAddress address) { _netAddress = address; }
	bool		IsConnected() { return _connected.load(); }
	void		SetId(int num) { _cid = num; }
	int		GetId() { return _cid; }

public:
	void			Connected();				// Listener가 accept 완료 후 호출
	void			Disconnect(const WCHAR* cause);
	// 서버가 먼저 끊을 때(중복 접속 차단 등, A2.7). Disconnect는 소켓을 그대로 둬서 가만히 있는 클라이언트의 연결이 남으므로,
	// 여기서는 양방향을 닫아 상대에게 FIN을 보내고 걸어 둔 수신을 끝낸다. 직전에 Send한 패킷은 이미 전송을 걸었으면 도착한다
	// (다른 전송이 진행 중이라 큐에만 있던 패킷은 버려진다). 송신 묶기(P1b)로 큐에만 있던 패킷은 끊기 전에 여기서 전송을 건다.
	void			ForceClose(const WCHAR* cause);

	// 패킷을 송신 큐에 넣는다. 잡 큐(Room·로비)의 작업을 실행 중인 스레드에서 부르면 바로 보내지 않고 그 잡 큐에
	// 등록해 두었다가 작업이 끝난 뒤 세션당 WSASend 1회로 보낸다(송신 묶기 P1b — JobQueue.h). 그 밖의 스레드에서는 바로 보낸다.
	void			Send(shared_ptr<SendBuffer> sendBuffer);

	// 잡 큐가 모아 둔 송신을 내보낸다(JobQueue 전용). 송신 큐에 쌓인 것을 WSASend 1회로 넘긴다.
	// sendNow가 false면 표시(보낸 시각·등록 해제)만 하고 WSASend는 하지 않는다 — 부른 쪽이 TrySendNow를 워커에게 넘긴다.
	void			FlushSend(long long nowMs, bool sendNow = true);
	// 송신 큐가 비어 있지 않고 진행 중인 송신이 없으면 지금 보낸다. 어느 스레드에서 불러도 된다.
	void			TrySendNow();
	// 마지막으로 FlushSend한 시각(steady_clock ms, 한 번도 없으면 0) — 세션별 송신 빈도 제한에 쓴다.
	long long	LastFlushMs() const { return _lastFlushMs.load(memory_order_relaxed); }

	int			ProcessData(BYTE* buffer, int len);

protected:
	// 콘텐츠 쪽(GameSession)이 구현하는 훅. 네트워크 계층은 게임 코드를 모른다.
	virtual void	OnConnected() {}
	virtual void	OnDisconnected() {}
	virtual void	OnRecvPacket(BYTE* buffer, int len) = 0;	// 헤더 포함 패킷 하나

private:
	void			RegisterRecv();
	void			ProcessRecv(int numOfBytes);

	void			RegisterSend();
	void			ProcessSend(int numOfBytes);


public:
	RWLock		_lock;
	
	RecvBuffer	_recvBuffer;
	

	// queue<shared_ptr<SendBuffer>> _sendQueue;
	SendBufferQueue		_sendQueue;
	atomic<int>			_sendQueued = 0;	// _sendQueue에 쌓인 패킷 수(NetStats 송신 큐 지표용)
	atomic<bool>			_sendRegistered = false;
	atomic<bool>			_flushPending = false;	// 어느 잡 큐의 "보낼 세션 목록"에 올라 있다(중복 등록 방지)
	atomic<long long>	_lastFlushMs = 0;

private:
	weak_ptr<ServerService> _service;
	SOCKET			_socket;
	NetAddress		_netAddress = {};
	atomic<bool>		_connected = false;
	unsigned int		_cid = {};

	RecvEvent		_recvEvent;
	SendEvent		_sendEvent; // 패킷을 모아서 보내기 위해서 재사용

};

struct PacketHeader
{
	uint16_t size;
	uint16_t id;
};

// 헤더를 포함한 패킷 하나의 최대 크기. 이보다 크거나 헤더보다 작으면 비정상 패킷으로 보고 연결을 끊는다.
constexpr int MAX_PACKET_SIZE = 8192;

