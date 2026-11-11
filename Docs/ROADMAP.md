# ROADMAP

포트폴리오 MMORPG(서버·클라이언트 구조 시연: 몬스터 사냥 → 아이템 드랍 → 유저 거래 → 동접 테스트)의 진행 순서입니다. 설계는 `DESIGN_*.md`, 확정된 결정은 [DECISIONS.md](DECISIONS.md)에 둡니다.
**한 번에 하나의 체크박스만 진행하고, 완료하면 즉시 `[x]`로 갱신합니다.**

두 트랙을 병행합니다.
- **트랙 A — 서버/포트폴리오** (아래, 우선): 안정화 → 몬스터·전투 → 정리 → 서버 기반 → 성장·저장 → 아이템 → 거래/경매장 → 부하 테스트 → 산출물
- **트랙 B — 콘텐츠/클라이언트** (그 아래, 기존 Test 병합 작업): 남은 것은 영웅·무기 테이블과 Paragon 에셋 최종 Move(Greystone·Sparrow 팩은 2026-11-12 삭제, Rampage 몬스터 쪽만 남음)

현재 위치: **P. 성능 개선 (P0 유령 플레이어 수정 → P1 송신 묶기 차례, 2026-11-06 기준선·동시성 감사 완료). 2026-11-06 결정: 트랙 B를 MC4.2 무기 공격까지 먼저 진행한 뒤 트랙 A 재개 (`DECISIONS.md`)**
브랜치(2026-11-12 정리): 개발은 **`Jeonghoon` 하나**, `main`은 Phase·마일스톤 끝에 병합하는 안정판, 시험은 짧은 작업 브랜치(`exp/…`) — `DECISIONS.md` 2026-11-12. `content/dev`는 병합 후 사용 종료.
이전 `content/dev` 진행 기록: **트랙 B Phase MC(모듈러 캐릭터 전환) 진행 중 — MC1부터 재작업(2026-11-04), MC2.3 판금 세트 완료(2026-11-06), MC2.4 아이콘 완료(2026-11-07), MC3.1 데이터 테이블·MC3.1b 스탯 재설계·MC3.2 장착 패킷·MC3.3 장비 저장·MC4.1 장비 컴포넌트·MC4.1b 원격 이동 튐 완료(2026-11-07), MC4.2a 구조·맨손(2026-11-08)·MC4.2b Mixamo 시범 리타깃·MC4.2c 한손검 + 공용 피격(2026-11-09) 완료, MC4.2d-1 스킬 조준 동기화 완료(2026-11-09), MC4.2d-2 활 애니메이션·MC4.2e 장갑·어깨·MC4.2f 방패 구현 완료(2026-11-10, 확인은 중간 테스트), FX 라이브러리(MC4.5) 1~5단계 완료(2026-11-12 — 채택 51개 손질, PIE 확인·`Fx` 연결은 남음), Paragon Greystone·Sparrow 팩 삭제(2026-11-12 — FX 의존 515개를 `Content/FX/Dep`로 Move), 다음 **MC4.2 중간 테스트**(U8 Mutable은 보류 — `DECISIONS.md` 2026-11-05). 2026-11-05 UE 5.8 업그레이드와 함께 `main`·`Jeonghoon`·`content/dev`를 한 지점(U7)으로 맞춤** (트랙 A P1도 같은 `Jeonghoon`에서 이어감)

**실행 순서 (2026-10-30 재계획, `DECISIONS.md`):** 완료: A0, A1.4, A2.1~A2.2g, M0.5, C-1~C-6, U1, A2.6, S1, A1.1, A1.2 , A1.3(AOI 재설계·병목 탐색) → **② 서버 기반** (재개 시 기준선 재측정) P0 유령 플레이어 → P1a 송신 지표 → P1b 송신 묶기 → P2 이동 패킷 묶기 → P3 거리별 전송 빈도 → P4 몬스터 깨우기 범위 → **③ 성장·저장** A2.3 경험치/레벨/골드 → A2.4 사망/부활 → A2.5 DB 저장 → **④** A3 아이템 → A4 거래/경매장 → A5 본 벤치마크 → A6 UI → A7 산출물. 큰 항목(C-5, C-6, S1, A3 이후)은 차례가 되면 코드를 읽고 하위 단계로 따로 계획합니다.

**검증 게이트 (모든 콘텐츠 단계 공통):** 체크박스는 해당 기능의 **오류 테스트**(비정상 입력, 중간 접속 해제, 중복·상태 불일치 요청)와 **부하 테스트**(단계 목표 CCU까지 램프, 200 → 1,000 → 5,000)를 통과하고 결과를 `Docs/TESTING.md`에 기록한 뒤에만 `[x]`로 표시합니다.

## ▶ 중단점 (2026-10-31) — "이어서 진행"이라고 하면 여기서 재개 (다음: P1 송신 묶기)
**상태:** A1.3 AOI 재설계·병목 탐색 완료(2026-10-31: 큰 셀 + 쿼드트리 AOI, 시야 R 50m·H 5m, 시야 감사 0, 병목 = 방 작업 안의 수신자별 `WSASend` — `TESTING.md`). A1.2 비동기 로거 완료(2026-10-31, ServerCore `Utils/Logger`, 서버 출력 전부 `LOG_*`, 소스 UTF-8 통일 — `TESTING.md`). A1.1 설정 파일 외부화 완료(2026-10-31, `Server/Server/ServerConfig.ini`, 오류 17종·덮어쓰기·회귀 — `TESTING.md`). S1 서버 권위 이동 완료(2026-10-31, 오류·부하 테스트 + PIE — `TESTING.md`). 과정에서 서버 내비메시를 다시 만들었다(풀 장애물 제거, `MapExportCommandlet` 좌표 형식 수정). C-6 BotEngine 분리 + DummyClient 재작성 완료(2026-10-30). 로딩 화면 이미지는 사용자가 2026-11-06 무렵 구해 오면 `WBP_SystemMessage` 대기 배경에 넣는다. 지난 진행 기록은 커밋 기록과 `TESTING.md`에 있습니다.
**다음 작업 (트랙 B MC4.2까지 마친 뒤):** ① 기준선 재측정(트랙 B MC3이 서버 코드를 바꾸므로 — 실행 방법·인자는 `PERF_BASELINE_2026-11-06.md` §1) → ② P0 유령 플레이어 수정 → ③ P1a 송신 지표 → ④ P1b 송신 묶기 → 같은 매트릭스로 전/후 측정. 2026-11-06 기준선·워커 수 비교는 [PERF_BASELINE_2026-11-06.md](PERF_BASELINE_2026-11-06.md), 동시성 감사(F1~F10, P1 설계 주의점)는 [AUDIT_2026-11-06_CONCURRENCY.md](AUDIT_2026-11-06_CONCURRENCY.md) — 코드가 바뀌었을 수 있으니 함수명으로 찾아 다시 확인. A1.3 결과와 병목 근거는 `TESTING.md` 2026-10-31. C-7은 2026-11-05 결정(Epic 공식 MCP, uemcp 폐기 — U5·U6).
**Claude가 못 보는 것:** 에디터의 애니메이션·이펙트 모양. 스크린샷이나 블루프린트 노드 복사 텍스트(Ctrl+C)를 붙여넣어 확인합니다.
**주의:**
- UE 빌드는 에디터가 켜져 있으면 Live Coding 때문에 막히니, C++를 고칠 일이 생기면 에디터를 닫아야 합니다.
- 에디터 작업은 Epic 공식 MCP(`.mcp.json`, 에디터를 켠 채로, 사용법은 `UEClient/JM/CLAUDE.md`)로 처리합니다. 실제 PIE도 `StartPIE`/`StopPIE`로 시작·종료할 수 있고, 로그인 입력은 사용자가 합니다(2026-11-05 uemcp 폐기).
- 서버는 `Server/Server` 폴더에서 `..\x64\Debug\GameServer.exe`로 실행합니다(DataTable·맵 상대 경로, SQL Server Express 필요). PIE 전에 Debug 빌드가 최신인지 확인하고, 서버가 실행 중이면 VS 빌드가 exe를 덮어쓰지 못합니다.
- 궁수 계정은 `LoadTest --hero archer`의 `arc_*` 계정이나 UE 로그인 화면에서 궁수 타입으로 가입하면 됩니다. (`content/dev` MC3.1부터 전사·궁수가 없어져 봇은 `--body male|female`, 가입은 남성/여성 — 병합 후에는 이쪽을 따름)

