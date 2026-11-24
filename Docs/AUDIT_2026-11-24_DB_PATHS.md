# DB 경로 구조 점검 (2026-11-24, ROADMAP A4.4 · D-309)

서버의 모든 DB 경로가 비동기 구조를 지키는지 코드로 점검한 결과입니다(판독 전용 — 판독 에이전트가 찾고, 어긋남 A1은 Claude가 코드로 다시 확인). A7.1 아키텍처 다이어그램·A7.2 벤치마크 보고서의 근거로 씁니다. 줄 번호는 커밋 `9067974a` 기준이며, 코드가 바뀌면 함수 이름으로 찾습니다.

## 기준

1. 게임 스레드(Room 잡·로비 잡·서비스 잡·IOCP 수신 스레드)는 DB를 직접 부르지 않고 `DatabaseWorker` 큐에 작업을 넣기만 한다(`GDBWorker->PushDBJob`).
2. DB 작업 스레드가 실행한 뒤 결과를 요청한 쪽(Room·로비·경매 서비스)의 잡 큐로 `PushJob`해 돌려준다.
3. 결과가 왔을 때 요청자(플레이어·세션)가 사라졌으면 안전하게 버리거나 의도대로 처리한다.
4. 플레이어별 동시 작업 제한(`itemOp.inFlight`·저장 상태 1건)과 `Player::HasUnfinishedWork`·`NotifyClosedIfDone`에 반영돼 있다.
5. 시작 때의 동기 DB 호출은 예외로 따로 둔다(작업 스레드를 만들기 전).

## 요약

- DB 경로 **24개**(로그인 2개로 나눔): **21개 지킴, 2개 예외(의도 — 시작 때 동기), 1개 어긋남(A1)**.
- 게임 스레드에서 SQL을 직접 실행하는 곳은 없음: SQL 실행은 `DatabaseWorker.cpp` 안의 11곳뿐(ServerCore `DBConnection.cpp`의 `SQLExecDirectW` 구현 하나), `PushDBJob` 호출은 12곳(Room 4·경매 서비스 5·패킷 핸들러 2·로비 1). `WorldService`는 DB를 쓰지 않음.
- `DatabaseWorker` 공개 작업 함수 12개(`TryLogin`·`TrySignUP`·`ExecItemOp`·`ExecAuctionOp`·`ExecAuctionExpire`·`ReadMail`·`ReadListingState`·`ReadSellerListings`·`SavePlayerProgress`·`SaveQuickSlots`·`SaveQuestProgress`·`TakeInitialAuctionListings`), 아이템 작업 종류(`ItemOpKind`) 16개(Equip·Move·Pickup·ShopBuy·Discard·ShopSell·Use·Trade·Give·LoginFix·AuctionRegister·AuctionBuy·AuctionCancel·MailClaim·QuestAccept·QuestComplete).

## 큐 구조

- **연결 수 = 작업 스레드 수**: `[Database] Connections`(기본 4, 1~64). 생성자가 연결 N개와 스레드 N개를 만든다.
- **큐 하나**: `ConcurrentQ<shared_ptr<Job>> _dbJobQueue`(mutex + condvar). 스레드 N개가 같은 큐에서 꺼내므로 작업 사이의 순서는 보장되지 않는다 → 플레이어별로 동시에 1건(`itemOp`·`SaveState`)만 둔다.
- **연결 풀**: 작업마다 꺼내고 돌려줌. 스레드 수 = 연결 수이고 작업 하나가 연결 하나만 쓰므로 비는 일이 없다(불변식 — `Pop`의 nullptr는 검사하지 않음).
- **결과 전달**: DB 스레드가 `room->PushJob`(Room)·`GLobby->PushJob`(로비)·`GAuction->PushJob`(경매 서비스)으로 돌려준다. 잡 인자가 Room·Player·Session의 `shared_ptr`를 쥐고 있어 결과가 올 때까지 살아 있고, 닫힌 Room에도 `PushJob`은 그대로 돈다.
- **경매 혼잡 기준**: `[Database] AuctionDbMaxInFlight` 16(0 = 끔), 전역 카운터 `Metrics::AuctionDbInFlight` — 넘으면 `TRY_LATER`. 대상은 등록·구매·취소·수령·수령함 읽기·만료(검증·판매자 다시 읽기는 제외 — S2).
- **지표**: `dbWait`(큐 대기·큐 길이 최대 — `PushDBJob`이 `Job::_enqueueUs`를 적고 `Run`이 잼, A5.4b).

