---
name: load-test
description: LoadTest·DummyClient 봇으로 부하 시험, 연결 혼란 시험, 드랍 롤 시험을 돌릴 때 사용. 서버 [Debug] 시험 키, 봇 인자, 시험 스크립트 위치, 지표 읽는 곳.
---

# 부하·혼란 시험

봇 사용법과 지표 열 설명, 측정 자료 보관 규칙은 `Docs/TESTING.md` "부하 테스트 하니스"가 기준이다. 서버 빌드·실행·종료는 `run-server` 스킬. 시험용 설정은 커밋하지 않는 `Server/Server/ServerConfig.local.ini`나 `--config <경로>`로 준다.

## 시험 스크립트

시험 스크립트는 `Server/PerfData/2026-11-19_A5.0/`(원시 소켓 `rawsock.py`·`stall_recv.py`·`idle.py`·`login_loop.py`, 실행 `f4.sh`·`verify.sh`·`chaos.sh`).

## 드랍 롤 시험

`GameServer.exe --roll-drops <횟수>`는 DataTable만 읽고 드랍 롤 빈도를 로그에 남긴 뒤 종료합니다(DB·포트를 열지 않음, 잘못된 CSV 시험에도 씀).

## ServerConfig `[Debug]` 키

- `DummySpawnZone=x,y,r[,n];...`(더미 봇 로그인을 들어오는 순서대로 앞 구역부터 n명씩 원 안에 배치 = 핫존·시야 경계 시험, 비우면 맵 전체 무작위)
- `ViewAudit=true`(방마다 1초에 한 번 플레이어 시야 목록을 실제 거리와 비교 — 거리 ≤ `WorldTable.ViewRadiusCm`인데 없으면 누락, > R+`ViewHysteresisCm`인데 있으면 초과, `[metrics] viewAudit(must/missing mustNot/extra)` 누적과 처음 10건 WARN 로그. 전수 검사라 부하 측정 때는 끈다)
- `DbSaveDelayMs=<ms>`(아이템 트랜잭션·진행 상황 저장 실행 전에 멈춤 — 빠른 재접속·중복 접속·결과 도착 전 접속 해제 시험용, DB 작업 스레드를 막으므로 평소에는 0)
- `DummyRoom=town|field|split`(DG3b — 더미 봇이 처음 들어가는 Room: `town` = 마을(실제 규칙, 사냥 봇은 문 포털로 나감), `field` = 사냥터 Room에 바로(걷는 영역 무작위 또는 `DummySpawnZone`), `split` = 번갈아. `DummySpawnZone` 좌표는 그 Room의 구역 안이어야 함)
- `MoveHandoffDelayMs=<ms>`(DG3b — Room 이동의 잡 사이(서비스 → 떠나기, 떠나기 → 들어오기)를 늦춤: 그 구간의 접속 해제·사망·중복 요청 시험용)
- `PortalNoRange=true`(DG3c — 포털 사용 요청의 거리 검사만 건너뜀: 같은 구역 Room 안이면 어디서든 그 포털을 요청. 던전 입구 100이 사냥터 북서쪽 끝이라 원시 소켓 클라이언트가 걷지 않고 요청하고, 봇이 던전 출구를 요청할 때 씀)
- `DevCommands=true`(개발 지급 명령 `CS_DEV_GIVE`를 받음)
- `DbItemFailEvery=<n>`(아이템 트랜잭션을 n번에 한 번 일부러 롤백 — DB 실패 때 메모리·DB가 그대로인지 시험, D-162).

경매 혼잡 기준은 시험 키가 아니라 운영 값 `[Database] AuctionDbMaxInFlight`(기본 16, 0 = 끔 — D-235)입니다. `DbSaveDelayMs`와 함께 1로 낮추면 `TRY_LATER` 경로를 볼 수 있습니다(`DbSaveDelayMs`는 수령함 읽기에도 걸림).

## 기능별 봇 인자