## ▶ 중단점 트랙 B (2026-11-09) — `content/dev`, MC4.2c 완료 → 다음 MC4.2d 활
**상태:** 판금 7개 모두 생성(상의·하의·장화·장갑·한손검·활·투구, 남녀, `Content/Equipment/Plate/*`, `jm_kit/items.json` `Plate_*`). VARCO 누적 3,250포인트. 투구는 **사슬을 뺀 눈높이 이미지 → 3만 폴리 생성 → UE 엔진 메시 축소로 LOD 4,000/2,000/1,000**(`garment.reduce_static_lods`, `attach.target_tris`), 정적 메시 + `head` 소켓 `fit: "helm"`, `yaw` 90. 상세는 `TESTING.md` 2026-11-06 "투구 4·5차".
**다음 작업:**
1. ~~투구 사용자 확인~~ 완료(2026-11-06, 시험 액터 `HM_*`·`HF_*` 삭제).
2. ~~판금 상의 등 윗부분 피부 비침~~ 완료(2026-11-06, `clear_cm` 1.5 — `TESTING.md`).
3. ~~MC2.4 아이콘~~ 완료(2026-11-07, 14개, `jm_kit.icon`, 사용자 확인 — `TESTING.md`).
4. ~~MC3.1~~ 완료(2026-11-07, ItemTable 14행·SkillTable 무기 타입·StatTable MALE/FEMALE·`MonsterTable.SkillId`, 바로 전환 — `DECISIONS.md`·`TESTING.md`).
5. ~~MC3.1b~~ 완료(2026-11-07, PlayerStatTable Level 키·MonsterTable 스탯·몬스터 ID 1001 + `template_id`, PlayerType 체형 전용, 피해 = BaseDamage + 공격력 — `DECISIONS.md`·`TESTING.md`).
6. ~~MC3.2~~ 완료(2026-11-07, 장착 패킷·서버 검증·스왑 잠금·콘솔 `Equip` — `DECISIONS.md`·`TESTING.md`).
7. ~~MC3.3~~ 완료(2026-11-07, `ItemTable.Starter` 기본 장비, 로그인 장비 복원, 변경 즉시 저장(측정 후 결정), DB 마이그레이션 — `DECISIONS.md`·`TESTING.md`).
8. ~~MC4.1~~ 완료(2026-11-07, `UEquipmentComponent` + 교체 가능한 표시 `ULeaderPoseAppearance`, `BodyTable`·ItemTable 외형 열은 `jm_kit.item_table` 생성, `ABP_PlayerBase`, Greystone/Sparrow 임시 외형 삭제 — `DECISIONS.md`·`TESTING.md`).
9. ~~MC4.1b~~ 완료(2026-11-07, A 속도 방향 30° 변화 시 즉시 송신 + B 따라잡기 상한 1.3배, `NetMoveStats`·봇 `--profile turn` — 남은 다듬기는 P2·P3에서, `DECISIONS.md`·`TESTING.md`).
10. ~~MC4.2a~~ 완료(2026-11-08, `ALI_Weapon`(`LocomotionLayer`·`UpperBodyLayer`)·`ABP_Layer_Unarmed`·`AM_Unarmed_Attack`·`AM_Common_Death`, `ABP_PlayerBase` 재배선(C++ `UAnimAssetBuilder`)·루트 모션 무시, `WeaponTypeTable`·BodyTable 몽타주 열, PIE 사용자 확인 — `DECISIONS.md`·`TESTING.md`). 남은 확인: 에디터를 닫을 때 `ExportSkillTiming.bat`을 돌려 1001이 "변경 없음"인지(이번에는 같은 규칙으로 CSV를 직접 씀).
11. ~~MC4.2b~~ 완료(2026-11-09, Mixamo 시범 리타깃 `AS_Bow_StandingDrawArrow`, `RTG_Mixamo_Mannequin`·`RetargetTools`, V1·V2 사용자 확인, 활·옷 어색함은 4.2d·4.2e로, V3는 4.2c·4.2d 다운로드 때 — `DECISIONS.md`·`TESTING.md`·`ExternalAssets.md`).
12. ~~MC4.2c~~ 완료(2026-11-09, 한손검 이동·공격·스킬 + 공용 피격: `Anims/OneHandSword/*`(`AS_`·`AM_OneHandSword_*`·`BS_OneHandSword_Locomotion`·`ABP_Layer_OneHandSword`)·`Anims/Hit/AM_Common_Hit`, 리타기터 Root Motion 끔 + Pin IK Bones, 제자리 처리(Mixamo In Place 방식), 스킬 2002 = 도약 내려찍기(사용자 교체), 테스트 플레이어 메시 오프셋 버그 수정, TestLevel 사용자 확인 — `DECISIONS.md`·`TESTING.md`·`ExternalAssets.md`). 4.2b와 함께 커밋.
    - 이후: 4.2d 활 → 4.2e 장갑·어깨 뭉침 → **4.2f 방패**. 정리 후보 3개는 2026-11-10 삭제 완료(`CS_DMOVE_PACKET`, `DT_CharacterAsset`·`CharacterAssetTable`·`FCharacterAssetData`, `MovementTable.xlsx` — `DECISIONS.md`).
13. ~~MC4.2d-1~~ 완료(2026-11-09, 스킬 조준 동기화 — `DECISIONS.md`·`TESTING.md`). MC4.2d-2 활 애니메이션·등에 메기 구현 완료(2026-11-10, 확인은 중간 테스트). MC4.2e 장갑·어깨·Paragon 참조 구현 완료(2026-11-10, 확인은 중간 테스트). MC4.2f 방패 구현 완료(2026-11-10, 서버·데이터·VARCO 600포인트·클라, 확인은 중간 테스트 — `DECISIONS.md`·`TESTING.md`). **다음: MC4.2 중간 테스트**(무기·동작 사용자 확인을 한꺼번에 — 2026-11-10 사용자 결정, 볼 것은 4.2d-2·4.2e·4.2f 체크박스에 적어 둠). 이전 메모: — 4.2d-2 시작 시 Longbow 팩 동작 목록 대조(팩 다운로드는 In Place 옵션이 없어 `retarget_strip_drift`로 제자리 처리). 활 시범 `AS_Bow_StandingDrawArrow`는 4.2c에서 새 리타기터 설정으로 다시 리타깃해 둠.
**VARCO 흐름 노드(JH_MMO 플로):** 투구 정면 컨셉 `531e1ef7…`, 3/4 시점 편집 `c1511cae…`, 3차 3D `8456f6d4…`(실패), 초록 배경 `aa21a66f…`, 4차 3D `4e325e94…`(실패), 사슬 뺀 눈높이 `a0bb8331…`, **5차 3D `ad0e9513…`(채택, 출력 0)**. 판금 상의 컨셉 `cb90b725…`가 세트 스타일 참고.

## 트랙 A — 서버/포트폴리오

### A0. 서버 치명 오류 수정 + 최소 검증 하니스 (빠르게, 범위 최소)
- [x] A0.1 `Session::Disconnect` null 체크 (`Session.cpp:60`). 완료: 접속 후 즉시 끊어도 서버 생존 (2026-10-20, `TESTING.md` 기록)
- [x] A0.2 `header.size` 하한/상한 검증, 핸들러 배열 경계(서버·UE 클라이언트). 완료: size=0, id=65535 패킷에도 행·OOB 없음 (2026-10-20, 서버 런타임 검증, UE 클라이언트는 컴파일만 확인 — `TESTING.md`)
- [x] A0.3 `JobQueue` 깨우기 누락, 송신 큐 잔류, Timer busy-spin 수정. 완료: 부하 중 로그인 결과 지연·CPU 100% 없음 (2026-10-20, 유휴 CPU 확인 + LoadTest 1,000봇 45초 검증, `TESTING.md`)
- [x] A0.4 `dt` 클램프 복구(`Room.cpp:55`), `StatComponent` 초기화(null 안전), `GameMap` 경계 검사(`ToCellPos` 클램프), 가입 시 스탯 없는 PlayerType 거부 (2026-10-20, 빌드·오류 테스트 통과. `dt` 클램프와 맵 밖 좌표는 직접 재현 수단이 없어 코드 리뷰로만 확인 — A0.6 하니스에서 재확인)
- [x] A0.5 Room 입장 경쟁 조건(정원·방 생성 락) + 맵 리소스 로딩 공유. 완료: 동시 접속 100 → 방 수 정확 (2026-10-20: 250명 → 3개, 1,000명 → 10개 방, `TESTING.md`. 네비메시는 공유, 로직 JSON은 방마다 읽음 — 작아서 유지)
- [x] A0.6 최소 검증 하니스: 헤드리스 `Server/LoadTest`(`--bots --host --ramp-rate` 등; DummyClient는 CP949·OpenGL 결합으로 새 프로젝트로 대체), 서버 지표(`Metrics`: 세션·방·플레이어·패킷/초·틱 지연/처리시간, `metrics.csv`), `CS_PING`/`SC_PONG` RTT 측정, 악성 패킷 주입 스크립트. 완료: 200봇 램프 + 악성 패킷 시험 결과가 `Docs/TESTING.md`에 기록됨 (2026-10-20, 1,000봇까지 확인)
- [x] A0.7 QA 버그 수정 (2026-10-28 QA 결과, `TESTING.md`): C1 로그인 전 `CS_ENTER_ROOM` 크래시, C2 공격자 이탈 후 지연 피해 크래시, H1 같은 연결 재로그인 시 유령 플레이어, H2 로그인 실패·가입 결과 미전송(`SC_LOGIN` 결과 코드 + `SC_SIGNUP` 전송, 로그인 화면 결과 표시) + 가입 시 영웅 선택(`player_type` 미전송으로 가입이 항상 실패). 완료: 재현 스크립트 재실행 통과 + LoadTest 회귀 + PIE 로그인 실패·성공·가입 (2026-10-28 완료)