## 경로 표

공통(표에서 반복하지 않음): 아이템 작업은 모두 `Room::StartItemOp`가 `itemOp.inFlight`를 켜고 `PushDBJob(ExecItemOp)`로 보내며, 결과는 Room 잡 `OnItemOpDone` → `ApplyItemOpResult`가 반영한다. 방에 없으면 메모리·패킷 반영만 건너뛰고(DB에는 들어감, 다음 로그인 때 읽음), 결과 불명이면 접속을 끊는다. 끝에서 `NotifyClosedIfDone`·`TryAdvanceMove`를 부른다. `HasUnfinishedWork` = `itemOp || progressSave || quickSlotSave || questSave`. 다른 Room으로 온 결과(orphan)는 `CheckDbResultOwner`가 접속을 끊는다(지표 `roomMove(... orphan)` 0이 정상).

| # | 경로 | 요청하는 곳 | DatabaseWorker 함수 | 결과가 돌아오는 곳 | 요청자가 사라졌을 때 | 동시 작업 제한 | 판정 |
|---|---|---|---|---|---|---|---|
| 1 | 스키마 버전 확인 | `DatabaseWorker` 생성자(작업 스레드 생성 전) | `CheckSchemaVersion`(내부) | 동기 — 다르면 시작 중단 | — | — | 예외(의도) |
| 2 | 시작 때 판매 중 경매 목록 | `DatabaseWorker` 생성자 | `ReadListings`(내부) | `TakeInitialAuctionListings` → `GAuction->Init` | — | — | 예외(의도) |
| 3 | 로그인 | `Handle_CS_LOGIN`(IOCP 수신 스레드) | `TryLogin`(퀘스트 2차 조회 포함) | 로비 잡 `OnLoginSuccess`/`OnLoginFailed` | 끊긴 세션이면 버림, `SetPlayer` 뒤 끊겼으면 `HandlePlayerGone` | 세션당 제한 없음(중복 성공은 `GetPlayer()`로 거름 — S1) | 지킴 |
| 4 | 중복 접속 대기 뒤 재로그인 | `AuthLobby::RemoveOnline`(로비 잡) | `TryLogin` | 3과 같음 | 대기 세션이 연결돼 있을 때만 | 캐릭터당 대기 1건(`HoldLogin`) | 지킴 |
| 5 | 가입 | `Handle_CS_SIGNUP`(IOCP 수신 스레드) | `TrySignUP` | 로비 잡 `OnSignUpResult` | 응답만 버림(계정 행은 남음 — 의도) | 제한 없음(S1) | 지킴 |
| 6 | 진행 상황 저장(레벨·경험치·HP) | `Room::RequestProgressSave`(주기·퇴장·도착·퀘스트 완료) | `SavePlayerProgress` | Room 잡 `OnProgressSaved` | 방에 없으면 최신 값으로 한 번 더, 실패면 재시도 | `progressSave` 1건(진행 중이면 합침), 이동 중 미룸 | 지킴 |
| 7 | 액티브 칸 저장 | `Room::RequestQuickSlotSave` | `SaveQuickSlots` | Room 잡 `OnQuickSlotsSaved` | 6과 같음 | `quickSlotSave` 1건, 설정 요청은 이동 중 `BUSY` | 지킴 |
| 8 | 퀘스트 처치 진행 저장 | `Room::RequestQuestSave`(`CreditKill`·주기·퇴장·도착) | `SaveQuestProgress` | Room 잡 `OnQuestSaved` | 6과 같음 | `questSave` 1건, 이동 중 미룸 | 지킴 |
| 9 | 장착 | `PlayerChangeEquip` | `ExecItemOp`(Equip) | 공통 | 공통 | `itemOp`·이동 중 `BUSY`, `weaponSwapPending` | 지킴 |
| 10 | 개발 지급 | `PlayerDevGive` | `ExecItemOp`(Give, 조립식) | 공통 | 공통 | `CheckItemRequest` | 지킴 |
| 11 | 칸 이동 | `PlayerMoveItem` | `ExecItemOp`(Move) | 공통 | 공통 | `CheckItemRequest` | 지킴 |
| 12 | 줍기 | `PlayerPickupItem` | `ExecItemOp`(Pickup) | 공통 | 드랍 제거는 방에 없어도 적용, 롤백이면 `picking` 해제, 불명이면 드랍 삭제 | `CheckItemRequest` + `drop->picking` | 지킴 |
| 13 | 버리기 | `PlayerDropItem` | `ExecItemOp`(Discard) | 공통 | 공통 | `CheckItemRequest` | 지킴 |
| 14 | 상점 구매·판매 | `PlayerShopBuy`·`PlayerShopSell` | `ExecItemOp`(ShopBuy·ShopSell) | 공통 | 공통 | `CheckShopRequest` → `CheckItemRequest` | 지킴 |
| 15 | 소비 아이템 사용 | `PlayerUseItem` | `ExecItemOp`(Use) | 공통 | 공통 | `itemOp.inFlight`만 — **이동 중 검사 없음** | **어긋남(A1)** |
| 16 | 로그인 장착 보정 | `PlayerEnterRoom`(처음 입장) | `ExecItemOp`(LoginFix, 조립식) | 공통 | 실패하면 다음 로그인 때 다시 | `itemOp` 켬(처음 입장이라 이동 검사 불필요) | 지킴 |
| 17 | 1:1 거래 확정 | `TryExecuteTrade`(A는 `StartItemOp`, B는 직접 켬) | `ExecItemOp`(Trade, 조립식) | 공통 → `OnTradeDone` | 방에 남은 쪽만 반영, 불명이면 B도 끊음 | 두 사람 모두 `itemOp`, 이동이 걸리면 확정 전 거래 취소 | 지킴 |
| 18 | 퀘스트 수락·완료 | `BeginQuestAccept`·`BeginQuestComplete` | `ExecItemOp`(QuestAccept·QuestComplete) | 공통 | 완료인데 방에 없으면 보상 경험치만 `AddExp` + 진행 저장 | `CheckQuestRequest` → `CheckItemRequest` | 지킴 |
| 19 | 경매 등록·구매·취소 | `PlayerAuctionRegister`·`Buy`·`Cancel` → `StartItemOp` → 서비스 잡 `AuctionService::BeginOp` | `ExecAuctionOp` | 서비스 잡 `OnOpDone`(목록 갱신) → Room 잡 `OnItemOpDone`(서비스가 거부하면 DB 없이 바로) | 공통(서비스는 `Player`를 만지지 않음) | `CheckAuctionRequest` → `CheckItemRequest`, `TryBeginDbWork`/`EndDbWork` 짝 | 지킴 |
| 20 | 수령함 수령 | `PlayerMailClaim` | `ExecItemOp`(MailClaim, 서비스 거치지 않음) | 공통 | 공통 | `CheckItemRequest`, `TryBeginDbWork`/`EndDbWork` | 지킴 |
| 21 | 수령함 읽기(내 목록) | `PlayerAuctionMy` → 서비스 잡 `QueryMy` | `ReadMail` | 서비스 잡 `OnMailRead` → 세션에 직접 응답 | 끊긴 세션이면 버림 | `itemOp` 안 씀(결과가 세션으로 가 Room 이동과 무관), 간격 `auctionMy`, `TryBeginDbWork` | 지킴 |
| 22 | 경매 만료 | `AuctionService::CheckExpire`(자기 재예약) | `ExecAuctionExpire` | 서비스 잡 `OnExpireDone`(판매자 알림 `GWorld->NotifySeller`) | 요청한 플레이어 없음 | 한 번에 1묶음, `TryBeginDbWork`/`EndDbWork` | 지킴 |
| 23 | 경매 등록 검증(State·Count 다시 읽기) | `AuctionService::VerifyListing`(구매·취소·만료 실패 뒤) | `ReadListingState` | 서비스 잡 `OnListingVerified`(실패면 재시도) | 요청한 플레이어 없음 | 등록당 1건, 혼잡 기준 밖(S2) | 지킴 |
| 24 | 판매자 등록 다시 읽기 | `AuctionService::ResyncSeller`(결과 불명 뒤) | `ReadSellerListings` | 서비스 잡 `OnSellerRead`(실패면 재시도) | 요청한 플레이어 없음 | 혼잡 기준 밖(S2) | 지킴 |

