# ROADMAP

포트폴리오 MMORPG(서버·클라이언트 구조 시연: 몬스터 사냥 → 아이템 드랍 → 유저 거래 → 동접 테스트)의 진행 순서입니다. 설계는 `DESIGN_*.md`, 확정된 결정은 [DECISIONS.md](DECISIONS.md)에 둡니다.
**한 번에 하나의 체크박스만 진행하고, 완료하면 즉시 `[x]`로 갱신합니다.**

두 트랙을 병행합니다.
- **트랙 A — 서버/포트폴리오** (아래, 우선): 안정화 → 몬스터·전투 → 아이템 → 거래/경매장 → 부하 테스트 → 산출물
- **트랙 B — 콘텐츠/클라이언트** (그 아래 Phase M0~M4, 기존 Test 병합 작업): 영웅·무기·애니메이션·데이터 파이프라인

현재 위치: **Phase M0.5 클라이언트 정리 (2026-10-21 우선순위 변경: 폴더 정리 → 캐릭터 전투 보완 → A2.3)** / 트랙 B는 Phase M0 – 에셋 정리 중

**실행 순서:** A0(치명 오류 + 최소 검증 하니스) → **A1.4 서버 CSV 로더(2026-10-20 앞당김)** → **A2′ 몬스터·전투(서버+클라이언트, 최우선)** → A1 나머지 → A3 아이템 → A4 거래/경매장 → A5 본 벤치마크 → A6 나머지 UI → A7 산출물

**검증 게이트 (모든 콘텐츠 단계 공통):** 체크박스는 해당 기능의 **오류 테스트**(비정상 입력, 중간 접속 해제, 중복·상태 불일치 요청)와 **부하 테스트**(단계 목표 CCU까지 램프, 200 → 1,000 → 5,000)를 통과하고 결과를 `Docs/TESTING.md`에 기록한 뒤에만 `[x]`로 표시합니다.

## ▶ 중단점 (2026-10-21) — "M0.5 이어서 진행"이라고 하면 여기서 재개 (M0.5 항목 1 이후는 아래 Phase M0.5 참고, 그다음 A2.3)
**상태:** A2.2 서브 단계(a~d) 완료, M0.5 항목 0·4·3·2 완료. 2026-10-21 사용자 확정 순서: **M0.5 항목 4(완료) → 3(완료) → 2(완료) → 1 → 5** (Content 정리 → 명명/배치 → `JMPlayer.cpp` 버그 → 네트워크 안정화 → 레이어 분리). 그다음 캐릭터 전투 보완(서 있을 때 상체 블렌드, 장판 이펙트 에셋), 그다음 **A2.3 경험치/레벨/골드**(보상 패킷, HUD, 스탯 반영 대미지).
**다음 작업:** M0.5 항목 5(레이어 분리). 항목 1은 코드 완료 상태이며 Play 확인(서버 종료 감지, PIE 반복, 재접속)이 남았습니다. 범위가 크므로 먼저 코드를 읽고 계획을 제시합니다. `Content/` 구조 기준은 `DECISIONS.md` 2026-10-21 폴더 구조 항목 참고. 항목 4·3·2 변경은 아직 커밋하지 않았습니다.
**Claude가 못 보는 것:** 에디터의 애니메이션·이펙트 모양. 스크린샷이나 블루프린트 노드 복사 텍스트(Ctrl+C)를 붙여넣어 확인합니다.
**주의:** UE 빌드는 에디터가 켜져 있으면 Live Coding 때문에 막히니, C++를 고칠 일이 생기면 에디터를 닫아야 합니다. 서버는 `Server/x64/Debug/GameServer.exe`(SQL Server Express 필요). 궁수 계정은 `LoadTest --hero archer`의 `arc_*` 계정이나 UE 로그인 화면에서 궁수 타입으로 가입하면 됩니다.
**참고:** 마지막 커밋 `ede6f6e`. 이후 변경(Paragon·StarterContent 437개 추적 해제, `.gitignore`에 `Characters/`·`FirstPerson/` 추가, 문서 2개)은 아직 커밋하지 않았습니다.