### A2′. 몬스터·전투 (서버 + UE 클라이언트, 최우선)
- [x] A2.1 서버: 몬스터 사망 제거 + 리스폰(`MaxCount`, `SpawnRadius`, 지연). 클라이언트: `SC_DEAD`/`SC_REMOVE_OBJECT` 반영, 사망 몽타주 후 제거 (2026-10-20 완료: 서버 시험 + PIE 확인 — 피격/HP바/쓰러진 자세 유지/3초 후 제거/10초 후 리스폰). 과정에서 `MonsterTable`(`HitStunMs` 포함), 플레이어 공격 서버 쿨타임, `atk(req/ok)` 지표 추가, `DT_CharacterAsset` 몽타주 오등록 수정, 사망 몽타주 `Enable Auto Blend Out` 해제. 남은 개선: 사망을 AnimBP 상태(`bIsDead`)로 전환(A2.4), 공격 중 이동 시 하체 모션(AnimBP 상체 레이어)
- [x] A2.2 전투 입력·스킬 (2026-10-20 결정: DECISIONS.md, 설계: DESIGN_COMBAT_SKILL.md). 서브 단계, 각각 검증 게이트 통과 후 완료 — a~g 완료(2026-10-28)
  - [x] A2.2a 프로토콜 + 서버 (2026-10-20 완료, `TESTING.md`): `CS_ATTACK`(skill_slot, target_id, aim_yaw, aim_x/y)·`SC_ATTACK`(aim_yaw, aim_x/y) 확장, `SkillTable` 확장(Slot·TargetingType·CastRange·DurationMs·TickMs) + 1번 스킬(402 전사 광역 강타 / 502 궁수 지면 장판), 스킬별 쿨타임, 서버 검증(사거리·대상 생존/적대·슬롯·NaN), 지면 장판(진입 즉시 + 1초 간격 피해, 3초), LoadTest `--profile hunt --hero --chaos`. **이월: 스탯을 반영한 대미지는 A2.3(레벨/성장)에서 처리**
  - [x] A2.2b 클라이언트 카메라/입력 (2026-10-21 완료: 검은사막식 3인칭 마우스 회전 카메라 + 전투 중 커서 숨김 + `Left Alt` 자유 커서, `DT_MovementTable` 데이터 기반 카메라/이동 수치, PIE 확인). 쿼터뷰 고정안은 폐기(`DECISIONS.md` 2026-10-21)
  - [x] A2.2c (2026-10-21 완료: 에디터 작업·PIE 확인. 궁수 장판 거리 고정은 `IMC_Default`의 Mouse Y에 Swizzle/Negate가 없어 카메라 피치가 0으로 고정된 것이 원인이었고, 에디터에서 모디파이어를 추가해 해결. 코드 쪽은 `ClientRestart`가 시작 피치를 덮어쓰는 문제를 다음 틱 재적용으로 수정, 장판 지점 폴백을 발밑 평면 교점으로 변경) 클라이언트 조준(크로스헤어 기준, 검은사막식): 전사 일반공격·스킬 = 카메라 방향 논타겟, 궁수 일반공격 = 크로스헤어 아래 몬스터 지정(없으면 무동작), 1번 키 = 조준 모드(범위 인디케이터) → 좌클릭 확정 시 1회 전송, 우클릭 = 취소. **C++ 작성 완료(컴파일 대기)**: `FSkillTableRow`/`DT_SkillTable`, `UDataManager::FindSkillBySlot`, `AJMMyPlayer` 조준·시전·인디케이터(디버그 도형), 카메라 피벗 높이/어깨 오프셋(`MovementTable`), `SC_ATTACK` 방향 반영, 크로스헤어 위젯 훅. **남은 것(에디터): `IA_Attack/IA_Skill1/IA_CancelAim` + `IMC_Default` 매핑, `DT_SkillTable` 임포트, `DT_MovementTable` Reimport, `WBP_Crosshair`, `BP_MyPlayer`·`BP_PControler` 지정, `BP_PControler` 좌클릭 노드 삭제**
  - [x] A2.2d 전투 연출 (2026-10-21 완료, PIE 확인): 공격 중 이동 시 상체 레이어 블렌드(AnimBP `Layered blend per bone` + `UpperBody` 슬롯, Greystone·Sparrow), 시전 중 방향 고정(`LockFacing`, `HitDelayMs`+0.2초), 장판 최소 표시(`SC_ATTACK`의 `skill_id`/`aim_x,y`로 모든 클라이언트에 디버그 원 `DurationMs` 동안 — 이펙트 에셋 교체는 이후). 남은 개선: 서 있을 때 하체 고정이 어색하면 이동 속도로 Blend Weight 제어
- [x] A2.2e 전사 스킬 모션·이펙트 (2026-10-27 추가, 2026-10-28 완료): 1a SkillTable 클라 전용 `Montage` 열로 스킬별 몽타주 재생(402 `AM_Greystone_Skill1`, 502 `AM_Sparrow_Skill1`), 1b 이펙트 열 일반화(`Fx`/`FxBaseRadius`) + 402 지면 강타 이펙트·전신 점프 몽타주, 타격 시점은 몽타주 `Skill Hit` 노티파이 → `ExportSkillTiming.bat`으로 `HitDelayMs` 갱신
- [x] A2.2f 전투 HUD (2026-10-27 추가): 내 HP 바·수치, 스킬 슬롯(좌클릭·1번) 쿨타임. `SC_ATTACK.cooldown_ms` 추가 — 진행 중(2026-10-28): f-1 서버·프로토콜 완료(`TESTING.md`), f-2 클라이언트 C++(`UCombatHudWidget`/`USkillSlotWidget`, SkillTable `Icon` 열) 빌드 완료, f-3 에셋(`WBP_CombatHud`/`WBP_SkillSlot`, `BP_GameMode` 부모 변경) 완료. 기본 공격 슬롯 제거(2026-10-28 결정), 스킬1 아이콘 적용 완료. PIE 확인 완료(2026-10-28)
- [x] A2.2g 행동 잠금·공격 속도 구조 (2026-10-28 추가, `DECISIONS.md`): SkillTable `AnimMs`(도구 추출), 기본 공격 `CooldownMs` 0, 서버 공용 행동 잠금, AttackSpeed = 기본 공격 재생 배율(서버 행동 시간·타격 시점, 클라 몽타주 재생 속도), 몬스터 AI 간격도 같은 규칙. A2.2f보다 먼저 — 코드·데이터 완료(2026-10-28, 서버 행동 잠금 시나리오·회귀 통과 `TESTING.md`, 클라이언트 빌드). PIE 확인 완료
- [ ] A2.3 서버+클라이언트: 경험치/레벨/골드 보상 패킷과 HUD(내 HP·EXP·레벨)
- [ ] A2.4 서버+클라이언트: 플레이어 사망/부활
- [ ] A2.5 서버: DB 저장 연결(`InitFromDb`, 접속 해제·주기 저장, `SavePlayerData` SQL 수정). 완료: 재접속 시 레벨·HP·골드 유지 (2026-11-07 메모: 지금 `InitFromDb`를 아무도 부르지 않아 DB 레벨·HP를 무시함 — 연결할 때 DB `Level`로 `PlayerStatTable`을 조회하고 `PLAYER_START_LEVEL` 고정을 없앰)
- [x] A2.6 미사용 패킷 정리(`CS_LOGOUT`/`CS_CMOVE`/`CS_LEAVE_ROOM`/`SC_CHAT`): 구현 또는 제거 — LOGOUT/CMOVE/LEAVE_ROOM은 C-1에서 삭제, 채팅은 **C-4에서 구현**(2026-10-30 완료)

### R. ③ 저장소 정리 (2026-10-28 계획, `DECISIONS.md`) — 지우는 일 먼저, 옮기는 일은 나중에
- [x] C-1 안 쓰는 코드·패킷 삭제 (2026-10-28 완료: 서버·클라이언트 빌드, 회귀 스크립트(A0.7·가입/로그인 결과·행동 잠금)·LoadTest 이동 1,000/hunt/chaos 통과, PIE는 C-3 뒤에 함께): `CS/SC_LOGOUT`, `CS_CMOVE`/`SC_CMOVE_OBJECT`, `CS_LEAVE_ROOM`, `ObjectInfo.appearance_items`(번호는 `reserved`), 서버 옛 주석 코드·깨진 `SavePlayerData`(없는 `Player_Data` 테이블)·미사용 함수, 클라이언트 인벤토리 키·`SendBuffer::CopyData`·주석 코드
- [x] C-2 DB 정리 (2026-10-29 완료: 백업 `GameServer_2026-10-28_preC2.bak` 후 마이그레이션 실행, `Mp`·`Status` 열도 삭제, `LastLoginAt`은 유지, 회귀·LoadTest 통과): 미사용 테이블 `Item_Meta`/`Player_Inventory`/`Trade_Log` 삭제(백업 + 마이그레이션 스크립트 + `schema.sql` 재생성), 미사용 열은 목록 확인 후
- [x] C-3 미사용 에셋 삭제 (2026-10-30 완료: uemcp 참조 검사 0건·리디렉터 없음, PIE(C-1 몫 포함) 로그인·이동·전사/궁수 공격·스킬·HUD·몬스터 공격/사망/리스폰 확인): `WBP_Info`(+`InfoWidgetClass`·`WidgetNames::MainInfo`), `WBP_Inventory`, `AM_Greystone_Skill1`, `IA_Jump`, `IMC_MouseLook` 삭제. Rampage 일반공격 몽타주를 SkillTable 301 `Montage`(`AM_Rampage_Attack`)로 지정
- [x] U1 시스템 메시지 창 + 방 입장 대기 (2026-10-30 추가·완료, `DECISIONS.md`. PIE 확인): 공용 모달 `WBP_SystemMessage`(`USystemMessageWidget`, `UJMUIManager::ShowSystemMessage` — Notice는 확인 버튼·반투명 배경, Waiting은 버튼 없음·불투명 검정 배경, FIFO 큐). 로그인 실패·가입 결과(`TxtResult` 대체)·서버 연결 실패(로그인/가입 버튼에서 재연결). 로그인 성공 → "방에 입장하는 중..." 동안 `UDataManager::PreloadGameAssets`(영웅·스킬 에셋)로 투명 캐릭터와 스킬 첫 사용 끊김 해결 → 내 캐릭터 스폰 시 닫힘. 이후: 로딩 이미지, 연결 끊김, 거래 결과에 사용
- [x] C-4 채팅 복구(A2.6 완료 처리) (2026-10-30 완료, `DECISIONS.md`·`TESTING.md`): `CS_CHAT` → 서버 검증(`PacketHandler::SanitizeChat`: 제어 문자·앞뒤 공백 제거, 빈 채팅 무시, 100자 자르기) + 도배 방지(0.5초, Room 잡) → 같은 방 전체에 `SC_CHAT`(`sender_name` 추가, 로그인 때 `Player`에 이름 보관) → 클라이언트 `SendChat`·`Handle_SC_CHAT`·`ChatWidget`("이름: 내용", 기록 최대 100줄). 채팅 키는 J 유지
- [x] C-5 소스 폴더 구조화 (2026-10-30 완료, 사용자 결정으로 범위 확대: **ServerCore 정적 라이브러리 분리**, `DECISIONS.md`): C-5a 의존 끊기(`Session` 가상 훅 + `GameSession`, `NetStats`, `SessionFactory`, `CoreUtils`/`MathUtils`) → C-5b `Server/ServerCore`(Network/Job/DB/Utils, `CorePch.h`) + GameServer 도메인 폴더(Session/Packet/DB/Data/Game(Object·Combat·Room·World·Lobby)/Utils), include는 루트 기준 경로 → C-5c 클라이언트 `JMGameInstance` → `Game/`. 동작 변경 없음, 단계마다 회귀
- [x] C-6 DummyClient = LoadTest 시각화 (2026-10-30 완료, `DECISIONS.md`·`TESTING.md`): C-6a LoadTest를 `Server/BotEngine`(정적 라이브러리, WSAPoll 유지 — ServerCore 재사용 안 함) + 콘솔 main으로 분리(동작 변경 없음) → C-6b DummyClient 재작성(옛 CP949 소스·`Protocol/` 복사본 삭제, 같은 엔진 + Win32/OpenGL 탑다운 화면: 봇·몬스터(사망 회색)·스폰·통계, 휠 확대/드래그 이동/F 맞춤, LoadTest와 같은 인자, UTF-8) → C-6c 문서
- [x] C-7 에디터 MCP 도구를 바꿀지 결정 (2026-11-05 결정: Epic 공식 MCP, uemcp 폐기 — U5·U6, `DECISIONS.md`) (2026-11-03 추가, **사용자가 검토 중**. 결정하면 `DECISIONS.md`에 기록): 지금 쓰는 uemcp 0.1.0은 GitHub 스타 0·기여자 1명·릴리스 1회로 검증이 거의 안 됐다(도입 때는 소스만 검토). 2026-11-03 조사 기준 활발히 관리되는 대안은 **ChiR24/Unreal_mcp(EngineWire)** 하나뿐이다: ★1,003·기여자 27·릴리스 30(v0.6.0)·MIT·UE 5.0~5.8, C++ 에디터 플러그인(Fab에서 엔진에 설치 가능)·127.0.0.1·토큰 인증, PIE 합성 입력·BP 그래프·UMG·애니메이션 지원. 위험은 에디터 안 C++ 실행, 빠른 변경(0.x, 호환 깨짐), 직접 검토할 수 없는 크기. 나머지 후보는 관리 중단(chongdashu ★2,111는 2025-04 이후 커밋 없음, kvick-games, runreal), 클라우드 API(flopperam 고급판), 1인 개발(GenOrca)이라 제외. 선택지: (a) uemcp 유지 후 UE 5.8에서 Epic 공식 MCP로 전환, (b) 별도 브랜치에서 EngineWire 0.6.0을 시험 — 네트워크 PIE 자동 실행, BP 그래프 수정, 위젯 배치 3가지로 판단 (2026-11-05 메모: UE 5.8 업그레이드는 Mutable 검토와 함께 나중에 — `DECISIONS.md`)

