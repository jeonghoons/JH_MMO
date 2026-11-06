# JH_MMO 게임 서버 동시성 감사 (2026-11-06)

대상: `Server/ServerCore`, `Server/Server`(손으로 작성한 코드 약 9,200줄), 봇은 서버와 상호작용하는 부분만. 생성 코드·ThirdParty 제외. 읽기 전용으로 수행했고 빌드·실행·git 변경은 하지 않았다.
파일 경로는 `Server/` 기준이며 줄 번호는 현재 작업 트리(`content/dev`) 기준이다.
> 2026-11-06 Claude Fable 5.1 서브에이전트가 수행. 서버 작업 재개 시 코드가 바뀌었을 수 있으니 줄 번호 대신 함수명으로 찾고, 각 항목을 다시 확인한 뒤 수정한다. 처리 계획은 `ROADMAP.md` P0·P1a·P1b·A5.0, 같은 날 성능 기준선은 [PERF_BASELINE_2026-11-06.md](PERF_BASELINE_2026-11-06.md).

## 0. 요약 (심각도순)

| # | 심각도 | 위치 | 한 줄 요약 |
|---|---|---|---|
| F1 | **높음** | `ServerCore/Network/Session.cpp:92-115` + `Server/Packet/PacketHandler.cpp:57,157` | 파싱 실패로 `Disconnect`한 뒤에도 같은 수신 묶음의 나머지 패킷을 계속 처리한다 → 끊긴 세션의 `CS_ENTER_ROOM`이 방에 들어가 **유령 플레이어**(정원 1칸 영구 점유). 악의적 클라이언트가 결정적으로 재현 가능 |
| F2 | **높음** | `Server/Session/GameSession.cpp:10-12` ↔ `Server/Game/Room/Room.cpp:777-781` | 다른 워커의 `Disconnect`(송신 실패 등)가 `CS_ENTER_ROOM` 처리 도중에 끼면 `PlayerLeaveRoom`이 안 들어가고 `PlayerEnterRoom`만 들어간다 → 같은 유령 플레이어(확률적). F1과 수정 지점이 같다 |
| F3 | 중간 | `Server/Session/GameSession.h:12`, `AuthLobby.cpp:33`, `PacketHandler.cpp:80-186`, `GameSession.cpp:10` | `GameSession::_currPlayer`(`shared_ptr`)를 로비 잡 스레드가 쓰고 수신 스레드·`Disconnect` 스레드가 동기화 없이 읽는다(데이터 레이스). `Session::_lock`은 선언만 되고 쓰이지 않음 |
| F4 | 중간 | `ServerCore/Network/Session.cpp:57-68` | `Disconnect`가 소켓을 닫지도 `shutdown`/`CancelIoEx`도 하지 않는다. 받지 않는 클라이언트에 `WSASend`가 걸려 있으면 세션(수신 버퍼 64KB + 송신 큐)이 TCP 타임아웃까지 산다 → 연결 자원 고갈(DoS) 경로 |
| F5 | 중간 | `ServerCore/Job/JobQueue.h:28-50` | `ExecuteJobs`가 큐가 빌 때까지 한 워커를 독점한다(시간·개수 상한 없음). 핫존 방 하나가 워커 1개를 계속 붙잡고, 방 50개·워커 6개 상황에서 다른 방의 틱이 밀린다. P1에서 플러시 지점을 정할 때 함께 설계해야 함 |
| F6 | 낮음 | `ServerCore/Job/Timer.h:33-39`, `Server/DB/DatabaseWorker.cpp:22-26`, `Listener.cpp:22` | 끝나지 않는 스레드를 소멸자에서 join → `GTimer`·`GDBWorker` 생성 이후의 어떤 `exit()`(예: `Listener::Dispatch`의 `exit(-1)`)도 프로세스를 멈춘다. 과거 사례의 재발 조건이 아직 남아 있음 |
| F7 | 낮음 | `ServerCore/Network/IocpCore.cpp:42` | `GetQueuedCompletionStatus` 실패 시 `over == nullptr`(핸들 닫힘·`ERROR_ABANDONED_WAIT_0`)을 검사하지 않고 `over->_owner` 역참조 → 종료 시 크래시 가능 |
| F8 | 낮음 | `Server/Game/Room/Room.cpp:109` + `ServerCore/Job/Timer.h:17-20` | `Room::Update`가 자기 자신을 강한 참조(`shared_from_this`)로 100ms마다 재예약 → Room은 영원히 해제되지 않는다(현재 방 삭제 기능이 없어 실해는 없음, `RoomManager::Remove(shared_ptr)`은 빈 함수) |
| F9 | 낮음(추정) | `Room.cpp:157,780` ↔ `GameSession.cpp:11` | `GameObject::_ownerRoom`(`weak_ptr`)을 수신 스레드(`TryEnterExisting`)와 방 스레드(`AddObject`)가 쓰고 `Disconnect` 스레드가 `lock()`한다. 같은 Room을 쓰는 경우라 실제 피해는 보지 못했지만 형식상 데이터 레이스 |
| F10 | 낮음 | `ServerCore/Network/Session.cpp:50-51,65` | `GetService()`가 `weak_ptr::lock()` 결과를 null 검사 없이 역참조. 서비스 소멸 중에만 문제(현재 종료 절차가 없어 실해 없음) |