## 트랙 A — 서버/포트폴리오

### A0. 서버 치명 오류 수정 + 최소 검증 하니스 (빠르게, 범위 최소)
- [x] A0.1 `Session::Disconnect` null 체크 (`Session.cpp:60`). 완료: 접속 후 즉시 끊어도 서버 생존 (2026-10-20, `TESTING.md` 기록)
- [x] A0.2 `header.size` 하한/상한 검증, 핸들러 배열 경계(서버·UE 클라이언트). 완료: size=0, id=65535 패킷에도 행·OOB 없음 (2026-10-20, 서버 런타임 검증, UE 클라이언트는 컴파일만 확인 — `TESTING.md`)
- [x] A0.3 `JobQueue` 깨우기 누락, 송신 큐 잔류, Timer busy-spin 수정. 완료: 부하 중 로그인 결과 지연·CPU 100% 없음 (2026-10-20, 유휴 CPU 확인 + LoadTest 1,000봇 45초 검증, `TESTING.md`)
- [x] A0.4 `dt` 클램프 복구(`Room.cpp:55`), `StatComponent` 초기화(null 안전), `GameMap` 경계 검사(`ToCellPos` 클램프), 가입 시 스탯 없는 PlayerType 거부 (2026-10-20, 빌드·오류 테스트 통과. `dt` 클램프와 맵 밖 좌표는 직접 재현 수단이 없어 코드 리뷰로만 확인 — A0.6 하니스에서 재확인)
- [x] A0.5 Room 입장 경쟁 조건(정원·방 생성 락) + 맵 리소스 로딩 공유. 완료: 동시 접속 100 → 방 수 정확 (2026-10-20: 250명 → 3개, 1,000명 → 10개 방, `TESTING.md`. 네비메시는 공유, 로직 JSON은 방마다 읽음 — 작아서 유지)
- [x] A0.6 최소 검증 하니스: 헤드리스 `Server/LoadTest`(`--bots --host --ramp-rate` 등; DummyClient는 CP949·OpenGL 결합으로 새 프로젝트로 대체), 서버 지표(`Metrics`: 세션·방·플레이어·패킷/초·틱 지연/처리시간, `metrics.csv`), `CS_PING`/`SC_PONG` RTT 측정, 악성 패킷 주입 스크립트. 완료: 200봇 램프 + 악성 패킷 시험 결과가 `Docs/TESTING.md`에 기록됨 (2026-10-20, 1,000봇까지 확인)