### S. 서버 권위 이동 (2026-10-30 `TODO.md` 백로그에서 승격, 정리 다음·A1 앞)
- [x] S1 서버 권위 이동 (2026-10-31 완료, `DECISIONS.md`·`TESTING.md`·[DESIGN_CONTROL_CAMERA.md](DESIGN_CONTROL_CAMERA.md)): S1a 서버 검증(`correction_seq` 순번, 토큰 버킷 속도 = `StatTable.MoveSpeed` × 허용 배율, NaN·전송 빈도·내비메시, 허용치는 `MovementTable`의 `Server*` 열) + 거부 시 본인에게 보정 + metrics `move(...)` + 봇(실제 시작 위치·속도 제한·순번·이동 chaos) → S1b 클라이언트(내 캐릭터 보정 적용, `MaxWalkSpeed` = 서버 스탯, `MaxSpeedCmSec` 삭제, 정지 중 전송 중단·전송 간격 하한, `SyncTransformToInfo` 내 캐릭터만, 원격 플레이어 외삽 300ms 상한) → PIE에서 발견·수정: 내비메시 경계 여유 2D, 맵 내보내기 기준(Pawn Block)·좌표 형식, 서버 오브젝트 항상 생성. 한계: 봇은 길 찾기 없음, z는 클라이언트 값

### A1. 관측성·설정
- [x] A1.1 설정 파일 외부화(포트, DB 연결 문자열, 로그 레벨) (2026-10-31 완료, `DECISIONS.md`·`TESTING.md`): `Server/Server/ServerConfig.ini` 계층형 설정(기본값 → ini → `ServerConfig.local.ini` → 환경변수 `JH_DB_CONNECTION` → `--config`/`--port`), 시작 시 엄격 검증(오류 17종 시작 중단), 적용 값·출처 출력(`PWD` 가림). 바인드 IP·워커 수·DB 연결 수도 설정으로. 시작 실패 시 `exit()`가 멈추던 기존 버그 수정. 로그 레벨은 A1.2 로거가 사용
- [x] A1.2 레벨/타임스탬프 로거로 핫패스 `cout` 대체 (2026-10-31 완료, `DECISIONS.md`·`TESTING.md`): ServerCore `Utils/Logger`(큐 + 전용 출력 스레드, 콘솔 + `Logs/GameServer_<시작시각>.log`, 큐 상한 65,536 초과 시 버리고 개수 집계 → `[metrics] logDrop`), `LOG_DEBUG/INFO/WARN/ERROR`(std::format, 레벨 확인 후 포맷), 서버의 `cout`/`printf` 약 70곳 교체(플레이어·연결마다 남는 로그는 Debug), `Log.Dir` 설정, 종료(exit·Ctrl+C) 시 flush. 소스 UTF-8 통일(`/utf-8`, `RecastNavMesh.cpp` 변환, 콘솔 UTF-8, 아이디·비밀번호 UTF-8 해석)
- A1.3 AOI 재설계와 병목 탐색 (2026-10-31 재계획, 원래 "메트릭 확장: 송신 큐·DB 큐·틱 p95" — 측정 기준을 먼저 정하고 필요한 계측만, `DECISIONS.md`)
  - [x] A1.3a 측정 도구 + 현재 격자 기준선 (2026-10-31 완료, `TESTING.md`: 분산 1,000·핫존 25·로그인 폭주 통과, 분산 2,500+·핫존 50+에서 핑·틱 지연 초과 — 핫존 원인은 방 잡 안의 브로드캐스트 `WSASend` 시스템 콜(CPU 샘플링 근거). 핫존 배치는 서버 `[Debug] DummySpawnZone`): AOI 수치 `Common/DataTable/WorldTable.csv`(서버 전용), 봇 `--zone x,y,r`(핫존)·1인 수신 B/s, 서버 지표(틱 본문·지연 p50/p95/p99/max, 시야 인원 avg/max, 송신 큐 합·세션 최대), 매트릭스(분산 1,000/2,500/5,000, 핫존 25/50/100, 핫존 사냥 100, 로그인 폭주 1,000 — `--move-interval 200`), 합격선 초과 지점 CPU 프로파일
  - [x] A1.3b 시야 반경 결정 + 정확도 검증 (2026-10-31 완료: R 50m·H 5m, 현재 격자 누락 분산 77%·경계 35%, 초과 0 — 감사 도구는 R 5m 시험으로 초과도 판정 확인. `TESTING.md`): 에디터 스크린샷(10/20/30/50/80m)으로 R·H 결정(사용자), 서버 시야 감사(`[Debug] ViewAudit`, 전수 거리 계산과 비교 — 누락: ≤R인데 안 보임, 초과: >R+H인데 보임), 경계 시나리오(R−5m 보임, R+H+5m 안 보임, 경계 왕복), 현재 격자에서 누락·초과 기록
  - [x] A1.3c 큰 셀 + 셀마다 쿼드트리 AOI(`AoiIndex`, 원형 반경 R + 히스테리시스 H): 시야 감사 0 + 매트릭스 전/후 비교 + PIE (2026-10-31 완료: 감사 5개 시나리오 0, 분산 1,000 시야 4→30명·송신 6배로 합격선 초과, AOI 비용 약 1%·송신 경로 약 75%, 몬스터 NPCMove 시점 버그 수정, 배회는 `MonsterTable.WanderRadiusCm`. `TESTING.md`)
  - [x] A1.3d 분석·보고: 합격선 초과 지점·원인·다음 수정 후보 → 고른 항목을 새 체크박스로 (2026-10-31 완료: 주 병목 = 방 작업 안의 수신자별 `WSASend`, 부 병목 = 깨어 있는 몬스터 증가 → 아래 P1~P4. 봇 무작위 이동을 목표 지점 걷기로 변경. `TESTING.md`)