그 외(동시성은 아니지만 읽는 중에 확인한 것, 6절): 플레이어 id와 몬스터 id 충돌(TODO.md에 이미 기록), `PlayerEnterRoom`이 `SC_ADD_OBJECT`를 두 번 보냄, 유휴 세션 타임아웃 없음.

---

## 1. 세션 수명·송신 경쟁

### 구조 (확인한 내용)
- 세션 참조: `IocpCore::Dispatch`가 `over->_owner`를 지역 `shared_ptr`로 복사한 뒤 `Dispatch`를 부르므로 처리 중 세션은 살아 있다(`IocpCore.cpp:30`). `RecvEvent/SendEvent::_owner`가 미완료 I/O 동안 세션을 붙잡고, 완료 처리 첫 줄에서 null로 돌린다(`Session.cpp:145,220`). 서비스 맵(`ServerService::_sessions`)과 `Player→weak_ptr<GameSession>`을 합치면 순환은 없다.
- 송신 플래그 프로토콜: `Send` = push → `_sendRegistered` CAS(false→true) 성공 시 `RegisterSend`; `ProcessSend` = `store(false)` → 큐 비어 있지 않으면 CAS → `RegisterSend`. seq_cst 원자 연산 + `concurrent_queue`라 "push와 플래그 해제 사이의 패킷"은 유실되지 않는다(A0.3 수정이 맞음). `_sendEvent`는 한 번에 하나의 `WSASend`에만 쓰이므로 재사용이 안전하다. `WSASend`가 즉시 완료되어 완료 통지가 먼저 처리되는 경우도 `RegisterSend`가 `WSASend` 뒤에 `_sendEvent`를 만지지 않아 안전.
- `Disconnect`는 `_connected.exchange(false)`로 멱등. 소켓은 `~Session`에서만 닫힌다.

### F1. 파싱 실패 Disconnect 뒤에도 같은 수신 묶음의 패킷을 계속 처리 — 높음
- 위치: `ServerCore/Network/Session.cpp:92-115` (`ProcessData` 루프에 `IsConnected()` 검사 없음), `Server/Packet/PacketHandler.cpp:57` (`Disconnect(L"Packet Parse Fail")`), `PacketHandler.cpp:154-157` (`Handle_CS_ENTER_ROOM`).
- 문제: `ProcessData`는 패킷마다 `OnRecvPacket`을 부르는데, 핸들러가 `Disconnect`를 불러도 루프를 끝내지 않는다. `Disconnect → GameSession::OnDisconnected`는 "플레이어가 방에 있을 때만" `PlayerLeaveRoom`을 넣으므로, **로그인은 했지만 아직 방에 없는** 플레이어는 정리할 것이 없다고 보고 지나간다. 그 직후 같은 묶음의 `CS_ENTER_ROOM`이 처리되어 `GRoomManager->EnterPlayer`가 실행된다.
- 실패 시나리오(결정적):
  1. 클라이언트: `CS_LOGIN` → `SC_LOGIN` 성공 수신.
  2. 클라이언트: `send()` 한 번으로 `[CS_MOVE, 페이로드 = 깨진 바이트(protobuf 파싱 실패)] + [CS_ENTER_ROOM]` 전송.
  3. 워커 A `ProcessRecv → ProcessData`: 패킷 1 → `ParseFromArray` 실패 → `Disconnect`(`_connected=false`, `OnDisconnected`: `_currPlayer` 있음·`GetCurrentRoom()` null → 아무것도 안 함, `ReleaseSession`).
  4. 같은 루프 패킷 2 → `Handle_CS_ENTER_ROOM`: `_currPlayer != null && GetCurrentRoom()==null` → `EnterPlayer` → `TryReserveSlot` → `SetOwnerRoom` → `PushJob(PlayerEnterRoom)`.
  5. 워커 A가 `Dispatch`를 빠져나오며 세션 마지막 참조 해제 → `~Session`(소켓 닫힘). 방 잡 `PlayerEnterRoom`은 `GetSession()`이 null이라 패킷은 못 보내지만 `_players`/`_objects`에 넣고 AOI에 등록한다.
  6. 결과: `[metrics] sessions=0 players=1`. 몬스터가 유령을 공격해 HP 0으로 만들고 시체로 남는다. 정원 1칸 영구 점유(100번 반복하면 방이 하나 더 생긴다). H1(2026-10-28)의 변종.
- 재현: `TESTING.md`의 Python 원시 소켓 스크립트 방식으로 위 2단계를 한 `send()`로 보낸 뒤 `[metrics]` 줄의 `sessions`/`players` 비교. 또는 `CS_CHAT`에 잘못된 UTF-8(파싱 실패) + `CS_ENTER_ROOM`를 한 번에.
- 수정 방향:
  - (필수, ServerCore) `ProcessData` 루프에서 `OnRecvPacket` 뒤 `if (!IsConnected()) break;` — 현업에서도 끊긴 세션의 잔여 패킷은 버린다.
  - (필수, 방어) `Room::PlayerEnterRoom`(방 잡) 첫머리에서 `auto s = player->GetSession(); if (!s || !s->IsConnected()) { ReleaseSlot(); return; }`. F2까지 같이 막는다.
  - (권장) `GameSession::OnDisconnected`에서 `_currPlayer`를 null로 돌리거나, `Handle_CS_ENTER_ROOM` 등 모든 핸들러 첫 줄에 `if (!session->IsConnected()) return;`.