경매 검색(`PlayerAuctionSearch`)은 서비스 메모리만 읽어 DB를 쓰지 않으므로 표에서 뺐습니다.

## 어긋남·의심

- **A1 소비 아이템 사용이 Room 이동 중인지 보지 않음(어긋남, Claude 확인)** — `Room::PlayerUseItem`은 `itemOp.inFlight`만 보고 `roomMove.Active()`를 보지 않습니다(`CheckItemRequest`·장착·액티브 칸·거래에는 있음). 이동 표시 뒤 `OnMoveResolved` 전의 틈에 사용 요청이 오면 `itemOp`가 켜져 `OnMoveResolved`가 이동을 취소합니다(`roomMove(... abortBusy)` +1). 결과는 같은 Room으로 오므로 데이터 손실·orphan은 없고, 포털·부활 이동이 시간 초과로 취소되는 불편만 생깁니다. 고친다면 이동 중이면 `BUSY`로 거부하는 한 줄(`CheckItemRequest`와 같은 위치·지표).
- **S1 로그인·가입은 세션당 진행 중 제한이 없음(경미)** — 응답 전에 같은 연결이 요청을 연달아 보내면 DB 작업이 그만큼 쌓입니다. 정합성은 `OnLoginSuccess`의 중복 거름으로 지켜짐. 악성 클라이언트의 DB 큐 점유는 추측(측정 안 함).
- **S2 경매 검증·판매자 다시 읽기는 혼잡 기준 밖(경미)** — `VerifyListing`·`ResyncSeller`는 `TryBeginDbWork` 없이 넣습니다. 실패 뒤에만 생기는 복구 읽기라 의도로 보이나 `Server/CLAUDE.md` "경매장"에는 예외로 적혀 있지 않음.
- **S3 orphan 결과에서도 옛 Room 잡이 `Player` 필드를 만짐(설계상 0건)** — `CheckDbResultOwner`가 접속을 끊은 뒤에도 저장 상태를 끄고 재요청하는 코드가 이어 돕니다. 소유 Room 규칙과 다르지만 이동 전 `HasUnfinishedWork` 검사 두 번이 막아 정상 경로에서는 일어나지 않음.
- **S4 진단 로그에 `questSave`가 빠짐(동작 영향 없음)** — 이동 취소 로그가 `itemOp`·`progressSave`·`quickSlotSave`만 출력.
- **S5 거래 상대 B의 `itemOp`는 지표에 안 잡힘(지표만)** — B는 `inFlight`만 켜고 `Metrics::ItemOpInFlight`를 올리지 않아 거래 중 `itemOp inflight`가 1 작게 보임.

## 처리 (2026-11-24 사용자 결정)

- A1: 고치지 않고 ROADMAP 백로그 "그 밖"에 적음.
- S2: 의도된 예외로 `Server/CLAUDE.md` "경매장"에 한 줄 적음.
- S1·S3·S4·S5: 이 문서에만 남김.