### P. 성능 개선 (2026-10-31 A1.3 결과, `DECISIONS.md`) — 순서대로 하나씩, 매번 A1.3 매트릭스로 재측정해 `TESTING.md`에 전/후 기록 (같은 PC 측정, 분산 2,500명 이상은 한계로 기록)
- [ ] P0 유령 플레이어 수정(2026-11-06 감사 F1·F2, `AUDIT_2026-11-06_CONCURRENCY.md`): `Session::ProcessData`가 `OnRecvPacket` 뒤 연결이 끊겼으면 중단, `Room::PlayerEnterRoom` 첫머리에서 세션 연결 확인(아니면 자리 반환), `PlayerEnterRoom`의 중복 `SC_ADD_OBJECT` 제거. 완료: Python 원시 소켓 `[깨진 CS_MOVE][CS_ENTER_ROOM]` 한 번 전송 → 수정 전 `sessions=0 players=1` 재현, 수정 후 0 + chaos 램프 중 강제 종료 반복 후 `players` 잔류 0 + 짧은 부하 회귀
- [ ] P1a 송신 지표 먼저: `NetStats`에 `WSASend` 호출 수/s와 호출당 메시지 수(WSABUF 평균) 추가, `metrics.csv` 재시작 헤더 중복 수정(+ 실행 ID 열), 측정 CSV·서버 로그 보관 규칙. 수정 전 값 기록(핫존 100 WSABUF 평균 ≈ 1 예상)
- [ ] P1b 송신 묶기: 방 작업 안에서는 `Session::Send(pushOnly)`로 큐에만 넣고 넣은 세션을 모아 두었다가 세션당 `WSASend` 1회. **작업 묶음 끝만이 아니라 잡 N개(16~64) 또는 1~2ms마다 보내기**(`ExecuteJobs` 독점 — 감사 F5 — 도 함께 해결), 보내기는 `_isProcessing` 해제 전, 모은 세션 목록은 RAII로 비움(감사 §7). 프로토콜 변경 없음. 목표(`PERF_BASELINE_2026-11-06.md` §5): 핫존 100 핑 p99 ≤ 50·틱 지연 p99 ≤ 20·서버 CPU 코어1 환산 ≤ 25%(워커 1 자연 실험 수준), 분산 1,000 핑 p99 ≤ 40, 핫존 100 WSABUF 평균 ≥ 10. `WorkerThreads`는 기본 유지(늘리면 악화). 구현 후 동시성 리뷰는 Fable 5.1로
- [ ] P2 이동 패킷 묶기: 틱마다 수신자별로 그 틱에 움직인 대상들의 위치를 패킷 하나에(새 패킷, `.proto` → `GanPackets.bat` → 서버·UE 핸들러·봇). 클라이언트 보간 확인(PIE). **완료 기준에 MC4.1b 회귀 측정 추가**: 봇 `--profile turn --turn-send --turn-interval 550 --move-interval 200` 3명 60초 + 관찰자 `NetMoveStats`가 MC4.1b 결과(최대 1.23배, 1.2배 초과 0.02%, 오차 16/83cm)보다 나빠지지 않을 것 — 한 틱 안의 즉시 송신이 합쳐지는 영향과 A2(속도 크기 변화 시 송신)·`RemoteCatchUpSpeedMul` 재조정을 여기서 판단(`DECISIONS.md` 2026-11-07)
- [ ] P3 거리별 전송 빈도: 가까운 수신자에게는 매 이동, 먼 수신자(예: 25m 밖)에게는 일부만(멈춤·방향 전환은 항상). 거리·비율은 `WorldTable`. **완료 기준: 먼 수신자 기준으로 MC4.1b turn 시나리오 회귀 측정**(간격이 길어진 대상의 외삽·따라잡기 확인)
- [ ] P4 몬스터 깨우기 범위 분리: `MonsterTable` 깨우기 반경(예: 20m), 시야(50m) 안이라도 그 밖이면 보이기만 하고 AI는 잠 — 행동트리 개선 때 재검토
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
- [ ] A5.0 연결 정리(2026-11-06 감사 F4·F3): `Disconnect`에서 `CancelIoEx`/소켓 닫기, 유휴 세션 타임아웃, `_currPlayer` 동기화(`atomic<shared_ptr>` 또는 `Session::_lock`). 종료 시 문제 F6~F10은 C 정리 단계 후보
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
2026-10-30 정리: M0/M2/M4의 남은 항목은 트랙 A에서 이미 끝난 작업(A1.4, A2.2)이라 완료 처리했습니다. 남은 것은 M3(영웅·무기 테이블)과 Paragon 에셋 최종 Move입니다. M3의 테이블은 A3의 ItemTable/DropTable과 `DESIGN_DATA.md` ID 규약을 공유합니다.
2026-11-04: 플레이어를 매니퀸 모듈형으로 바꾸기로 해서(`DECISIONS.md`) M3의 HeroTable·WeaponItemTable은 **Phase MC**(아래)로 대체합니다.

## Phase M0 – 병합 준비
- [x] Test의 서버 자리(Seam) 동작을 서버 권위 동작으로 전환한다는 방침을 `DESIGN_COMBAT_SKILL.md`에 기록
- [x] Test의 작업 절차 규칙·문서 구조·데이터 규약(`Common/DataTable`)을 이 저장소에 이식
- [x] **원본 에셋 팩 재다운로드** (사용자 작업) — 완료 후 `ExternalAssets.md` 표 채움
- [x] 이식된 `MovementTable.csv`를 UE DataTable로 임포트할 구조(Row 구조체, `UDataManager`) 설계 — A2.2b에서 완료(`Data/MovementTableRow.h`, `Game/DataManager`, `DT_MovementTable`)

## Phase M0.5 – 클라이언트 정리 (2026-10-20 검토 결과, 상세 항목은 아래 체크박스 단위로 진행)
- [x] 0. `.gitignore`: `Content/Paragon*/`, `StarterContent/` 추가 완료 (2026-10-20). 추적 중이던 437개(Paragon* 435 + StarterContent 2, 720MB)는 `git rm -r --cached`로 인덱스에서만 제거, 디스크 파일 유지 확인 (2026-10-21, 커밋 대기). 과거 커밋에는 남아 있어 저장소 크기는 줄지 않음
- [x] 1. 네트워크 안정화: 패킷 크기 검증, 수신 스트림 어긋남, `Disconnect` 스레드 종료 대기, `Deinitialize`, 재접속 정리, 핸들러 반환값·경계 처리 — 완료 (2026-10-27, 빌드 + Play 확인: 정상 흐름, 서버 종료 시 `Connection to server lost` 1회, PIE 반복 시작/종료, 재접속). 끊김 후 화면 처리(메시지·로그인 복귀)는 사용자 결정으로 추후 구현
- [x] 2. `JMPlayer.cpp` 몽타주 로드 버그·조기 return 수정, 죽은 코드·미사용 include 삭제 — 완료 (2026-10-21, 빌드 + Play 확인). 피격 몽타주 로드 조건(`HitMontage.IsPending()`), `Super::SetPlayerData` 선행 호출, 중복 로드 핸들 정리, Row 없음 경고 로그. 미사용 include는 없었음
- [x] 3. 명명/배치: `NpcCharaceter` 오타·위치, `CharacterAssetData.h` 위치, CP949 주석 UTF-8 변환 (BP 참조는 에디터에서 안내) — 완료 (2026-10-21, 빌드 + Play 확인). `ANpcCharacter`(`Character/`), `CharacterAssetData.h`(`Data/`), CP949 8개 파일 UTF-8(BOM) 변환, `DefaultEngine.ini`에 `[CoreRedirects]` 클래스 리디렉터 추가, `BP_Npc` 재저장
- [x] 4. Content 정리(에디터 수동): WBP 중복 통일, 깨진 참조(`ABP_Base`, `BP_Player`, `DT_Appearance/Equip`) 제거, 잔재 에셋·`Character/` 루트 정리, Config(기본 맵·레거시 입력) — 완료 (2026-10-21, Play 확인 통과). WBP는 이미 `_Core/UI/` 한 벌. `Maps/TestMap`은 사용자 결정으로 유지
- [x] 5. 레이어 분리: 송신 파사드(5a), 핸들러 보일러플레이트(5b), UIManager 일원화(5c) — 완료 (2026-10-27, 각 단계 빌드 + Play 확인)
- 이월: 서버 권위 이동·하드코딩 이전 → `Docs/TODO.md` 기록. `ParagonSample` 맵·`_Core/Materials·Environment`는 `NetworkManager.cpp:64`가 인게임 레벨로 하드코딩해 사용 중이므로 자체 제작 맵으로 교체한 뒤 제외·삭제

## Phase M1 – 에디터 검증 (JH_MMO에서 진행, 에셋 재다운로드 이후)
- [x] 1. 기본 무기 숨김: 본 스케일 0으로 무기만 사라지는지 (Greystone, Sparrow) — 통과 (현재는 숨길 필요 없음)
- [x] 2. 무기 분리 — 폐기 (활·방패 분리가 막힘, 영웅 고유 무기만 쓰기로 함)
- [x] 3. 교차 리타게팅 — 폐기 (같은 이유)
- [x] **캐릭터 베이스 결정: Paragon 영웅 + 고유 무기** (`DECISIONS.md` 2026-10-17)

## Phase M2 – 데이터 파이프라인
- [x] Row 구조체 + `UDataManager` 이식, `MovementTable` 임포트/조회 — A2.2b에서 완료
- [x] 서버 CSV 로더 (`Common/DataTable/*.csv`를 읽어 `ServerData.h` 하드코딩 대체) — A1.4에서 완료. 서버 이동 수치 연동은 S1에서
- [x] `UJMAssetDataManager`의 Mock DB 교체 여부 결정 — 폐기(클래스가 이미 삭제됨, 2026-10-30 확인)

## Phase M3 – 캐릭터/무기/애니메이션
- [ ] HeroTable (Greystone, Sparrow): 영웅 → 메시/AnimBP/스탯, 영웅 선택 흐름 → Phase MC로 대체(2026-11-04)
- [x] 영웅별 AnimBP 연결 ([DESIGN_ANIMATION.md](DESIGN_ANIMATION.md), AnimLayer/무기 교체 없음) — `ABP_Greystone`/`ABP_Sparrow`(`DT_CharacterAsset`), 상체 레이어·`DefaultSlot`은 A2.2d·A2.2e에서 완료
- [ ] WeaponItemTable + 무기 머티리얼 교체 + 장착 CS_/SC_ 패킷, 서버 스탯 계산 ([DESIGN_CHARACTER_EQUIPMENT.md](DESIGN_CHARACTER_EQUIPMENT.md)) → Phase MC로 대체(2026-11-04)
- [x] 모듈러 코드/에셋 삭제 (클라이언트 코드, CoreC, DT_Appearance/DT_Equip). 남은 일(에디터 컴파일 확인, `BP_Player`/`BP_MyPlayer`/`ABP_Base` 정리, `appearance_items` 제거)은 M0.5-4·C-1에서 완료
- [ ] 사용 중인 Paragon 에셋을 새 폴더로 정리해 커밋 (재다운로드 파일은 커밋하지 않음) — 절차는 아래 "Paragon 에셋 최종 Move"

## Phase M4 – 전투/스킬 (서버 권위)
- [x] SkillTable(영웅 종속 — `PlayerType` 열) + 스킬 시전 CS_/SC_ 패킷 ([DESIGN_COMBAT_SKILL.md](DESIGN_COMBAT_SKILL.md)) — A2.2a~g에서 완료
- [x] AnimNotify는 시전·연출 타이밍만, 판정은 서버 — A2.2e(`AnimNotify_SkillHit` + `ExportSkillTiming.bat`)에서 완료

## Phase MC – 모듈러 캐릭터 전환 (`content/dev`, 2026-11-04)
플레이어 = 매니퀸 공용 스켈레톤 + 슬롯별 장비 교체 + 몸 부위 숨김, 스킬은 무기 타입 종속(`DECISIONS.md` 2026-11-04). 각 단계 시작 시 `Jeonghoon`을 merge하고, 콘텐츠 단계는 위 검증 게이트(오류·부하 테스트)를 따릅니다. 아트 기준은 [DESIGN_ART_CONCEPT.md](DESIGN_ART_CONCEPT.md), 프로토타입 근거는 [HANDOFF_MODULAR_EQUIPMENT_VARCO.md](HANDOFF_MODULAR_EQUIPMENT_VARCO.md).