### F2. 다른 스레드의 Disconnect가 ENTER_ROOM 처리와 겹치는 경쟁 — 높음
- 위치: `Server/Session/GameSession.cpp:10-12`(Leave 조건 = `GetCurrentRoom()` non-null), `Server/Game/Room/Room.cpp:777-781`(`SetOwnerRoom` → `PushJob`), `ServerCore/Network/Session.cpp:207-210, 223-226`(송신 실패에서 `Disconnect`).
- 문제: `Disconnect`는 수신 스레드뿐 아니라 **송신 완료를 처리한 다른 워커**(`ProcessSend`의 `Send 0`, `RegisterSend Fail`)에서도 불린다. `Handle_CS_ENTER_ROOM`이 `GetCurrentRoom()==null`을 확인하고 `SetOwnerRoom`을 하기 전 사이에 다른 워커의 `Disconnect → OnDisconnected`가 `GetCurrentRoom()`을 보면 null이라 Leave 잡을 넣지 않는다. 그 뒤 수신 스레드가 Enter 잡을 넣는다.
- 시나리오: 워커 B가 이 세션의 `SC_LOGIN` 송신 완료를 처리하다 클라이언트 RST로 `ProcessSend(0) → Disconnect("Send 0")`. 동시에 워커 A가 같은 세션의 `CS_ENTER_ROOM`을 처리 중(파이프라인으로 LOGIN 직후 ENTER_ROOM을 보낸 봇·클라이언트). 결과는 F1과 같은 유령 플레이어. 창은 마이크로초 단위지만 chaos 램프(수천 번 접속·강제 종료)에서는 누적된다.
- 재현(확률적): `LoadTest --chaos`로 봇 1,000명을 여러 번 램프업/강제 종료(Ctrl+C) 반복 후 `[metrics] sessions=0`인데 `players>0`인지 확인. 과거 "정상 순서 접속·종료 300회 후 플레이어 0" 검사에 "강제 종료 + 핫 루프"를 추가.
- 수정 방향: F1의 방어(방 잡에서 `IsConnected()` 검사)로 닫힌다. 더 견고하게는 세션 상태 전이를 세션 단위로 직렬화(세션별 잡 큐, 또는 `_currPlayer`/방 배정 변경을 모두 로비 잡에서만).

### F3. `GameSession::_currPlayer` 데이터 레이스 — 중간
- 위치: 쓰기 `Server/Game/Lobby/AuthLobby.cpp:33`(로비 잡, 임의의 워커), 읽기 `Server/Packet/PacketHandler.cpp:80,103,114,154,162,173,186`(수신 처리 워커), `Server/Session/GameSession.cpp:10-12`(`Disconnect`를 부른 스레드). `ServerCore/Network/Session.h:53`의 `RWLock _lock`은 어디서도 쓰지 않는다.
- 문제: `shared_ptr` 복사/대입은 원자적이지 않다. 정상 클라이언트는 `SC_LOGIN`을 받고 나서 다음 패킷을 보내므로 사실상 순서가 잡히지만, (a) `CS_LOGIN` 직후 응답을 기다리지 않고 `CS_ENTER_ROOM`/`CS_LOGIN`을 파이프라인하는 클라이언트, (b) 로그인 결과가 나오는 순간 다른 워커에서 `Disconnect`(송신 실패·`Recv 0`)가 도는 경우에는 로비 스레드의 대입과 겹친다. 찢긴 `shared_ptr` 복사 → 참조 카운트 손상·크래시 가능(확률 낮음, UB).
- 재현: Python 원시 소켓으로 `CS_LOGIN` + `CS_ENTER_ROOM`를 한 `send()`에 담아 수백 회 반복(다른 계정으로). 결정적 재현은 어렵고, Debug 빌드 + `/fsanitize=address`로는 잡히지 않는 종류라 ThreadSanitizer가 없는 MSVC에서는 코드 리뷰로 판단해야 한다.
- 수정 방향: (현실안) `std::atomic<std::shared_ptr<Player>>`(C++20, MSVC 지원)로 바꾸고 `load()/store()`만 쓴다. 또는 이미 있는 `Session::_lock`으로 감싼다. (현업) 세션 상태를 바꾸는 일은 한 스레드로 몰아 넣는다 — 로비 잡이 `_currPlayer`를 쓰지 말고, 세션이 자기 큐를 갖거나 `OnLoginSuccess`가 세션에 "로그인 완료" 잡을 보내는 식.