### A2′. 몬스터·전투 (서버 + UE 클라이언트, 최우선)
- [x] A2.1 서버: 몬스터 사망 제거 + 리스폰(`MaxCount`, `SpawnRadius`, 지연). 클라이언트: `SC_DEAD`/`SC_REMOVE_OBJECT` 반영, 사망 몽타주 후 제거 (2026-10-20 완료: 서버 시험 + PIE 확인 — 피격/HP바/쓰러진 자세 유지/3초 후 제거/10초 후 리스폰). 과정에서 `MonsterTable`(`HitStunMs` 포함), 플레이어 공격 서버 쿨타임, `atk(req/ok)` 지표 추가, `DT_CharacterAsset` 몽타주 오등록 수정, 사망 몽타주 `Enable Auto Blend Out` 해제. 남은 개선: 사망을 AnimBP 상태(`bIsDead`)로 전환(A2.4), 공격 중 이동 시 하체 모션(AnimBP 상체 레이어)
- [ ] A2.2 전투 입력·스킬 (2026-10-20 결정: DECISIONS.md, 설계: DESIGN_COMBAT_SKILL.md). 서브 단계, 각각 검증 게이트 통과 후 완료
  - [x] A2.2a 프로토콜 + 서버 (2026-10-20 완료, `TESTING.md`): `CS_ATTACK`(skill_slot, target_id, aim_yaw, aim_x/y)·`SC_ATTACK`(aim_yaw, aim_x/y) 확장, `SkillTable` 확장(Slot·TargetingType·CastRange·DurationMs·TickMs) + 1번 스킬(402 전사 광역 강타 / 502 궁수 지면 장판), 스킬별 쿨타임, 서버 검증(사거리·대상 생존/적대·슬롯·NaN), 지면 장판(진입 즉시 + 1초 간격 피해, 3초), LoadTest `--profile hunt --hero --chaos`. **이월: 스탯을 반영한 대미지는 A2.3(레벨/성장)에서 처리**
  - [x] A2.2b 클라이언트 카메라/입력 (2026-10-21 완료: 검은사막식 3인칭 마우스 회전 카메라 + 전투 중 커서 숨김 + `Left Alt` 자유 커서, `DT_MovementTable` 데이터 기반 카메라/이동 수치, PIE 확인). 쿼터뷰 고정안은 폐기(`DECISIONS.md` 2026-10-21)
  - [x] A2.2c (2026-10-21 완료: 에디터 작업·PIE 확인. 궁수 장판 거리 고정은 `IMC_Default`의 Mouse Y에 Swizzle/Negate가 없어 카메라 피치가 0으로 고정된 것이 원인이었고, 에디터에서 모디파이어를 추가해 해결. 코드 쪽은 `ClientRestart`가 시작 피치를 덮어쓰는 문제를 다음 틱 재적용으로 수정, 장판 지점 폴백을 발밑 평면 교점으로 변경) 클라이언트 조준(크로스헤어 기준, 검은사막식): 전사 일반공격·스킬 = 카메라 방향 논타겟, 궁수 일반공격 = 크로스헤어 아래 몬스터 지정(없으면 무동작), 1번 키 = 조준 모드(범위 인디케이터) → 좌클릭 확정 시 1회 전송, 우클릭 = 취소. **C++ 작성 완료(컴파일 대기)**: `FSkillTableRow`/`DT_SkillTable`, `UDataManager::FindSkillBySlot`, `AJMMyPlayer` 조준·시전·인디케이터(디버그 도형), 카메라 피벗 높이/어깨 오프셋(`MovementTable`), `SC_ATTACK` 방향 반영, 크로스헤어 위젯 훅. **남은 것(에디터): `IA_Attack/IA_Skill1/IA_CancelAim` + `IMC_Default` 매핑, `DT_SkillTable` 임포트, `DT_MovementTable` Reimport, `WBP_Crosshair`, `BP_MyPlayer`·`BP_PControler` 지정, `BP_PControler` 좌클릭 노드 삭제**
  - [x] A2.2d 전투 연출 (2026-10-21 완료, PIE 확인): 공격 중 이동 시 상체 레이어 블렌드(AnimBP `Layered blend per bone` + `UpperBody` 슬롯, Greystone·Sparrow), 시전 중 방향 고정(`LockFacing`, `HitDelayMs`+0.2초), 장판 최소 표시(`SC_ATTACK`의 `skill_id`/`aim_x,y`로 모든 클라이언트에 디버그 원 `DurationMs` 동안 — 이펙트 에셋 교체는 이후). 남은 개선: 서 있을 때 하체 고정이 어색하면 이동 속도로 Blend Weight 제어
- [ ] A2.3 서버+클라이언트: 경험치/레벨/골드 보상 패킷과 HUD(내 HP·EXP·레벨)
- [ ] A2.4 서버+클라이언트: 플레이어 사망/부활
- [ ] A2.5 서버: DB 저장 연결(`InitFromDb`, 접속 해제·주기 저장, `SavePlayerData` SQL 수정). 완료: 재접속 시 레벨·HP·골드 유지
- [ ] A2.6 미사용 패킷 정리(`CS_LOGOUT`/`CS_CMOVE`/`CS_LEAVE_ROOM`/`SC_CHAT`): 구현 또는 제거