**MC0 문서**
- [x] MC0.1 결정 기록(`DECISIONS.md`)과 프로토타입 인수인계서 (2026-11-04)
- [x] MC0.2 아트·장비 컨셉 `DESIGN_ART_CONCEPT.md` (2026-11-04, 고사실 판타지·검은사막 계열 참고)
- [x] MC0.3 `DESIGN_CHARACTER_EQUIPMENT`·`DESIGN_ANIMATION`·`DESIGN_COMBAT_SKILL`·`DESIGN_DATA`를 새 방향으로 개정 (2026-11-04)

**MC1 베이스**
- [x] MC1.1 매니퀸(Manny/Quinn, 템플릿 모션)을 `Content/Character/Mannequin/`으로 이식 (2026-11-04, 60개 — Pistol·Rifle 제외, `ExternalAssets.md`)
- [x] MC1.2 기준 캐릭터 = 키트 조립 그대로(2026-11-04 재작업, `DECISIONS.md`): 리더 `SKM_CoreC_<M|F>_Base_Body_Combined` + 키트 부위(Face·Eyes·Teeth·Hair·Torso·Hands·Legs·Feet) + 키트 의상(Upper·Lower·Shoes), 체형별 구성은 데이터. 매니퀸 달리기·공격 재생 시 키트 의상이 몸과 벌어지지 않음을 기준으로 확인(Simulate). 머티리얼 = 키트 원본 (2026-11-04, `jm_kit.preview` + `kit_profiles.json`, `TESTING.md`)
- [x] MC1.3 뼈대 검사 도구: 장비 메시의 본 개수·이름·순서·바인드 포즈가 키트 의상과 같은지 자동 검사(다르면 실패) (2026-11-04, `jm_kit.skeleton_check`, `TESTING.md`)

**MC2 장비 에셋**
- [x] MC2.1 의상 맞춤 도구 재작성: 키트 의상(예: `Techwear01_Upper`)을 복제해 형상만 새 메시로 교체 — 뼈대·바인드 포즈·PA를 키트에서 상속, 가중치는 키트 의상에서 전이, MC1.3 검사 통과. (이전 MC2.1·2.1b·2.1c 결과물은 2026-11-04 삭제, 키트 채택은 유지) (2026-11-04, `jm_kit.garment`, `TESTING.md`)
- [x] MC2.2 천 세트 7개(VARCO, 포인트 사전 승인, 첫 아이템에서 Remesh 품질 확인) (2026-11-05, `Content/Equipment/Cloth/*`, `jm_kit/items.json`, VARCO 2,300/2,500포인트, `TESTING.md` — 남은 것: 무기 쥐는 자세는 MC4.2·4.3. 2026-11-05 보완: 상의 가슴 얼룩 `Texture3D`로 해결(100포인트), 장갑을 키트 손 형상 + 무늬 칠하기로 재작업(손가락 맞춤은 MC4.2와 함께) — `DECISIONS.md`)
- [x] MC2.3 판금 세트 7개 (2026-11-06 완료, 사용자 확인: 투구는 사슬 뺀 눈높이 이미지 → 3만 폴리 → UE 축소 LOD 4천/2천/1천, VARCO 누적 3,250포인트, `TESTING.md`. 판금 상의 등 피부 비침은 `clear_cm`으로 보완)
- [x] MC2.4 아이콘 SceneCapture 촬영 → `T_Icon_<Item>` (2026-11-07, 천·판금 14개 512² 투명 PNG `SourceArt/Icons/` + 아이템 폴더 `T_Icon_<Item>`, `jm_kit.icon`·MCP `icon_shoot`, 사용자 확인 — `TESTING.md`)

**UE 5.8 업그레이드 (`upgrade/ue-5.8`, 워크트리 `JH_MMO_ue58`, 확정 업그레이드, MC2.3 전에 진행 — `DECISIONS.md` 2026-11-05)** — 트랙 A의 U1(시스템 메시지 창)과 다른 번호 체계
- [x] U1~U4 엔진 전환: C++ 빌드, 설정 정리, `jm_kit`·uemcp 회귀, 네트워크 PIE 회귀 (2026-11-05, `TESTING.md`)
- [x] U5 Epic 공식 MCP 전환·시험(= C-7): 플러그인·자동 시작 설정, `jm_mcp` 툴셋(`KitTools`·`DataTableReimportTools`, 임의 코드 실행 없음), `.mcp.json` HTTP. 통과 기준 = 연결·`jm_kit` 실행·스크린샷·실제 PIE 시작/종료(+ DT 재임포트·BP 그래프·위젯 배치 결과 기록), `TESTING.md` (2026-11-05 통과)
- [x] U6 uemcp 삭제: `.mcp.json` 항목, `bRemoteExecution`, 문서·CLAUDE.md(5.6 → 5.8, MCP 사용법) (2026-11-05)
- [x] U7 병합·새 출발: `main`·`Jeonghoon`·`content/dev` fast-forward + push, 원래 폴더 `JH_MMO`에서 빌드·PIE 확인, 워크트리 `JH_MMO_ue58` 제거 (2026-11-05: 세 브랜치 `a4a748a`에서 출발, 워크트리·`upgrade/ue-5.8` 삭제, 원래 폴더 5.8 빌드·PIE 로그인 확인은 U5에서)
- [ ] U8 **보류(2026-11-05, `DECISIONS.md`) — 재검토: Mutable Beta 해제 또는 그래프 공개 API.** Mutable 시험(남녀): 천 세트 7개로 Customizable Object, 지금 방식과 틈·관통, 드로우콜, 장비 교체 시간, 메모리를 측정 비교 → 채택 시 `DESIGN_CHARACTER_EQUIPMENT.md` 개정·`body_mask` 대체

**MC3 데이터·서버**
- [x] MC3.1 `ItemTable` + SkillTable 무기 ID 재번호(1xxx/2xxx/3xxx/9xxx), StatTable MALE/FEMALE, `MonsterTable.SkillId` (2026-11-07: 바로 전환 — 전사·궁수 삭제, 무기 타입은 MC3.2 전까지 Unarmed. itemId = [슬롯][티어][일련번호], 로더 오류 26종·가입 검증·hunt 부하·PIE 통과, `DECISIONS.md`·`TESTING.md`)
- [x] MC3.1b 스탯 테이블 재설계 + 몬스터 ID 분리 + 피해 공식 (2026-11-07 추가·완료, `DECISIONS.md`·`TESTING.md`): StatTable → `PlayerStatTable`(Level 키, 체형 공용), 몬스터 스탯 → `MonsterTable` 열, 몬스터 ID 3 → 1001(`ObjectInfo.template_id`), `PlayerType` 체형 전용(3·4·5 reserved), 피해 = BaseDamage + 시전자 공격력(시전 순간 확정, 9001 BaseDamage 40), 가입 Level·Hp 데이터 기반, `MapExport.bat`·`ExportSkillTiming.bat` UE 5.8 경로. 로더 오류 35종·스폰 템플릿 없음·피해 값·가입·부하·PIE 통과
- [x] MC3.2 `CS_/SC_CHANGE_EQUIP`(`.proto` → `GanPackets.bat`) + 서버 검증·스탯 계산·무기 스왑 잠금 + 거부 결과 코드·UI 표시 + 개발용 콘솔 명령 `Equip` (2026-11-07: 원시 소켓 18/18·hunt 부하·PIE 사용자 확인, 거부는 본인 전용 `SC_CHANGE_EQUIP_RESULT`, 빈도 200ms, 무기 해제도 잠금·잠금 중 방어구 허용 — `DECISIONS.md`·`TESTING.md`)
- [x] MC3.3 `Player_Equipped` 저장, 가입 시 기본 장비, 기존 `PlayerType`·장비 행 마이그레이션 (2026-11-07: `ItemTable.Starter`, 변경 즉시 저장(플레이어별 동시 1건·합치기, 측정 15회 후 사용자 결정), 마이그레이션 639명 → MALE·665명 기본 장비, 원시 소켓·부하·PIE 사용자 확인 — `DECISIONS.md`·`TESTING.md`)