### F4. `Disconnect`가 소켓을 정리하지 않는다 — 중간
- 위치: `ServerCore/Network/Session.cpp:57-68`. 소켓 닫기는 `~Session`(`Session.cpp:13-14`)뿐.
- 문제: 서버가 먼저 끊는 경우(파싱 실패, 비정상 헤더) 상대 소켓은 세션 객체가 죽을 때까지 열려 있다. 세션은 미완료 `WSASend`의 `_sendEvent._owner`가 붙잡는다. 받지 않는(수신 창 0) 클라이언트에게 송신이 걸려 있으면 완료가 오지 않아 세션·64KB 수신 버퍼·송신 큐가 TCP 재전송 타임아웃까지(Windows 기본 keepalive 꺼짐 → 사실상 수 분~) 남는다. 악의적 클라이언트가 연결을 열고 응답을 유도한 뒤 읽기를 멈추면 반복할수록 메모리·핸들이 쌓인다. 정상 종료(`Recv 0`)에는 영향이 없다.
- 재현: Python으로 로그인 후 `SO_RCVBUF`를 작게 잡고 수신을 멈춘 채 다른 봇들이 시야 안에서 이동하게 두면 서버가 그 세션에 `WSASend`를 걸고 완료가 오지 않는다. 그 상태에서 서버 쪽 `Disconnect`를 유도(깨진 패킷 전송)하고 `sessions`가 줄어도 프로세스 핸들 수·`sendQ(total)`이 줄지 않는지 확인.
- 수정 방향: (현업) `Disconnect`에서 `shutdown(SD_BOTH)` + `CancelIoEx((HANDLE)_socket, nullptr)` → 미완료 I/O가 오류로 완료되어 `_owner`가 풀리고 세션이 곧 해제된다. 또는 `DisconnectEx`로 재사용. (현실안) `CancelIoEx` 한 줄 + 세션 유휴 타임아웃(마지막 수신 시각, 예: 60초)을 Metrics 리포터 스레드가 검사해 `Disconnect`.

### 확인함 (문제 없음)
- `Send`/`RegisterSend`/`ProcessSend` 플래그 프로토콜(위 "구조"). 다중 스레드(로비 잡 + 방 잡)가 같은 세션에 동시에 `Send`해도 안전.
- `RegisterRecv`는 한 번에 하나만 걸리고(완료 처리 끝에서만 재등록), `Disconnect` 뒤에는 재등록하지 않는다.
- `RecvBuffer`는 수신 스레드 전용(동시에 한 수신만 처리) — 락 불필요.
- `SendBuffer`는 `Commit` 뒤 불변이라 여러 세션이 같은 버퍼를 공유해도 된다.
- `AcceptEvent::_session`이 accept 완료까지 세션을 붙잡고, `Connected()`에서 서비스 맵에 들어간다. id는 `Listener::_clientId`(atomic) 단조 증가.
- `~Session`은 `ReleaseSession`의 `_lock` 안에서 실행되지 않는다(호출자가 항상 `shared_ptr`을 들고 있다).

---

## 2. JobQueue 재진입·실행 보장

### 구조 (확인한 내용)
- `Push` = `concurrent_queue::push` → `_isProcessing` CAS(false→true) 성공 시에만 `PostQueuedCompletionStatus`. 한 번에 포스트는 최대 1개(`_jobEvent` 재사용 안전). `ExecuteJobs`는 비울 때까지 돌고 `store(false)` → 재검사 → CAS로 "드레인과 해제 사이 push"를 이어 받는다. 두 스레드가 동시에 같은 큐를 실행하는 경로는 없다(CAS가 보장).
- 방 잡 안에서 같은 방으로 `PushJob`(예: `OnDisconnected`가 방 잡 실행 중인 워커에서 불릴 수 있음)은 push만 되고 CAS 실패 → 현재 루프가 이어서 처리. 다른 큐(로비·DB)로의 push는 독립. 타이머 스레드는 큐 락을 풀고 `Push`한다(`Timer.cpp:44-52`).
- 방 상태를 만지는 모든 경로가 방 잡 안인지: `Room::Update → Monster::Update → Room::CastSkill/NPCMove/BroadcastAOI`, `UpdateZones → OnDamaged → OnDead → ReserveJob`, `CombatProcessor` — 모두 같은 잡 안의 동기 호출이며 컨테이너 수정(`_objects`/`_players`/`_monsters` erase)은 `PlayerLeaveRoom`/`RemoveMonster` 잡에서만 일어나 반복자 무효화가 없다.
- 타이머 잡은 모두 id로 다시 찾는다(`ExecuteSkillHit`, `ApplyDelayedDamage`, `RemoveMonster`, `SpawnMonster`) — C2 수정이 올바르고 공격자 null 경로(`ApplyDelayedDamage`, `Monster::OnDamaged`의 `attacker` null 검사)도 확인.
- 방 간 이동은 아직 없다. `PlayerLeaveRoom`은 `_ownerRoom`을 지우지 않지만 세션이 죽은 플레이어는 어느 핸들러에도 다시 들어오지 못한다(핸들러가 `_currPlayer`를 세션에서 얻음).

### F5. `ExecuteJobs`가 워커를 독점 — 중간(성능·공정성)
- 위치: `ServerCore/Job/JobQueue.h:28-50`.
- 문제: 큐가 빌 때까지 한 워커가 한 큐에 묶인다. 핫존 방(초당 수천 잡)은 워커 1개를 거의 항상 붙잡고, 분산 2,500(방 25개·워커 6개)에서는 긴 드레인이 다른 방의 `Update` 잡 실행을 늦춘다 — A1.3 "분산 2,500+에서 틱 지연 증가, 서버 CPU 여유"와 일치하는 설명이지만 **측정하지 않았으므로 추정**.
- 재현: `[metrics] tickLate` p99가 방 수 증가에 따라 커지는지, 워커 수를 `WorkerThreads`로 바꿔가며 비교(이미 TESTING.md 매트릭스 있음).
- 수정 방향: (현업) 한 번의 `ExecuteJobs`에 잡 개수(예: 64) 또는 시간(예: 1~2ms) 상한을 두고, 남으면 `_isProcessing`을 유지한 채 `PostQueuedCompletionStatus`로 자기 자신을 다시 포스트(다른 워커가 집어가도록). 상한 도달 시 F-P1(아래)의 송신 플러시를 함께 한다.