### A1. 관측성·설정
- [ ] A1.1 설정 파일 외부화(포트, DB 연결 문자열, 로그 레벨)
- [ ] A1.2 레벨/타임스탬프 로거로 핫패스 `cout` 대체
- [ ] A1.3 메트릭 확장: 송신 큐 깊이, DB 큐 깊이, Room 틱 p95/max
- [x] A1.4 서버 CSV 로더로 `ServerData.h` 하드코딩 대체 (**트랙 B의 M2 "서버 CSV 로더"와 같은 작업**). **A0 직후, A2′ 시작 전에 진행** — `StatTable`/`SkillTable`을 CSV로 이전. 완료: CSV 수치를 고치고 서버만 재시작하면 반영됨 + 검증 게이트 (2026-10-20: 잘못된 CSV 6종 오류 테스트, 수치 변경 반영, 1,000봇 회귀 확인 — `TESTING.md`). **`MonsterTable`(리스폰·보상 수치)은 사용처가 생기는 A2.1/A2.3에서 추가**하고 같은 로더(`CsvFile`)를 재사용

### A3. 아이템/드랍/인벤토리
- [ ] A3.1 `Docs/DESIGN_ITEMS_TRADE.md` 작성(ItemTable, DropTable, 인벤토리, 패킷, 거래 상태기계, 경매장)
- [ ] A3.2 `ItemTable.csv`, `DropTable.csv` + 로더
- [ ] A3.3 `Inventory` 컴포넌트 + `Player_Inventory` 읽기/쓰기
- [ ] A3.4 사망 시 드랍 롤 → `OBJECT_TYPE_ITEM` 월드 오브젝트(AOI 전파, 소유권, 소멸 타이머)
- [ ] A3.5 `CS_PICKUP_ITEM`, `SC_INVENTORY_UPDATE` 등 패킷 + 서버 재검증. 완료: 두 클라이언트가 같은 드랍을 경쟁해도 정확히 한 명만 획득

### A4. 거래 (A2·A3 이후)
- [ ] A4.1 1:1 거래 상태기계(요청→수락→제시/잠금→양측 확정→원자적 교환), 타임아웃, 이동·접속 해제 시 롤백. 같은 Room의 JobQueue로 직렬화
- [ ] A4.2 거래 패킷·서버 구현, `Player_Inventory` 양측 갱신 + `Trade_Log`를 한 DB 트랜잭션으로. 완료: 반복 시험에서 부분 교환·복제 없음
- [ ] A4.3 Room 간 경매장: 전역 `AuctionService`(자체 JobQueue + DB 트랜잭션), 등록 시 에스크로, 구매 시 골드·아이템 한 트랜잭션, 만료 반환. 완료: 다른 Room 두 유저의 등록→구매 성공, 재시작 후에도 유지
- [ ] A4.4 DB 작업은 `DatabaseWorker` 비동기 큐 + 결과를 Room/서비스 Job으로 복귀

### A5. 본 부하 테스트 (동접 1,000 → 5,000)
- [ ] A5.1 봇 `is_dummy` 로그인 우회 제거(별도 계정 대량 생성 또는 서버 설정 플래그로 제한)
- [ ] A5.2 봇 행동 프로파일: 이동, 몬스터 공격, 줍기, 거래/경매
- [ ] A5.3 클라이언트 지표: RTT p50/p95/p99, 접속 성공/실패, packets/s, CPU/메모리 CSV
- [ ] A5.4 단계 램프(1,000 → 2,500 → 5,000), 병목 발견→수정→재측정 기록. 시나리오: 다수의 작은 Room vs 한 Room 밀집. 필요 시 `TCP_NODELAY`, 다중 AcceptEx, 레이트 리밋, 송신 큐 상한

### A6. 클라이언트 연동 (UE, 최소)
- [ ] A6.1 인벤토리 UI(읽기 전용)
- [ ] A6.2 드랍 아이템 액터 표시·줍기 입력
- [ ] A6.3 거래 UI, 경매장 UI(기능 우선)

### A7. 산출물
- [ ] A7.1 아키텍처 다이어그램
- [ ] A7.2 벤치마크 보고서(환경, 램프 프로파일, 지연/CPU/메모리 그래프, 병목 개선 서사)
- [ ] A7.3 루트 README 재작성(서버 권위 지점, SQL Server 설정, 부하 테스트 방법, 알려진 한계)
- [ ] A7.4 선택: 시연 영상