**MC4 클라이언트**
- [x] MC4.1 장비 컴포넌트(Leader Pose, 몸 숨김 데이터 구동) (표시 방식 교체 가능 인터페이스 — `DECISIONS.md` 2026-11-05) (2026-11-07: `UEquipmentComponent`·`ULeaderPoseAppearance`, `BodyTable` + ItemTable 외형 열(`jm_kit.item_table`), `ABP_PlayerBase`, PIE 남녀·장비 교체·원격 봇 3명·PIE 2인 사용자 확인 — `TESTING.md`)
- [x] MC4.1b 원격 이동 방향 전환 튐(2026-11-07 MC4.1 확인 중 발견, S1 원격 보간): 측정 → 방향 변화·정지 시 즉시 송신 + 따라잡는 속도 상한(`MovementTable`) → 재측정, 봇 이동 속도 전송 보완 (2026-11-07: `MoveSendTurnDeg` 30 + `RemoteCatchUpSpeedMul` 1.3, turn 봇 최대 2.78→1.23배·오차 −82%·송신 +18%, PIE 2인 개선 확인 — 남은 다듬기는 P2·P3, `TESTING.md`)
- [ ] MC4.2 `ABP_PlayerBase` + `ALI_Weapon` + 무기 타입별 레이어·몽타주, `ExportSkillTiming.bat` (+ 장갑 손가락 맞춤 보완 — 무기 쥐기와 함께, `DECISIONS.md` 2026-11-05). 2026-11-08 하위 단계로 분할(모션 출처·스킬 조준 동기화 — `DECISIONS.md` 2026-11-08):
  - [x] MC4.2a 구조 + 맨손 + 사망(템플릿만): `WeaponTypeTable`(Name 1·2·3) → `LinkAnimClassLayers`, BodyTable `HitMontage`·`DeathMontage`(생성기), `ALI_Weapon`·`ABP_Layer_Unarmed`·`AM_Unarmed_Attack`·`AM_Common_Death`, Main States 점프·착지 정리 (2026-11-08: 레이어 이름 `LocomotionLayer`·`UpperBodyLayer`, 상체 슬롯 `spine_01` 레이어 블렌드, 기본 구현 = 맨손 이동, `RootMotionMode = IgnoreRootMotion`, Land→Locomotion은 이미 자동 전이라 변경 없음, SkillTable 1001 몽타주 연결, PIE 사용자 확인 — `TESTING.md`)
  - [x] MC4.2b Mixamo 시범 리타깃(활 Standing Draw Arrow 1개 → 사용자 확인: V1 리타깃 품질, V2 템플릿 이동 위 상체 재생, V3 검·활 팩 동작 목록 대조) + 일괄 리타깃 도구(`jm_kit.retarget`·jm_mcp `RetargetTools`), `ExternalAssets.md` (2026-11-09 출처 우선순위 템플릿 → Mixamo → VARCO, GASP·Lyra 제외 — `DECISIONS.md`) (2026-11-09: V1 "Mixamo와 거의 비슷"·V2 `BP_TestPlayer` 상체 재생 사용자 확인, 손바닥뼈 체인 매핑 해제, V3는 4.2c·4.2d 시작 시로 이동, 활·옷 어색함은 4.2d·4.2e — `TESTING.md`)
  - [x] MC4.2c 한손검 + 공용 피격(시작 시 Sword and Shield 팩 동작 목록 대조 — 없는 동작은 VARCO): `ABP_Layer_Sword`(왼팔 맨손 포즈 덮기 — 방패 착용 시 원본 왼팔로 전환하는 값은 자리만 두고 MC4.2f에서 연결, `DECISIONS.md` 2026-11-09 방패), `AM_Sword_Attack`·`AM_Sword_Skill2`, `AM_Common_Hit`, SkillTable 2001·2002 교체·타이밍 재추출 (2026-11-09: 애셋 이름은 사용자 요청으로 `OneHandSword`(WeaponType은 `Sword` 그대로) — `ABP_Layer_OneHandSword`(이동 = 1D `BS_OneHandSword_Locomotion` + `upperarm_l` 아래 맨손 덮기, 비율 `LeftArmOverrideAlpha` 기본 1·4.2f에서 방패 착용 시 0), `AM_OneHandSword_Attack`(UpperBody, 타격 0.62)·`AM_OneHandSword_Skill1`(DefaultSlot, 도약 내려찍기, 타격 1.25)·`AM_Common_Hit`(UpperBody, 머리 피격), SkillTable 2001 620/1500·2002 1250/2333, TestLevel 사용자 확인(이동·공격·스킬·피격) — `TESTING.md`)
  - [ ] MC4.2d 활 + 스킬 조준 동기화 — 2026-11-09 두 단계로 분할(`DECISIONS.md`):
    - [x] MC4.2d-1 스킬 조준 동기화: `CS_SKILL_AIM{slot, active, seq}`/`SC_SKILL_AIM`, `ObjectInfo.aim_skill_id`, SkillTable `AimMoveSpeedMul`·`AimMaxMs`, MovementTable `ServerAimMinIntervalMs`, 서버 이동 예산 감속·해제(취소·`CS_ATTACK`·사망·무기 교체·시간 초과), 클라 로컬 권위 + seq, `MaxWalkSpeed` 배수, `JMAnimInstance.bIsSkillAiming`, 봇 `--aim`. 완료: 로더 오류·원시 소켓(켜기·거부·예산·해제·늦은 시청자)·hunt `--aim` 부하·PIE 2인 사용자 확인 (2026-11-09 완료: 로더 7종·원시 소켓 24/24·부하 핑 p99 기준과 같음·PIE 2인 사용자 확인 — `TESTING.md`)
    - [ ] MC4.2d-2 활 애니메이션·등에 메기 (2026-11-10 구현 완료, **사용자 확인은 중간 테스트에서 한꺼번에** — 사용자 결정. 그때 볼 것: 손에 쥔 활 위치·각도, 조준 걷기 재생 배율(×1.5~2.3) 또는 `AimMoveSpeedMul` 0.25, 기본 공격 1.73초, 등 위치, **시위 움직임**(권장 A = 활 메시의 시위를 지우고 활 끝 → 오른손 V자 줄을 따로 그림, B = 활 스켈레탈 변환·시위 뼈, C = 정적 유지 — 테스트 때 결정); `ExportSkillTiming.bat` 대조 결과 3001·3002 수기 값과 같음): `ABP_Layer_Bow`(조준 = Longbow 팩 aim walk + 당김 상체), `AM_Bow_Attack`·`AM_Bow_Skill1`, 활 등 메기(MC4.3 장치 먼저 적용). 시작 시 Longbow 팩 동작 목록 대조. **완료 기준에 추가(2026-11-09 MC4.2b): 당김 동작에서 활이 손에 쥐어진 위치·각도(손 앞에 뜨지 않음), 시위는 정적 유지 여부를 사용자 확인 후 결정**. 끝나면 MC4.2d-1 임시 화면 표시 `[SkillAim]`(`AJMCharacterBase::SetSkillAiming`의 `AddOnScreenDebugMessage`) 삭제(2026-11-09 사용자 결정)
  - [ ] MC4.2e 장갑 손가락 맞춤 + **팔을 어깨 높이로 든 자세(활 당김·검 스킬)에서 천·판금 상의 어깨·소매 뭉침 확인·보완**(2026-11-09 MC4.2b에서 발견) + Paragon 애니메이션 참조 0 확인 + 문서 정리(정리 후보는 사용자에게 묻기) (2026-11-10 구현 완료, **사용자 확인은 중간 테스트** — 볼 것: 장갑 손목 가장자리(톱니 제거)·활 쥔 손(+2cm 손가락 쪽)·검 쥔 손, 스킬1·활 당김 때 상의 어깨 접힘(가중치는 그대로 — 키트 상의와 같은 자리, 거슬리면 보정 모프 후속). 측정 도구 `jm_kit.posed`·`refs`, 정리 후보 3개 삭제(`CS_DMOVE_PACKET`·`DT_CharacterAsset`·`MovementTable.xlsx`), Paragon 애니메이션 참조 0 — `DECISIONS.md`·`TESTING.md`)
  - [ ] MC4.2f 방패(선택 장비, 2026-11-09 추가 — `DECISIONS.md`): `OFFHAND` 슬롯(EquipPart 7, `.proto` → `GanPackets.bat`, `equip_items` 7칸, 서버 한손검 전용 검증·무기 변경 시 자동 해제·방송, `UEquipmentComponent`), ItemTable 방패 행(스탯 0), VARCO 천·판금 방패 2개(포인트 사전 승인) + 아이콘, `ABP_Layer_Sword` 방패 착용 시 왼팔 원본 전환, `DESIGN_CHARACTER_EQUIPMENT.md` 슬롯 표 갱신. 완료: 방패 착용·미착용 검 동작, 활로 바꿀 때 방패 해제 PIE 확인 (2026-11-10 구현 완료, **사용자 확인은 중간 테스트** — 볼 것: 대기·공격·스킬1 때 방패 위치·각도(손등 방향 앞면, 손바닥에서 4cm), 방패 착용 시 왼팔 원본·미착용 시 맨손 덮기, 활·맨손으로 바꿀 때 방패 사라짐(서버가 해제 먼저 방송), 판금 방패 색이 어두운지. 원시 소켓 22/22·DB 불일치·로더 7종·장비 봇·hunt 부하, 7101 천·7301 판금, VARCO 600포인트, TestLevel PIE 표시 — `DECISIONS.md`·`TESTING.md`)
- [ ] MC4.3 비전투 시 무기 등에 메기 (활은 MC4.2d에서 먼저 적용 — 검에 같은 장치 확장)
- [ ] MC4.4 개발용 장비창 `WBP_Equipment`, 스킬 슬롯 배열 HUD
- [ ] MC4.5 FX 라이브러리(`Docs/FX_CATALOG.md`): 1~5단계 완료(2026-11-12, Paragon 파티클 193개 → 채택 51개 손질, 도구 `UFxEditLibrary`·`jm_kit.fx_fix`). 남은 것: 중간 테스트(PIE)에서 사망·부활·화상·냉기 시전의 본 이미터와 한손검 공격 궤적(`AM_OneHandSword_Attack` Trail 0~1.5초) 확인 후 구간·폭 조정, 스킬별 `SkillTable.csv` `Fx` 연결(기본 공격 2001/3001 타격·투사체, 버프·텔레그래프는 기능 생길 때), ~~`Paragon*` 삭제 전 FX 의존 Move~~ 2026-11-12 완료(`Content/FX/Dep/<Greystone|Sparrow|Rampage>/`, 'Paragon 에셋 최종 Move' 2)

**MC5 검증·병합**
- [ ] MC5.1 오류·부하 테스트, 클라이언트 성능 측정, SceneCapture 시트 승인(`TESTING.md`), `Jeonghoon` 병합(사용자 승인). 클라이언트 성능은 20명(남녀 각 10) 측정 → 기준 초과 시 `FSkeletalMeshMerge` 시험(`DECISIONS.md` 2026-11-05)

## 수동 작업 기록 (에디터에서 사용자가 직접)
상세 절차는 기록하지 않고 대화창으로 안내합니다. 여기에는 항목별 한 줄 요약과 상태만 남깁니다.