### 확인함
- `JobQueue` 소멸 시점: Room·Lobby가 소유하며 둘 다 프로세스 수명과 같다. 포스트된 `_jobEvent`는 `_owner`로 큐를 붙잡아 소멸 중 Dispatch는 없다.
- `AuthLobby` 잡이 한 스레드씩 실행되므로 "DB 응답 전 연달아 온 로그인"의 두 번째 성공을 거르는 로직(`AuthLobby.cpp:17`)은 로비 스레드 안에서는 옳다(단, F3의 레이스는 별개).
- `Timer::Run`은 CV `wait_until`로 잠들고(A0.3), 큐 락을 풀고 `Push`하므로 Timer 락 → IOCP 포스트 사이에 다른 락이 없다.

---

## 3. 락 순서·데드락·경합

### 모든 동기화 지점
| 종류 | 위치 | 잡는 스레드 | 락 안에서 하는 일 |
|---|---|---|---|
| `shared_mutex` `ServerService::_lock` | `ServerService.cpp:48-70` | accept 처리 워커(`Connected`), `Disconnect` 호출 스레드 | 맵 삽입/삭제 + 실패 시 `LOG_ERROR`(→ Logger `gMutex`) |
| `shared_mutex` `RoomManager::_lock`, `mutex _createMutex` | `Room.cpp:757,766,777,796` | 수신 워커(`EnterPlayer`), Metrics 리포터(읽기) | 읽기 락 안에서 `TryReserveSlot`(atomic) + `PushJob`(`PostQueuedCompletionStatus` 시스템 콜); `_createMutex` 안에서 `CreateRoom → InitRoom`(파일 I/O·몬스터 생성) |
| `mutex` `DBConnectionPool::_mutex` | `DBConnectionPool.cpp:46-60` | DB 스레드 | pop/push만 |
| `mutex`+CV `ConcurrentPQ`(Timer) | `Timer.cpp` | Timer 스레드, `ReserveJob` 호출자(방 잡) | push/pop만, 실행은 락 밖 |
| `mutex`+CV `ConcurrentQ`(DB 잡) | `ConcurrentQueue.h` | 수신 워커(push), DB 스레드(pop) | push/pop만 |
| `mutex` `Logger::gMutex`, `gDirectMutex` | `Logger.cpp` | 모든 스레드 | `vector::push_back`(재할당 가능), 출력은 락 밖 |
| `mutex` `Metrics::SampleStat::_mutex` | `Metrics.h:377` | 방 잡, 리포터 | push_back / swap |
| `mutex` `GNavCacheMutex` | `NavmeshManager.cpp:36` | 방 생성(수신 워커, `_createMutex` 안) | 첫 로드 때 파일 읽기 |
| 원자 | `Session::_connected/_sendRegistered/_sendQueued`, `JobQueue::_isProcessing`, `Room::_playerCount`, `NetStats::*`, `Metrics::*`, id 생성기, `Logger::_minLevel/_dropped` | — | — |
| 선언만 | `Session::_lock` | 미사용 | — |

### 획득 순서 그래프
- `_createMutex` → `RoomManager::_lock`(읽기) → (Room `PushJob`: concurrent_queue, 락 아님)
- `_createMutex` → `GNavCacheMutex`
- `ServerService::_lock` → `Logger::gMutex`
- `RoomManager::_lock`(읽기, 리포터) — 단독
- `ConcurrentPQ` 락 — 단독(실행 전 해제)
- Logger·Metrics·DB 풀 락은 잎(leaf): 안에서 다른 락을 잡지 않는다.

**순환 없음.** 데드락 경로는 찾지 못했다.

### 락을 잡은 채 하는 무거운 일 (낮음)
- `RoomManager::_lock` 읽기 안에서 `PostQueuedCompletionStatus`(`Room.cpp:781`). 읽기 락이라 다른 입장과는 병행되고 쓰기 락은 방 생성 때만 → 실해 없음.
- `_createMutex` 안에서 맵 로딩(`Room.cpp:796-800`) — 설계된 동작(A0.5). 방이 꽉 찬 순간 몰려온 입장 요청이 그 수신 워커들을 맵 로딩 동안 막는다(수백 ms). 방이 100명 단위로 새로 생길 때마다 반복. 방 생성을 미리(예: 90% 찼을 때) 해 두면 줄일 수 있다.
- `ServerService::_lock` 쓰기 안에서 `LOG_ERROR` — 실패 경로에서만.

