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
- `DevCommands=true`(개발 지급 명령 `CS_DEV_GIVE`를 받음)
- `DbItemFailEvery=<n>`(아이템 트랜잭션을 n번에 한 번 일부러 롤백 — DB 실패 때 메모리·DB가 그대로인지 시험, D-162).

## 기능별 봇 인자

- 개발 지급·칸 옮기기: `--give-gear`·`--give-interval`·`--slot-interval`(서버 `DevCommands=true` 필요)
- 상점: `--shop-interval`
- 포털: `--portal-interval`(가장 가까운 포털에 요청 — 반경 밖이면 거부되므로 `DummySpawnZone`으로 포털 앞에 세움)
- 핑: `--ping-interval`(기본 1초, 서버 유휴 타임아웃 60초보다 짧아야 함)

## 읽는 법

- 사냥 봇은 쿨타임을 보지 않아 공격 요청의 약 77%가 거부된다(정상, LoadTest 요약 `[summary] attack:`).
- 봇은 공격하면서 움직여 잠금 중 이동이 보정된다(`move(...speed...)` 증가).
- 접속이 모두 끝난 뒤 `world(roster/chMax)`가 0이 아니거나 `login(forced)`가 0이 아니면 정리 신호가 빠진 경로가 있다.
- 끊긴 세션이 실제로 사라졌는지는 `conn(sessionObjs)`(바닥 1)·핸들 수·`sendQ(total)`로 본다.