범위 제외: 다중 맵·포털, 3명 이상 영웅·콤보·스킬트리, 비밀번호 해시·토큰(알려진 한계로 문서화), 다중 머신 확장, Prometheus/Grafana, CI, UI 연출.

---

## 트랙 B — 콘텐츠/클라이언트 (기존 Test 병합 작업, 병행)
트랙 B의 M2 "서버 CSV 로더"는 A1.4와 같은 작업이므로 중복 진행하지 않습니다. M3/M4의 테이블은 A3의 ItemTable/DropTable, A2′의 전투와 `DESIGN_DATA.md` ID 규약을 공유합니다.

## Phase M0 – 병합 준비
- [x] Test의 서버 자리(Seam) 동작을 서버 권위 동작으로 전환한다는 방침을 `DESIGN_COMBAT_SKILL.md`에 기록
- [x] Test의 작업 절차 규칙·문서 구조·데이터 규약(`Common/DataTable`)을 이 저장소에 이식
- [x] **원본 에셋 팩 재다운로드** (사용자 작업) — 완료 후 `ExternalAssets.md` 표 채움
- [ ] 이식된 `MovementTable.csv`를 UE DataTable로 임포트할 구조(Row 구조체, `UDataManager`) 설계

## Phase M0.5 – 클라이언트 정리 (2026-10-20 검토 결과, 상세 항목은 아래 체크박스 단위로 진행)
- [x] 0. `.gitignore`: `Content/Paragon*/`, `StarterContent/` 추가 완료 (2026-10-20). 추적 중이던 437개(Paragon* 435 + StarterContent 2, 720MB)는 `git rm -r --cached`로 인덱스에서만 제거, 디스크 파일 유지 확인 (2026-10-21, 커밋 대기). 과거 커밋에는 남아 있어 저장소 크기는 줄지 않음
- [ ] 1. 네트워크 안정화: 패킷 크기 검증, 수신 스트림 어긋남, `Disconnect` 스레드 종료 대기, `Deinitialize`, 재접속 정리, 핸들러 반환값·경계 처리 — **코드 완료·빌드 성공 (2026-10-21), Play 확인 대기** (정상 흐름, 서버 종료 시 `Connection to server lost` 1회, PIE 반복 시작/종료, 재접속). 끊김 후 화면 처리(메시지·로그인 복귀)는 사용자 결정으로 추후 구현
- [x] 2. `JMPlayer.cpp` 몽타주 로드 버그·조기 return 수정, 죽은 코드·미사용 include 삭제 — 완료 (2026-10-21, 빌드 + Play 확인). 피격 몽타주 로드 조건(`HitMontage.IsPending()`), `Super::SetPlayerData` 선행 호출, 중복 로드 핸들 정리, Row 없음 경고 로그. 미사용 include는 없었음
- [x] 3. 명명/배치: `NpcCharaceter` 오타·위치, `CharacterAssetData.h` 위치, CP949 주석 UTF-8 변환 (BP 참조는 에디터에서 안내) — 완료 (2026-10-21, 빌드 + Play 확인). `ANpcCharacter`(`Character/`), `CharacterAssetData.h`(`Data/`), CP949 8개 파일 UTF-8(BOM) 변환, `DefaultEngine.ini`에 `[CoreRedirects]` 클래스 리디렉터 추가, `BP_Npc` 재저장
- [x] 4. Content 정리(에디터 수동): WBP 중복 통일, 깨진 참조(`ABP_Base`, `BP_Player`, `DT_Appearance/Equip`) 제거, 잔재 에셋·`Character/` 루트 정리, Config(기본 맵·레거시 입력) — 완료 (2026-10-21, Play 확인 통과). WBP는 이미 `_Core/UI/` 한 벌. `Maps/TestMap`은 사용자 결정으로 유지
- [ ] 5. 레이어 분리: 송신 파사드, 핸들러 보일러플레이트, UIManager 일원화 (M2 `UDataManager`와 함께)
- 이월: 서버 권위 이동·하드코딩 이전 → `Docs/TODO.md` 기록. `ParagonSample` 맵·`_Core/Materials·Environment`는 `NetworkManager.cpp:64`가 인게임 레벨로 하드코딩해 사용 중이므로 자체 제작 맵으로 교체한 뒤 제외·삭제