### 방 잡 밖에서 방 상태를 만지는 경로
- `Room::TryReserveSlot/ReleaseSlot/NumPlayers`: atomic — 괜찮음.
- `RoomManager::TryEnterExisting`의 `player->SetOwnerRoom(room)`(수신 워커): F9.
- `Handle_CS_ENTER_ROOM`의 `player->GetCurrentRoom()`(수신 워커): F9의 읽기 쪽.
- 그 외 `Room` 멤버 접근은 모두 잡 안이다(`GetGameMap`, `GetNavManager`도 `Monster::Update` 안에서만).

### F9. `GameObject::_ownerRoom` 비원자 접근 — 낮음(추정)
- 쓰기: `Room.cpp:780,802`(수신 워커), `Room.cpp:157`(`AddObject`, 방 잡 — 같은 Room을 다시 대입). 읽기: `GameSession.cpp:11`(`Disconnect` 스레드), `PacketHandler.cpp` 각 핸들러(수신 워커).
- `weak_ptr` 대입은 포인터 두 개를 비원자로 바꾼다. 방 잡의 `AddObject`가 같은 Room을 다시 대입하는 동안 다른 스레드가 `lock()`하면 형식상 UB이지만, 전후 값이 같은 제어 블록이라 실제로는 같은 Room을 얻는다. 실해를 만드는 경로는 찾지 못했다.
- 수정 방향: `AddObject`에서 플레이어는 이미 배정된 방이면 다시 대입하지 않거나, 방 배정을 한 곳(수신 워커 → 방 잡 중 하나)에서만 한다. F3을 `atomic<shared_ptr>`로 고칠 때 `_ownerRoom`도 같은 방식으로 맞추면 깔끔하다.

---

## 4. shared_ptr/weak_ptr 순환·수명

### 확인함
- `GameSession → shared_ptr<Player>`, `Player → weak_ptr<GameSession>`: 순환 없음.
- `Room → shared_ptr<GameObject/Player/Monster>`, `GameObject → weak_ptr<Room>`, `GameMap → weak_ptr<Room>`: 순환 없음.
- `Monster::_targetPlayer`는 `weak_ptr` — 떠난 플레이어를 붙잡지 않는다.
- 잡 람다(`Job.h:14-20`)는 `owner`(Room/Lobby/DBWorker `shared_ptr`)와 인자(`shared_ptr<Player>`, `shared_ptr<GameSession>`)를 값으로 캡처 → 잡이 실행되거나 버려질 때까지만 수명을 늘린다. 큐는 비워지므로 누수 없음.
- `TimerItem`은 `weak_ptr<JobQueue>` + `shared_ptr<Job>`: 큐가 사라지면 잡은 버려진다. 단 잡 안의 `owner`(Room)는 강한 참조(F8).
- DB 잡·로비 잡이 `shared_ptr<GameSession>`을 들고 있는 동안 세션이 끊겨도 `OnLogin*`/`OnSignUpResult`가 `IsConnected()`로 걸러 송신하지 않는다. 끊긴 세션에 대한 `Send`는 `Send` 첫 줄에서 버려진다.
- `IocpEvent::_owner`는 완료 처리 시작에서 바로 null → 세션이 이벤트에 붙들려 남지 않는다(F4는 "완료가 안 오는" 경우).

### F8. Room이 영원히 산다 — 낮음
- `Room::Update`(`Room.cpp:109`)가 `ReserveJob`으로 자신을 100ms마다 재예약하고, `Timer::Reserve`가 만든 `Job`이 `shared_from_this()`를 캡처 → Room 참조 카운트가 0이 되지 않는다. 지금은 방 삭제가 없어(`RoomManager::Remove(shared_ptr)`는 빈 함수, `Remove(int)`는 호출처 없음) 문제가 드러나지 않지만, 빈 방 회수를 넣는 순간 "삭제했는데 Update가 계속 돈다"가 된다.
- 수정 방향: 반복 타이머 잡은 `weak_ptr` 소유자로 만들고(`Job`에 `weak_ptr` 버전 추가), 방 종료 플래그를 두어 `Update`가 재예약을 멈추게 한다.

### F6. 전역 객체 소멸 순서·`exit()` 멈춤 — 낮음
- `Timer::~Timer`(`Timer.h:33-39`)와 `DatabaseWorker::~DatabaseWorker`(`DatabaseWorker.cpp:22-26`)가 끝나지 않는 스레드를 join. `GTimer`·`GDBWorker`가 전역이므로 **생성 이후의 모든 `exit()`**가 정적 소멸자에서 멈춘다. 2026-10-31에 시작 실패 경로는 앞으로 옮겼지만, 런타임 경로가 남아 있다: `Listener::Dispatch`의 `exit(-1)`(`Listener.cpp:22`, 이벤트 타입 오류), `ServerService` 생성자(`ServerService.cpp:10`, 시작 시라 괜찮음). 또 Ctrl+C 핸들러는 `Logger::Shutdown` 뒤 기본 처리(`ExitProcess`)라 멈추지 않는다 — 확인함.
- 수정 방향: (현실안) 전역을 `shared_ptr`로 두지 말고 `main` 안에서 만들어 `leak`하거나, 소멸자에서 join 대신 stop 플래그 + CV notify + join. (현업) 명시적 `Shutdown()` 순서(리스너 중단 → 세션 정리 → 큐 드레인 → 타이머/DB 정지 → 로거)를 두고 `exit()` 대신 그 경로로.

---

## 5. 그 외 데이터 레이스·DB 콜백