- [x] 검증 1: 기본 무기 본 스케일 0 숨김 (통과, 현재는 사용하지 않음)
- [x] Greystone, Sparrow: 몽타주 Duplicate(`AM_`), AnimBP·피격·사망 Rename, `DT_CharacterAsset` 경로 갱신 (2026-10-19 완료)
- [x] Rampage: `BP_Npc`가 `Character/Rampage/`의 `AM_Rampage_Attack`·`AM_Rampage_Hit`·`AM_Rampage_Death`·`ABP_Rampage`를 사용, SkillTable 301 `Montage` = `AM_Rampage_Attack` (2026-10-30 확인. 메시는 아직 팩 원본 → 최종 Move 대상)
- [ ] Paragon 에셋 최종 Move: 캐릭터 작업 종료 시 (아래 "Paragon 에셋 최종 Move" 참고)
- [x] U1: `WBP_SystemMessage` 생성(부모 `USystemMessageWidget`, `Dim`→`PanelSize`→`Panel`→`TxtMessage`/`BtnOk`), `WBP_Login`의 `TxtResult` 삭제, `BP_JMGameInstance.SystemMessageWidgetClass` 지정 (2026-10-30, 도구로 처리)
- [x] MC1.1: 템플릿 매니퀸 60개를 `/Game/Characters/Mannequins` → `/Game/Character/Mannequin` Move, 참조 확인·저장 (2026-11-04, 도구로 처리)
- [ ] ~~MC1.3: `jm_modular.body_parts.build("M"/"F")` → `Character/Mannequin/Body/SK_Body_*` 12개, `verify()` 통과, 테스트 레벨 `Dev/MC/L_BodyTest`(git 제외, 디버그 머티리얼과 함께 보관)에서 SceneCapture 확인, 중간 결과물 삭제 (2026-11-04, 도구로 처리)~~ 삭제됨
- [ ] ~~MC2.1: 프로토타입 FBX 5개(`Saved/VDL`) → `Dev/MC/Garments` 임포트, 프로토타입·보완판 결과 `Dev/MC/ProtoFit`·`Dev/MC/Fit`(git 제외), 의상 머티리얼 양면 (2026-11-04, 도구로 처리)~~ 삭제됨
- [x] MC2.1b: Fab 키트를 사용자가 Add to Project(`Content/CoreC`, git 제외), `body_parts.build("M"/"F")`로 몸 부위·눈·이·헤어(LOD 3) 재생성, `MI_Skin_<M|F>_<Body|Face>` 4개 생성(`UseComic?` 끔) (2026-11-04, 도구로 처리) — 키트 설치는 유지, 몸 부위·`MI_Skin_*`은 2026-11-04 삭제
- [x] MC4.2e: 천·판금 장갑 재생성(손목 가장자리 정리), 활 2개 부착값 → ItemTable 재생성·`DT_ItemTable` 재임포트, `BP_JMGameInstance` 재저장(`DT_CharacterAsset` 참조 제거), 스킬 아이콘 2개 `Content/UI/Icons/Skill/`로 이동·`DT_SkillTable` 재임포트 (2026-11-10, 도구로 처리)
- [x] MC4.2d-2: Longbow 팩 11개 가져오기·리타깃·제자리 처리, `BS_Bow_Locomotion`·`BS_Bow_AimWalk`·`AM_Bow_Attack/Skill1/Sheathe`·`ABP_Layer_Bow`, DT 3개(Skill·Item·WeaponType) 재임포트, BP_TestPlayer 시작 장비 활 (2026-11-10, 도구로 처리)
- [x] MC4.1: `ABP_PlayerBase`(템플릿 `ABP_Unarmed` 복제·부모 `JMAnimInstance`, 도구로 처리) 상태·전이 안 연결 5개(Walk/Run X·Y, Idle↔Walk/Run, Land→Locomotion) — 공식 MCP가 중첩 그래프에 노드를 못 만들어 사용자가 직접 (2026-11-07 완료)
- [x] MC4.2a: `ALI_Weapon`·`ABP_Layer_Unarmed`·`AM_Unarmed_Attack`·`AM_Common_Death` 생성, `ABP_PlayerBase` 재배선·`RootMotionMode`, `DT_WeaponTypeTable` 생성, `DT_BodyTable`·`DT_SkillTable` Reimport (2026-11-08, 도구로 처리 — `AnimBuilderTools`)
- [x] MC4.2b: 사용자가 Mixamo에서 Y Bot·Standing Draw Arrow FBX 다운로드 → `SourceArt/Mixamo/`, 가져오기·IK 리그·리타기터·리타깃 (2026-11-09, 도구로 처리 — `RetargetTools`)
- [x] MC4.2c: 사용자가 Mixamo Pro Sword and Shield Pack 다운로드 → `SourceArt/Mixamo/Pro Sword and Shield Pack/`, 가져오기·리타깃·블렌드스페이스·레이어·몽타주·DT (2026-11-09, 도구로 처리 — `RetargetTools`·`AnimBuilderTools`)
- [ ] 로딩 화면 이미지: 사용자가 이미지를 구해 오면(2026-11-06 무렵) `WBP_SystemMessage` 대기(Waiting) 배경에 적용
- [x] A2.2f: `WBP_SkillSlot`·`WBP_CombatHud` 생성, `BP_GameMode` 부모 → `AIngameGameModeBase` + `HUDWidgetClass = WBP_CombatHud` (2026-10-28, 도구로 처리)
- [x] A2.2f: 스킬1 아이콘 2개(배경 투명 처리) → `Character/<영웅>/Icons` 가져오기, SkillTable `Icon` 경로 + `DT_SkillTable` Reimport, `WBP_CombatHud`에서 `SlotAttack` 제거 (2026-10-28, 도구로 처리)
- [x] A2.2g: SkillTable `AnimMs` 열 + 501 `Montage`(`AM_Sparrow_Attack`), `ExportSkillTiming.bat`으로 `DT_SkillTable` Reimport (2026-10-28)
- [x] A0.7: `WBP_Login`에 `TxtResult`(Text) + `ComboHero`(ComboBox (String)) 추가, 라벨 `TxtHeroLabel`("가입할 영웅"), 순서 비밀번호 → 영웅 라벨 → 콤보 → 버튼 → 결과 메시지, 글꼴·여백을 기존 항목에 맞춤 (2026-10-28 완료)
- [x] 전사 판정 조정: `AM_Greystone_Attack` 약 0.41초에 Skill Hit 노티파이 → `ExportSkillTiming.bat`(401 `HitDelayMs`, `DT_SkillTable` 갱신) (2026-10-28 완료)
- [x] A2.2e-1b: `P_Greystone_GroundSmash` Copy, `AS/AM_Greystone_Skill2`(전신 `DefaultSlot`), ABP 2개에 `Slot 'DefaultSlot'`, 몽타주 2개에 Skill Hit 노티파이, `ExportSkillTiming.bat`으로 `DT_SkillTable` 갱신 (2026-10-28 완료)
- [x] A2.2e-1a: `AM_Sparrow_Skill1`(실제로는 시퀀스) → `AS_Sparrow_Skill1` 이름 변경, 몽타주 `AM_Sparrow_Skill1`(슬롯 `UpperBody`) 생성, `DT_SkillTable` Reimport (2026-10-27 완료)
- [x] 장판 이펙트: `P_RainofArrows` → `Character/Sparrow/P_Sparrow_RainofArrows` Copy, `DT_SkillTable` Reimport (2026-10-27 완료)
- [x] M0.5-4 Content 정리 (2026-10-21): `Input/Touch` 삭제, `BP_JMGameInstance`의 깨진 변수 `DT_Appearance/Equip` 제거, `BP_Player`/`BP_MyPlayer`의 `ABP_Base`·Mannequin 참조 해제 후 `ABP_Base`·`NewInterchange…` 삭제, `BP_MyPlayer/Player/Npc` → `_Core/Blueprints/Character/`, `DT_CharacterAsset` → `Data/` 이동(커맨드렛 Python, 리디렉터 없음), `DefaultInput.ini` 레거시 Action/Axis Mappings 삭제, `Characters/`·`FirstPerson/`·`LevelPrototyping/` 삭제

## Paragon 에셋 최종 Move (캐릭터 작업 종료 시, 2026-10-17·10-19 방침)
재다운로드한 Paragon 전체 팩은 커밋하지 않고(`.gitignore`), 실제 쓰는 에셋과 의존 에셋만 `Content/Character/<영웅>/`(Greystone, Sparrow, Rampage)로 **Move**해 커밋합니다. 고칠 에셋(몽타주·AnimBP·피격·사망)은 이미 Duplicate 완료, 무거운 에셋(메시·스켈레톤·머티리얼·텍스처·시퀀스)이 남아 있습니다. 한 단계씩 사용자에게 묻고 진행합니다.
1. 영웅 폴더와 `Characters/Global`을 Move → Fix Up Redirectors → `BP_Npc` 확인 → Save All → `Paragon*` 삭제 → PIE 확인 (Reference Viewer로 의존 확인)
2. ~~FX 의존 Move + Greystone·Sparrow 삭제~~ **2026-11-12 완료**: `refs_move`로 Greystone·Sparrow FX 의존 243개(루트 = 지울 폴더 바깥 `/Game` 전체)와 Rampage FX 의존 272개(루트 `/Game/FX`)를 `Content/FX/Dep/<팩>/`(팩의 `FX/` 아래 구조 유지)로 Move — 리디렉터 0, `Content/FX`의 Paragon 참조 0, 막는 참조 0. 그 뒤 에디터를 닫고 `ParagonGreystone`·`ParagonSparrow`·`Character/Greystone`·`Character/Sparrow`(옛 AnimBP·몽타주·`BP_Arrow`) 삭제, 재실행 후 끊긴 참조 0·FX 51개 이미터 429개 머티리얼 정상·로드 오류 없음(`DECISIONS.md` 2026-11-12). 검 `SM_Greystone_Blade_01`은 쓰지 않음(팩과 함께 삭제). 이전 기록: 화살은 2026-11-12 `/Game/Equipment/Common/SM_Arrow`로 복제 완료(Move 불필요, `BP_Arrow`는 참조 0 → 삭제). **`Content/FX` 51개가 `Paragon{Sparrow,Greystone,Rampage}/FX`의 머티리얼·메시·텍스처·벡터필드에 의존**(예: `P_AoE_ArrowRain` 25개)하므로 `Paragon*`을 지우기 전에 `refs_find`(루트 `/Game/FX`, 금지 접두사 Paragon)로 사슬을 뽑아 `Content/FX/Dep/` 등으로 Move → Fix Up Redirectors → 참조 0 확인
3. 영웅 하나가 끝날 때마다 `Content/Character/**`와 소스·문서만 체크포인트 커밋. 원본 `Paragon*` 폴더의 재다운로드 변경분은 커밋하지 않음(복원 또는 제외 방식은 그때 결정)