## Phase M1 – 에디터 검증 (JH_MMO에서 진행, 에셋 재다운로드 이후)
- [x] 1. 기본 무기 숨김: 본 스케일 0으로 무기만 사라지는지 (Greystone, Sparrow) — 통과 (현재는 숨길 필요 없음)
- [x] 2. 무기 분리 — 폐기 (활·방패 분리가 막힘, 영웅 고유 무기만 쓰기로 함)
- [x] 3. 교차 리타게팅 — 폐기 (같은 이유)
- [x] **캐릭터 베이스 결정: Paragon 영웅 + 고유 무기** (`DECISIONS.md` 2026-10-17)

## Phase M2 – 데이터 파이프라인
- [ ] Row 구조체 + `UDataManager` 이식, `MovementTable` 임포트/조회
- [ ] 서버 CSV 로더 (`Common/DataTable/*.csv`를 읽어 `ServerData.h` 하드코딩 대체), 이동 수치 연동
- [ ] `UJMAssetDataManager`의 Mock DB 교체 여부 결정

## Phase M3 – 캐릭터/무기/애니메이션
- [ ] HeroTable (Greystone, Sparrow): 영웅 → 메시/AnimBP/스탯, 영웅 선택 흐름
- [ ] 영웅별 AnimBP 연결 ([DESIGN_ANIMATION.md](DESIGN_ANIMATION.md), AnimLayer/무기 교체 없음)
- [ ] WeaponItemTable + 무기 머티리얼 교체 + 장착 CS_/SC_ 패킷, 서버 스탯 계산 ([DESIGN_CHARACTER_EQUIPMENT.md](DESIGN_CHARACTER_EQUIPMENT.md))
- [x] 모듈러 코드/에셋 삭제 (클라이언트 코드, CoreC, DT_Appearance/DT_Equip). 남은 일: 에디터 컴파일 확인, `BP_Player`/`BP_MyPlayer`/`ABP_Base` 정리, `appearance_items` proto 제거
- [ ] 사용 중인 Paragon 에셋을 새 폴더로 정리해 커밋 (재다운로드 파일은 커밋하지 않음)

## Phase M4 – 전투/스킬 (서버 권위)
- [ ] SkillTable(영웅 종속, `HeroId`) + 스킬 시전 CS_/SC_ 패킷 ([DESIGN_COMBAT_SKILL.md](DESIGN_COMBAT_SKILL.md))
- [ ] AnimNotify는 시전·연출 타이밍만, 판정은 서버

## 수동 작업 기록 (에디터에서 사용자가 직접)
상세 절차는 기록하지 않고 대화창으로 안내합니다. 여기에는 항목별 한 줄 요약과 상태만 남깁니다.

- [x] 검증 1: 기본 무기 본 스케일 0 숨김 (통과, 현재는 사용하지 않음)
- [x] Greystone, Sparrow: 몽타주 Duplicate(`AM_`), AnimBP·피격·사망 Rename, `DT_CharacterAsset` 경로 갱신 (2026-10-19 완료)
- [ ] Rampage: 사용 에셋 확인(`BP_Npc`) 후 같은 방식으로 처리 — 몽타주 Rename은 완료, `BP_Npc`의 `CurrentAttackMontage`는 아직 팩 원본(`Attack_Biped_Melee_A_Montage`)을 참조하고 사망/피격 몽타주는 미지정 (A2.1에서 지정)
- [ ] Paragon 에셋 최종 Move: 캐릭터 작업 종료 시 (아래 중단점 4번 참고)
- [x] M0.5-4 Content 정리 (2026-10-21): `Input/Touch` 삭제, `BP_JMGameInstance`의 깨진 변수 `DT_Appearance/Equip` 제거, `BP_Player`/`BP_MyPlayer`의 `ABP_Base`·Mannequin 참조 해제 후 `ABP_Base`·`NewInterchange…` 삭제, `BP_MyPlayer/Player/Npc` → `_Core/Blueprints/Character/`, `DT_CharacterAsset` → `Data/` 이동(커맨드렛 Python, 리디렉터 없음), `DefaultInput.ini` 레거시 Action/Axis Mappings 삭제, `Characters/`·`FirstPerson/`·`LevelPrototyping/` 삭제