### 확인함
- `NetStats`·`Metrics` 카운터는 모두 atomic. `SendQueueMax`/`AoiNodes`의 CAS 최대값 갱신 올바름. `SampleStat`은 뮤텍스.
- Logger: 생산자는 `gMutex` 안에서 `push_back`만, 소비자는 swap 후 락 밖에서 포맷·출력. `Shutdown`은 `gRunning` 가드로 중복 호출·콘솔 핸들러 스레드와 atexit 동시 호출에 안전. `Record::category`는 `const char*`인데 모든 호출이 리터럴이라 수명 문제 없음(규칙으로 남겨 둘 것).
- `Utils::GetRandom`은 `thread_local` 엔진. `rand()`(`Room::SpawnMonster`, `NavmeshManager`)는 MSVC CRT가 스레드별 상태라 레이스는 없음(품질 문제만).
- `DataManager` 테이블은 시작 시 1회 채우고 이후 읽기만 → 안전. `dtNavMesh`는 읽기 전용 공유, `dtNavMeshQuery`는 Room마다 하나(방 잡 안에서만) → 안전.
- `AoiIndex`는 Room 잡 안에서만(주석대로) — 모든 호출처가 `Room::*` 잡 안임을 확인.
- DB: 스레드 수 = 연결 수라 `Pop()`이 null을 돌려주는 경우는 없고, `TryLogin/TrySignUP/UpdateEquipData` 모든 경로가 `Push`로 반환한다. DB 결과는 로비 잡으로만 넘어가고 세션이 사라진 경우는 `IsConnected()`로 거른다. `OnLoginSuccess` 안에서 `Player`를 만들고 `_currPlayer`를 쓰는 것이 F3.
- `Room::PlayerChat`의 `chatCooldown`, `PlayerMove`의 `moveGuard`는 방 잡 안에서만 읽고 쓴다(주석과 일치).

### F7. `IocpCore::Dispatch` 실패 분기의 null 역참조 — 낮음
- `IocpCore.cpp:36-46`: `GetQueuedCompletionStatus`가 실패하면 `over`가 null일 수 있다(핸들이 닫힘, `ERROR_ABANDONED_WAIT_0`, `INVALID_HANDLE`). `WAIT_TIMEOUT`만 거르고 나머지는 `over->_owner` 역참조. 지금은 IOCP 핸들을 닫는 경로가 없어 실해 없음. `if (over == nullptr) return false;` 추가.

### F10. `GetService()` null 미검사 — 낮음
- `Session.cpp:51,65`: `weak_ptr::lock()` 결과를 바로 `->`. 서비스가 세션보다 먼저 사라지는 경로가 없어 실해 없음. 종료 절차를 만들 때 함께.

---

## 6. 동시성 외 메모 (읽는 중 확인, 참고용)
- 플레이어 id = 세션 번호(1부터), 몬스터 id = 100001부터(`Room.h:79-83`, `Listener.h:27`) → 누적 접속 10만 회면 같은 방에서 충돌 → `AddObject` 실패로 입장 무응답. **TODO.md에 이미 기록됨.** LoadTest 5,000봇 × 20회면 도달한다.
- `Room::PlayerEnterRoom`(`Room.cpp:209-216`)이 `SC_ADD_OBJECT`(자기 자신)를 두 번 보낸다(중복 코드). 클라이언트가 중복 ADD를 무시하는지 확인 필요. P1 측정 전 지우면 송신 수가 1건 줄어 비교에 영향이 없도록 먼저 정리하는 편이 좋다.
- 유휴 세션 타임아웃이 없다(패킷을 보내지 않고 연결만 유지하는 클라이언트는 영원히 세션·방 자리를 차지). F4 수정과 함께.
- `Monster::OnDamaged`가 `Character::OnDamaged` 뒤에 `ChangeState(HIT)`를 하므로 죽은 몬스터도 상태가 바뀐다(TODO.md 33행에 기록됨).

---

## 7. P1 "송신 묶기" 설계 시 주의점

계획: 방 잡 안에서는 `Session::Send(buf, pushOnly=true)`로 큐에만 넣고, 작업 묶음 끝에 세션당 `RegisterSend` 1회.