- 개발 지급·칸 옮기기: `--give-gear`·`--give-interval`·`--slot-interval`(서버 `DevCommands=true` 필요)
- 상점: `--shop-interval`
- 포털: `--portal-interval`(가장 가까운 포털에 요청 — 반경 밖이면 거부되므로 `DummySpawnZone`으로 포털 앞에 세움)
- Room 이동(DG3b): 문 포털을 쓰면 `SC_TELEPORT` 대신 `SC_ROOM_CHANGE`가 옵니다 — 요약 `[summary] room: roomChange=… moveRejected=…`. hunt 봇은 마을에 있으면 가장 가까운 문 포털(101~103)로 걸어가 사냥터로 나가고, 죽어 마을에서 부활하면 다시 나갑니다.
- Room 이동 오류 시험(원시 소켓, `Server/PerfData/2026-11-20_DG3b/`): `bash run_move_tests.sh [Debug|Release] [plain wait timeout handoff trade revive]` — 묶음마다 맞는 `[Debug]` 설정으로 서버를 띄워 `move_test.py` 시나리오를 돌리고 DB 아이템·골드 합계(`sqlcmd`)와 `[metrics]` 잔류·`roomMove`의 `abortBusy/orphan/ownerBad` 0을 판정합니다. 공용 코드 `movekit.py`(protobuf 해석, 걷기 — 서버의 이동 보정 `SC_MOVE`를 반영해야 이후 이동이 버려지지 않음), 측정 `run_one.sh`(`DUMMYROOM=`·`SERVER_EXE=`·`DATA=`)·`run_matrix.sh`·`summarize.py`·`chaos.sh`.
- 파티(DG2): `--party-size N`(봇을 번호순으로 N명씩 묶어 묶음의 첫 봇 — 또는 지금 파티장인 봇 — 이 2초마다 한 명씩 오브젝트 id로 초대, 받은 초대는 모두 수락)·`--party-churn-sec S`(이 간격 ±50%마다 탈퇴 → 다시 초대됨: 위임·해산·거부 경로). 요약 `[summary] party: inParty=… by PartyResult: …` — 탈퇴를 섞으면 `TARGET_IN_PARTY`·`NOT_LEADER`·`TARGET_BUSY` 거부가 나오는 것이 정상. 더미 봇은 소유권 없는 드랍만 만들어 파티 공유 줍기는 지나가지 않음.
- 파티 시험(원시 소켓, `Server/PerfData/2026-11-20_DG2/`): `bash run_party_tests.sh [Debug|Release] [plain loot small channel midmove]` — 묶음마다 설정·데이터 테이블 사본(`JH_DATA_DIR`)으로 서버를 띄워 `party_test.py` 시나리오를 돌리고 `[metrics]` 잔류(`party(now …)` 0/0/0 포함)를 판정합니다. 공용 코드는 DG3b의 `movekit.py`. 기다리기만 하는 클라이언트도 5초마다 핑을 보내야 유휴 타임아웃(60초)에 끊기지 않고, 시험 계정은 약해서 몬스터 처치는 시나리오당 한 번만(죽으면 마을에서 부활해 돌아옴). 부하 `run_load.sh`·`summarize.py`.
- 던전(DG3c-1): `--dungeon`(사냥터 Room에 있으면 입구 포털 100을 요청 — `--party-size`와 함께면 묶음이 다 모인 파티장만, 파티원은 따라 들어감. 던전 밖에서는 공격하지 않음 — 시전 중이면 서버가 포털 요청을 버림)·`--dungeon-stay-sec S`(들어간 뒤 이 시간 ±25%에 출구 900을 요청하고 다시 들어감 — 서버 `PortalNoRange=true` 필요)·`--dungeon-map <던전 로직 JSON>`(던전 안 hunt 봇이 찾아갈 스폰). 봇은 레벨이 다른 `SC_ROOM_CHANGE`를 받으면 곧바로 `CS_ENTER_ROOM`(로딩 끝)을 보냅니다. 입구 앞에 세우려면 서버 `DummyRoom=field` + `DummySpawnZone=19020,-18800,300`(봇 `--zone`도 같은 값). 요약 `[summary] dungeon: enterSent=… entered=… exitSent=… cleared=… inDungeonNow=…`.
- 던전 시험(원시 소켓, `Server/PerfData/2026-11-20_DG3c/`): `bash run_dungeon_tests.sh [Debug|Release] [plain party handoff limits clear]` — 묶음마다 설정·데이터 테이블 사본(`tweak_tables.py`)으로 서버를 띄워 `dungeon_test.py` 시나리오(혼자 입장·출구, 격리, 파티 동반 입장·탈퇴 시 내보냄, 로딩 대기 중 끊김·중복 로그인·시한, 인계 구간 끊김, 상한, 클리어)를 돌리고 DB 아이템·골드 합계와 `[metrics]` 잔류를 판정합니다: `dungeon(now/created/closed players)`의 `now` 0·`created` = `closed`, `roomsAlive` = `rooms` = 시험 전 값, `monsters` = 시험 전 값, `drops` 0. 모든 묶음은 `PortalNoRange=true`. 시험 클라이언트는 레벨이 바뀌면 `CS_ENTER_ROOM`을 보내야 들어가집니다(`auto_load`). 측정은 `run_load.sh`(회귀 `hunt_250`·`spread_250` + 인스턴스 50·100·250개 + 입장·퇴장 반복) → `summarize.py`.
- 경매 시험(A4.3, 원시 소켓, `Server/PerfData/2026-11-21_A4.3/`): `bash run_auction_tests.sh [Debug|Release] [plain race expire limits fail busy cut restart]` — 묶음마다 **서버를 띄우기 전에** `reset_db.sql`로 시험 계정(Test1~Test4)의 경매 흔적(등록·우편·경매 보관/수령함 아이템·지급받은 시험 아이템)을 지우고 골드를 1000으로 맞춘 뒤, 설정·`EconomyTable` 사본(`tweak_tables.py <폴더> <열=값>…`)으로 서버를 띄워 `auction_test.py` 시나리오를 돌립니다(`restart` 묶음은 중간에 서버를 다시 띄움). 판정: 시나리오마다 DB 합계(아이템 수량 합, 보유 골드 + 수령함 골드 — 지급한 만큼만 증가)와 경매 데이터 정합(`db_broken` 0), 묶음마다 `[metrics]` 잔류 0·`auction(... dbInFlight)` 0·서비스 목록 수 = DB의 판매 중 등록 수·ERROR 0. 모든 묶음은 `DevCommands=true`·`PortalNoRange=true`. 경매 NPC까지는 꺾임점을 따라 걷습니다(`route_to_npc` — 마을 소품을 옮기면 함께 고침). 회귀(앞 항목의 시나리오 + 더미 250명 사냥 램프)는 `run_regress.sh`. 더미 봇은 검색(`CS_AUCTION_SEARCH`)만 받아지고 봇의 경매 프로필은 아직 없습니다(A5.2).
- 핑: `--ping-interval`(기본 1초, 서버 유휴 타임아웃 60초보다 짧아야 함)

## 읽는 법

- 사냥 봇은 쿨타임을 보지 않아 공격 요청의 약 77%가 거부된다(정상, LoadTest 요약 `[summary] attack:`).
- 봇은 공격하면서 움직여 잠금 중 이동이 보정된다(`move(...speed...)` 증가).
- 접속이 모두 끝난 뒤 `world(roster/chMax)`가 0이 아니거나 `login(forced)`가 0이 아니면 정리 신호가 빠진 경로가 있다.
- 끊긴 세션이 실제로 사라졌는지는 `conn(sessionObjs)`(바닥 1)·핸들 수·`sendQ(total)`로 본다.