## ▶ 중단점 (2026-10-17 종료 시점) — "에셋 정리 이어서 진행"이라고 하면 여기서 재개
**목표:** 재다운로드한 Paragon 전체 팩은 커밋하지 않고(`.gitignore`, 완성 후 제거), 실제 쓰는 에셋과 의존 에셋만 `Content/Character/<영웅이름>/`(Greystone, Sparrow, Rampage)로 **Move**해 커밋한다. 원본은 복원·백업하지 않으며 Move 후 Fix Up Redirectors를 실행한다 (`DECISIONS.md` 2026-10-17).

**확인된 기준 에셋 (`DT_CharacterAsset`)**
- Greystone: 메시 `/Game/ParagonGreystone/Characters/Heroes/Greystone/Meshes/Greystone`, 공격 몽타주 `.../Greystone/Animations/Attack_PrimaryA_Montage`, AnimBP·피격·사망은 이미 `Content/Character/Greystone/`
- Sparrow: 메시 `/Game/ParagonSparrow/Characters/Heroes/Sparrow/Meshes/Sparrow`, 공격 몽타주 `.../Sparrow/Animations/Primary_Fire_Med_Montage`, AnimBP·피격·사망은 이미 `Content/Character/Sparrow/`
- Rampage(몬스터): 사용 에셋 미확인 (`Content/Character/BP_Npc` 확인 필요)

**재개 시 순서 (한 단계씩 사용자에게 묻고 진행)**
> 2026-10-19 방침 변경 (`DECISIONS.md`): 팩은 캐릭터 작업 종료까지 유지. 고칠 에셋(몽타주·AnimBP·피격·사망)은 `Content/Character/<영웅>/`에 **Duplicate**, 무거운 에셋(메시·스켈레톤·머티리얼·텍스처·시퀀스)은 캐릭터 작업 종료 시 한 번에 **Move**.

1. Step 2 — Greystone: 공격 몽타주 등 고칠 에셋을 `Content/Character/Greystone/`에 Duplicate(네이밍 규칙 적용), `DT_CharacterAsset` 경로를 복사본으로 변경. 네이밍 규칙(SK_/ABP_/AM_/M_/T_)을 먼저 `DECISIONS.md`에 확정 (사용자 결과 대기 중)
2. (완료 2026-10-19: Greystone·Sparrow Step 2) 활/화살 `FX/Meshes/Heroes/Sparrow/Abilities/SM_Sparrow_Arrow`, 검 `FX/Meshes/Heroes/Greystone/SM_Greystone_Blade_01` 사용 여부는 **미정** — 스킬 작업(M4) 때 결정, 쓰기로 하면 최종 Move 대상에 포함
3. Rampage 사용 에셋 확인 후 처리
4. 캐릭터 작업 종료 시: 영웅 폴더와 `Characters/Global`을 Move → Fix Up Redirectors → `DT_CharacterAsset` 확인 → Save All → `Paragon*` 삭제 → PIE 확인 (Reference Viewer로 의존 확인)
5. 영웅 하나가 끝날 때마다 `Content/Character/**`와 소스·문서만 체크포인트 커밋
6. 정리 후 `git status`로 남은 변경(재다운로드 파일)을 확인하고, 커밋 범위를 사용자와 정해 `Content/Character/**`와 소스/문서만 커밋. 원본 `Paragon*` 폴더의 재다운로드 변경분은 커밋하지 않음(복원 또는 제외 방식은 그때 결정)

**그 외 남은 일**
- 에디터 컴파일 확인(모듈러 코드 삭제 후), `BP_Player`/`BP_MyPlayer`/`ABP_Base`가 쓰이는지 확인 후 정리
- `appearance_items` proto 제거(`GanPackets.bat` 필요)
- 이후 M2: Row 구조체/`UDataManager` 이식, 서버 CSV 로더