1. **플러시 집합은 `Send(pushOnly)` 안에서 자동 등록.** 방 코드가 "누구에게 보냈는지"를 따로 모으면 빠뜨리는 경로(`Character::OnDamaged`, `Monster::StopMove`, `UpdateView` 등 Room 밖 클래스에서 보내는 곳이 많다)가 생긴다. ServerCore에 `thread_local` 보류 집합(예: `vector<shared_ptr<Session>>` + 세션에 `atomic<bool> _flushPending` 중복 방지)을 두고 `Send(pushOnly)`가 거기에 넣게 한다. 플러시도 ServerCore(`JobQueue::ExecuteJobs` 안의 훅)에서 하면 계층 규칙(ServerCore → 게임 금지)을 지킨다.
2. **플러시는 기존 CAS 프로토콜을 그대로 쓴다.** `if (_sendRegistered.CAS(false,true)) RegisterSend();`. 이미 송신 중(플래그 true)이면 `ProcessSend`의 재검사가 큐를 이어서 보내므로 데이터는 잃지 않는다. 플러시를 안 하고 넘어가면 "송신 중이 아닌 세션"만 멈춘다 — 즉 유실이 아니라 **지연**이지만, 다음 일반 `Send`가 올 때까지 무한정 밀릴 수 있어 플러시 누락 = 버그.
3. **플러시 시점 = 드레인 루프 끝만으로는 부족하고, 잡마다 플러시만으로는 효과가 없다.** 핫존에서 이동 1건은 99명에게 1패킷씩이라 잡 단위 플러시는 지금과 같은 `WSASend` 99회다. 묶음 효과는 *여러 잡*(여러 플레이어 이동, `Update`의 몬스터 이동 여러 건)이 같은 수신자에게 쌓일 때 난다. 따라서 `ExecuteJobs`에 **잡 N개(예: 32~64) 또는 시간 T(예: 1ms) 상한**을 두고 상한마다 플러시 → 계속 드레인. 이렇게 하면 F5(독점)와 함께 해결되고, 추가 지연은 ≤T로 묶인다. 상한 값은 매트릭스(핫존 100·분산 1,000)로 측정해 고른다.
4. **플러시는 `_isProcessing.store(false)` 전에.** 플러시 중 `WSASend` 실패 → `Disconnect → OnDisconnected → room->PushJob(Leave)`가 *지금 실행 중인 큐*로 들어온다. 플러시가 플래그 해제 뒤에 있으면 그 잡이 다른 워커에서 동시에 돌 수 있다(큐 이중 실행). 순서: 드레인(또는 상한) → 플러시 → `store(false)` → 재검사.
5. **보류 집합은 RAII로 반드시 비운다.** 예외·조기 반환으로 `thread_local` 집합에 `shared_ptr<Session>`이 남으면 그 워커가 사는 동안 세션이 누수된다(F4와 결합하면 소켓까지). 잡 실행 전후를 감싸는 가드 객체로.
6. **Disconnect와의 경쟁:** 플러시 시점에 `IsConnected()==false`면 `RegisterSend`가 플래그를 true로 둔 채 돌아온다(지금도 같음, 무해). 큐에 남은 버퍼는 `~Session`에서 통계만 차감. 다만 F4가 있는 한 "끊겼지만 완료가 안 온 세션"에 pushOnly로 계속 쌓이지는 않는지 확인 — `Send` 첫 줄의 `IsConnected()`가 막는다.
7. **다른 큐(로비·DB)와 타이머 스레드는 pushOnly를 쓰지 않는다.** 로비 잡도 같은 `ExecuteJobs` 훅을 지나므로 써도 되지만, 플러시 훅이 모든 `JobQueue`에 붙어 있어야 한다. DB 스레드·Timer 스레드·Metrics 스레드는 `ExecuteJobs`를 거치지 않으므로 거기서 `Send(pushOnly)`를 부르면 영영 플러시되지 않는다. 현재 그 스레드들에서 `Send`를 부르는 곳은 없다(확인함) — 주석·assert로 못 박을 것.
8. **순서 보장:** 세션별 순서는 `concurrent_queue`가 지키고, 플러시는 큐를 통째로 `WSABUF` 배열로 넘기므로(`RegisterSend` 기존 동작) 섞이지 않는다. 로비 잡의 일반 `Send`가 방의 pushOnly 버퍼까지 함께 실어 보내는 경우도 순서는 그대로.
9. **통계 해석:** `sendQ(total/sessionMax)`에 플러시 전 묶음이 잡혀 값이 커진다. 리포터는 비동기라 순간값이지만, 비교 전후 의미가 달라지므로 TESTING.md에 적어 둔다. `MessagesOut`(Send 호출 수)은 그대로, `WSASend` 호출 수를 새 카운터로 넣어야 P1 효과가 수치로 보인다.
10. **현업 참고:** 더 나아가면 송신 전용 스레드(세션별 송신 큐를 모아 `WSASend`)나 `TransmitPackets`를 쓰지만, 포트폴리오 규모에서는 3번의 "상한 + 플러시"로 충분히 커널 호출 수를 줄일 수 있고 코드 변경이 ServerCore 두 파일(`Session.cpp`, `JobQueue.h`)에 그친다. F1/F2의 `PlayerEnterRoom` 방어와 F5의 상한을 P1과 같은 커밋에 넣으면 측정이 한 번으로 끝난다.

---

## 8. 재현 도구 요약
- 결정적(F1): Python 원시 소켓 — 로그인 후 `[깨진 CS_MOVE][CS_ENTER_ROOM]`을 한 `send()`로. 기대: `sessions=0 players=1`이 리포트에 남음.
- 확률적(F2·F3): `LoadTest --bots 1000 --chaos` 램프 중 Ctrl+C 반복 ×10, 또는 Python으로 `CS_LOGIN+CS_ENTER_ROOM` 파이프라인 ×500 후 `players` 잔류 확인.
- 자원(F4): Python 수신 중단 클라이언트 + 봇 50명 시야 안 이동 → 서버 쪽 Disconnect 유도 후 핸들 수(`Get-Process GameServer | % Handles`)·`sendQ(total)` 변화.
- 종료(F6): 디버거로 `Listener::Dispatch`에 강제 진입하거나 임시로 `exit(-1)` 호출 → 프로세스가 `~Timer`에서 멈추는지.
