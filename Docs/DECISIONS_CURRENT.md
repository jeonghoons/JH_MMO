# DECISIONS_CURRENT

지금 유효한 결정만 주제별로 한 줄씩 모은 요약입니다. 평소에는 이 파일만 읽습니다.
원본 로그 [DECISIONS.md](DECISIONS.md)(날짜|결정|이유 표)는 그대로 두며, 근거·세부·번복 이력은 원본에서 날짜와 ID로 찾습니다.
ID `D-NNN`의 NNN = 원본 표의 데이터 행 순번(R#, 세 자리). 새 결정은 원본 표 끝에 추가하고, 다음 번호로 여기에도 한 줄 추가합니다.
`(일부: …)` = 원본 행 중 유효한 부분만 남김. 여러 행을 합친 줄은 ID를 모두 적습니다.

## 원칙·범위

- **D-002** (2026-10-16) "나중에 서버가 할 일"로 둔 동작은 모두 서버 권위(CS_ → 서버 검증·계산 → SC_)로, `IGameBackend`/`ULocalGameBackend`는 들이지 않음
- **D-104** (2026-10-01) 스탯·장비·스킬·이동·UI 수치는 DataTable 기반, 코드 하드코딩 금지
- **D-015** (2026-10-20) 목표 = 서버·클라 구조 시연용 포트폴리오 MMORPG(몬스터 사냥, 아이템 드랍, 유저 거래, 동접 테스트)
- **D-017** (2026-10-20) 동접 목표 1,000 → 5,000 단계적, 서버+봇을 한 PC에서 돌리면 보고서에 명시 (일부: `MAX_TEST` 인용 금지는 대상 상수가 없어 제외)
- **D-018** (2026-10-20) 거래는 전투·아이템 이후 착수, 범위는 1:1 개인 거래 + Room 간 경매장 전체
- **D-019** (2026-10-20) 콘텐츠가 추가되는 단계는 오류 테스트·부하 테스트(검증 게이트)를 통과해야 완료 (일부: 당시 작업 우선순위는 낡음, 부하 범위는 D-147)

## 작업 방식·저장소

- **D-062** (2026-11-04) `git push`는 사용자 지시가 있을 때만(저장소 전체 규칙) (일부: `content/dev` 운용은 D-143으로 대체)
- **D-143** (2026-11-12) `Jeonghoon` = 유일한 개발 줄기(ROADMAP 항목 단위 커밋), `main` = 안정판(마일스톤 끝에 병합+태그), 시험만 짧은 `exp/<이름>`(며칠 안에 병합·폐기 후 삭제), 트랙 구분은 ROADMAP으로 — `.uasset`은 병합 불가
- **D-144** (2026-11-12) 원본 `DECISIONS.md`는 보존하고 평소엔 이 요약을 읽음. 진행 문서는 ROADMAP 하나, 상태 표기 `[예정]`·`[진행]`·`[확인 대기]`·`[완료]`·`[보류]`·`[폐기]`. 분류 변경은 사용자 승인 후, 함정·상시 판단은 `CLAUDE.md` — 작업 전 읽는 분량 축소
- **D-144** (2026-11-12) `.proto` 재생성은 `GanPackets.bat` + git pre-commit + Stop 훅으로 강제, 판독 전용 `reader` 에이전트, 절차형 메모리는 프로젝트 스킬로 — PostToolUse는 중간 상태에서 실패
- **D-145** (2026-11-15) 번복된 안은 끝 절 "채택하지 않은 안"에 이유와 함께 한 줄씩(원본에 이유가 없으면 `(추정)` 또는 "기록 없음"), 요약 줄 끝에 이미 이유가 있는 번복은 따로 적지 않음
- **D-146** (2026-11-15) 리뷰 모델은 버전 고정 없이 "최상위 모델 high", M3 Paragon Move 범위는 `ParagonRampage`까지, MC5.1 클라 성능 기준 수치는 측정 전에 정함 (일부: 검증 게이트의 항목별 200 → 1,000 → 5,000 램프는 D-147로 완화)
- **D-147** (2026-11-16) 로드맵 = 마일스톤 3개(1 한 바퀴: 사냥 → 드랍 → 줍기 → 장착 → 거래·경매 + 최소 UI → 2 수치: 성능·동접 5,000 확정 → 3 산출물). 검증 게이트는 서버 기능 항목마다 오류 테스트 + 200명 짧은 램프, 1,000 → 5,000 램프는 마일스톤 2에서 한 번에. 경매장 유지. 콘텐츠(외형·애니메이션·이펙트) 다듬기는 동결 — 기능이 동작하면 통과, 어색한 점은 백로그에 한 줄. P2~P4·A4.4는 램프에서 병목으로 드러난 것만. MC4.5 `Fx` 연결은 하되 이펙트 선택은 나중에
- **D-013** (2026-10-19) 외부 팩 에셋 중 고칠 것(몽타주·AnimBP 등)은 Duplicate해 새 이름으로 관리, Move 뒤 Fix Up Redirectors 필수 (일부: 팩 일괄 Move는 D-142로 대체)
- **D-023** (2026-10-21) 외부 팩(`Paragon*`·`KiteDemo`·`StarterContent`)은 git 제외, 그 밖은 git 추적. 클래스 BP `_Core/Blueprints/`, UI `_Core/UI/`, DataTable `Data/`, 캐릭터·NPC BP는 `Character/` 하위. `ParagonSample` 맵은 팩을 참조해 자체 맵 전까지 유지 (일부: 영웅 폴더는 낡음)
- **D-023** (2026-10-21) `Maps/TestMap`은 삭제됨 — 서버 없는 테스트 레벨은 `/Game/Maps/TestLevel` (원본의 "TestMap 유지"는 낡음)
- **D-014·D-030·D-072·D-141** (2026-11-12) 접두사 `SK_` `SM_` `ABP_` `ALI_` `AM_` `AS_` `M_` `MI_` `T_` `DT_` `P_`, 아이콘 `T_Icon_<…>`, 무기 애셋 `AS_`·`AM_`·`BS_<무기 애셋명>_<동작>`(검 = `OneHandSword` ≠ WeaponType `Sword`), 공용 `AM_Common_<동작>` (일부: 영웅 이름 포함·`MI_Skin`·`SK_Body` 규칙은 폐기)
- **D-072** (2026-11-04) 폴더: 베이스 `Content/Character/Mannequin/`(`Content/Characters/`는 gitignore 경로라 안 씀), 장비 `Content/Equipment/<Tier>/<Slot>/`, 이펙트 `Content/FX/`, 원본 `SourceArt/`, 시각 품질은 SceneCapture 시트 승인 후 `TESTING.md` (일부: 도구 위치는 D-076)
- **D-076** (2026-11-04) 에셋 제작 Python 도구는 `UEClient/JM/Content/Python/`(에디터 경로 자동, 쿠킹 안 됨), 에디터 없는 내보내기는 C++ 커맨드렛 `JMEditorTools` (일부: `jm_modular`는 D-083으로 삭제, 지금은 `jm_kit`)

## 서버 기반

- **D-016** (2026-10-20) DB는 SQL Server Express(ODBC) 유지, 설정 외부화·README 설치 절차로 재현성 보완
- **D-150** (2026-11-16) DB 스키마 = `Schema_Version` + `migrations/NNNN_*.sql`(한 트랜잭션·재실행 안전·자기 버전 행 INSERT) + `migrate.ps1`(백업 후 순번대로) + 서버 시작 검사(`DB_SCHEMA_VERSION`과 다르면 시작 거부, 서버는 마이그레이션을 실행하지 않음). SQL은 저장 프로시저 없이 인라인, 문자열은 `Server/Server/DB/DbQueries.h`에만. 스키마를 바꾸면 마이그레이션 + `schema.sql` + `DB_SCHEMA_VERSION`을 함께
- **D-151** (2026-11-16) 진행 상황(레벨·경험치·HP·골드) 저장 = `DB_PlayerProgress` 스냅숏이 DB 값과 다를 때만, 주기(`ServerConfig` `PlayerSaveIntervalSec` 60초, id로 분산) + 퇴장, 플레이어당 동시 1건(`Player::SaveState`), 더미 봇 제외. 로그인: DB 레벨로 `PlayerStatTable` 조회(없으면 `SERVER_ERROR`), HP = DB 값을 최대치 안으로(저장 이력 없으면 최대치, 0 이하면 부활 규칙 값). 죽은 채 끊으면 HP = 최대 × `ReviveHpPercent`로 저장. 중복 접속 차단은 A2.7, 위치 저장 없음
- **D-048** (2026-10-30) `Server/ServerCore` 정적 lib(Network/Job/DB/Utils) + GameServer 도메인 폴더. Core는 Protocol·게임 헤더 금지, 가상 훅·`SessionFactory`로 연결, 같은 파일명 금지, `#include`는 프로젝트 루트 기준 (일부: 봇의 Core 재사용은 D-051로 대체)
- **D-044** (2026-10-28) DummyClient 유지 = LoadTest와 같은 봇 엔진의 부하 테스트 시각화 프런트엔드 — 검증은 콘솔 LoadTest, 시연은 DummyClient
- **D-051** (2026-10-30) 봇 네트워크는 논블로킹 + WSAPoll 루프를 `Server/BotEngine`(정적 lib)으로, LoadTest·DummyClient 공유
- **D-054** (2026-10-30) 봇 길 찾기 없음: 서버는 갈 수 없는 이동만 막고 봇은 직선 이동(막히면 무작위·목표 변경)
- **D-055** (2026-10-31) 서버 설정 = 계층형 INI(기본값 → `ServerConfig.ini` → `.local.ini` → 환경변수 `JH_DB_CONNECTION` → 명령줄), 시작 시 엄격 검증(fail-fast), `PWD` 가림, 게임 수치는 DataTable
- **D-056** (2026-10-31) 로거 = 자체 비동기(`Utils/Logger`): 게임 스레드는 push만, 큐 가득 차면(65,536) 버리고 `logDrop` 집계. 서버 소스·출력 UTF-8 통일 — 게임 스레드를 막지 않는 것이 우선
- **D-052** (2026-10-30) 이동 = 클라 예측 + 서버 검증·보정(속도 토큰 버킷·내비메시·NaN·빈도, `correction_seq`로 연쇄 차단), 속도는 서버 스탯 `MoveSpeed`, 허용치는 `MovementTable` `Server*` 열
- **D-053** (2026-10-30) 서버 내비메시는 충돌 켜짐·Pawn Block 스태틱 메시만 `MapExportCommandlet`으로, Detour 좌표(y,z,-x)·정점 순서 반대, 경계 여유는 수평 거리 `ServerNavToleranceCm`(50) — 풀 위 되돌림 버그
- **D-123** (2026-11-07) 원격 이동 튐: 방향이 `MoveSendTurnDeg`(30°) 넘게 바뀌면 즉시 송신 + 원격 따라잡기 상한 = 이동 스탯 × `RemoteCatchUpSpeedMul`(1.3). 남은 다듬기는 P2·P3에서 turn 회귀 측정과 함께
- **D-049** (2026-10-30) 채팅: 같은 방 전체 방송(본인 포함), 이름은 `SC_CHAT.sender_name`, 제어 문자·공백 정리, 빈 채팅 무시, 100자(UTF-16), 0.5초 간격 제한 — AOI 밖 이름을 클라가 모름
- **D-037** (2026-10-28) 로그인은 `LoginResult`로 성공·실패 모두 응답, 가입은 `SC_SIGNUP_PACKET.result`, 성공 때만 입장. 가입 때 체형을 `CS_SIGNUP.player_type`으로 (일부: 영웅 선택 → 체형)
- **D-057** (2026-10-31) 합격선: 틱 본문·틱 지연 p99 ≤ 20ms, 핑 p99 ≤ 50ms, 핫존 1인 수신 ≤ 64KB/s, 송신 큐 증가 없음, CPU ≤ 70%, 실패·logDrop 0, 메모리 안정. AOI = 큰 셀 + 쿼드트리, 원형 시야 R + H, 수치는 `WorldTable.csv`
- **D-057** (2026-10-31) 시야 정확도 검증은 보여야 하는 경우(≤R)와 보이면 안 되는 경우(>R+H) 모두 — 서버 감사 + 경계 시나리오. 측정은 실제 이동 빈도(200ms)
- **D-058** (2026-10-31) CPU 70%는 전체 코어 기준(`server_cpu_pct` ÷ 논리 코어), 핫존은 `[Debug] DummySpawnZone`으로 서버가 더미 봇 배치 + 봇 `--zone`, 중심 (3150, −1130) 반경 10m
- **D-059** (2026-10-31) 시야 R 50m·H 5m(`WorldTable` 5000·500), 감사 대상 = 플레이어가 보는 모든 캐릭터, `DummySpawnZone`은 여러 구역 — 기본 카메라에서 50m ≈ 33px가 식별 한계
- **D-060** (2026-10-31) `AoiIndex`(셀 50m, 노드 8명 초과 4분할, 최소 5m, 절반 이하 병합), 몬스터 배회 `MonsterTable.WanderRadiusCm`(500, 행동트리 때 재검토), `NPCMove`는 이동 후 호출
- **D-061** (2026-10-31) 성능 개선 P1 송신 묶기 → P2 이동 패킷 묶기 → P3 거리별 빈도 → P4 몬스터 깨우기 범위 분리, 각각 같은 매트릭스로 재측정. 측정은 같은 PC (일부: P1 세부는 D-100)
- **D-100** (2026-11-06) P0 유령 플레이어를 먼저 분리, P1 = P1a 송신 지표 → P1b 송신 묶기(잡 N개·시간 상한마다), `WorkerThreads` 기본 유지 — 정확성·성능 수정을 섞지 않음

## 데이터

- **D-003** (2026-10-16) 데이터 허브 = `Common/DataTable/` CSV + 정수 ID 규약, 서버도 같은 CSV를 읽음
- **D-110** (2026-11-07) StatTable 폐지: 플레이어 `PlayerStatTable`(키 Level, 체형 공용), 몬스터 스탯은 `MonsterTable` 열
- **D-065** (2026-11-04) 기본 스탯 체형 공용(MaxHp 500, AttackSpeed 1.0, MoveSpeed 500, AttackDamage 0), 보너스: 무기 = 공격력, 방어구 = MaxHp(서버 계산) (일부: StatTable은 D-110)
- **D-113** (2026-11-07) 가입 때 DB Level·Hp는 PlayerStatTable 시작 레벨 값(SQL 하드코딩 제거), 로그인은 DB 레벨로 스탯 조회(D-151). 가입 HP는 장비 보너스 전 값이라 저장 이력이 없으면 로그인 때 최대치
- **D-111** (2026-11-07) MonsterTable ID 1001~, `ObjectInfo.template_id`로 전송·클라는 `object_type`으로 판별, `PlayerType`은 체형 전용(3·4·5 reserved), 맵 JSON에 없는 템플릿은 그 마커만 건너뜀
- **D-109** (2026-11-07) itemId 4자리 [슬롯][티어][01~99](슬롯 Head1~Weapon6·Offhand7, 티어 Cloth1·Leather2·Plate3), 위반 시 시작 중단. 보너스 천/판금 방어구 +30/+80 HP, 무기 +10/+20 (일부: 6칸 → 7칸)
- **D-118** (2026-11-07) 기본 장비 = `ItemTable.Starter` 열(슬롯당 최대 1), 가입 트랜잭션에서 지급, 마이그레이션 SQL 목록과 같아야 함을 명시
- **D-121** (2026-11-07) `BodyTable.csv`(클라 전용, 체형별 Leader·Eyes·Teeth·Hair·BodyMaterial·AnimClass), ItemTable 외형 열은 `jm_kit.item_table.export`가 생성(손 수정 금지) (일부: ABP 이름은 D-122)
- **D-121** (2026-11-07) 슬롯 기본 마스크는 아이템 행에 합쳐 넣음. 같은 외형·다른 등급 아이템이 생기면 외형 열을 `ItemVisualTable`로 분리
- **D-124** (2026-11-08) `WeaponTypeTable.csv`(클라 전용, Name 1 Unarmed·2 Sword·3 Bow = SkillTable ID 첫 자리, `AnimLayer` 등 무기 타입별 값), 피격·사망 몽타주 = BodyTable `HitMontage`·`DeathMontage` (BodyTable도 생성 표 — `kit_profiles.json`에서 고침, 손 수정 금지)
- **D-030·D-031** (2026-10-27) SkillTable 클라 전용 열: `Montage`(스킬 몽타주), `Fx`·`FxBaseRadius`(재생 배율 = `Radius` ÷ `FxBaseRadius`), `Icon` (일부: 옛 `ZoneFx*`·영웅 에셋은 낡음)

## 전투·스킬

- **D-064** (2026-11-04) 직업 없음: 캐릭터 = 체형(가입 때 고정), 스킬 = 장착 무기 타입(Unarmed/Sword/Bow, 몬스터 Monster), 아무 무기나 장착, 빈 무기 슬롯 = 맨손(기본 공격 있음)
- **D-064** (2026-11-04) 무기당 슬롯 0 기본 공격 + 슬롯 1 스킬, 2~5는 CSV 행 추가로 확장. SkillTable ID 앞자리 맨손 1·검 2·활 3·몬스터 9, 몬스터는 `MonsterTable.SkillId`
- **D-105** (2026-10-01) 콤보 공격 없음, 스킬은 숫자키 + 무기별 스킬 목록 (일부: "맨손 공격 불가"는 D-064로 번복)
- **D-112** (2026-11-07) 피해 = 스킬 BaseDamage + 시전자 공격력(플레이어·몬스터 단일 공식, 시전 순간 확정), 몬스터 공격력은 MonsterTable 고정, 방어력은 나중(A2.3 무렵) — 공식 하나로 데이터만 조정
- **D-041** (2026-10-28) 행동 시간 = `AnimMs` 동안 공용 행동 잠금, 기본 공격 쿨타임 0, 스킬은 시전부터 `CooldownMs`, AttackSpeed = 기본 공격 재생 배율(스킬 미적용), HUD는 스킬만 쿨타임
- **D-029** (2026-10-27) 쿨타임 표시는 서버 `SC_ATTACK.cooldown_ms`로 받고 클라는 공식을 복제하지 않음 (일부: 당시 작업 순서는 낡음)
- **D-036** (2026-10-28) 이미 발사된 투사체 피해는 공격자가 사라져도 적용(`attacker_id` 유지), 공격자가 없으면 어그로는 바꾸지 않음
- **D-022** (2026-10-21) 3인칭 마우스 회전 카메라, 전투 중 커서 숨김, Left Alt 동안 자유 커서. 조준은 크로스헤어 기준, 좌클릭 확정·숫자키 조준 진입·우클릭 취소 (일부: 전사/궁수 → 무기 타입은 D-064)
- **D-031** (2026-10-27) 스킬 이펙트: 장판은 조준 지점에 `DurationMs` 동안, 부채꼴·직선은 `HitDelayMs`에 시전자 발밑에서 시전 방향으로 1회 (일부: 영웅 팩 에셋은 낡음)
- **D-028** (2026-10-27) 활 장판(3002) `Radius` 400 = 이펙트 원본 크기(`FxBaseRadius` 400) — 이펙트 축소 대신 밸런스를 에셋에 맞춤 (일부: 이펙트는 지금 `P_AoE_ArrowRain`)
- **D-034** (2026-10-28) 검 2002 `Angle` 360(반지름 300 원형), 2001 `Radius` 190·`Angle` 180 (측정 근거는 옛 Greystone 모션 — 재측정은 ROADMAP 백로그)
- **D-126** (2026-11-08) 스킬 조준은 서버 동기화: 상태가 바뀔 때만 전용 패킷, 서버가 무기·쿨타임·생존 검증 후 이동 예산 감속·AOI 방송 — "조준은 로컬 전용" 번복 (일부: skill_id → slot은 D-133)
- **D-133** (2026-11-09) `CS_SKILL_AIM`(1011){slot,active,seq}·`SC_SKILL_AIM`(2014), 늦게 본 사람은 `ObjectInfo.aim_skill_id`, SkillTable `AimMoveSpeedMul`·`AimMaxMs`, `ServerAimMinIntervalMs` 200
- **D-133** (2026-11-09) `CS_ATTACK` 수신 시 검증 전 조준 해제, 내 조준 상태는 로컬 권위(서버 `false`는 seq가 같을 때만), 이동 예산 배수는 이전 배수로 정산 후 교체
- **D-149** (2026-11-16) 플레이어 부활은 요청형: 사망 후 `WorldTable.ReviveDelaySec`가 지나면 `CS_REVIVE`(1012) → 서버가 사망·대기 시간 검증(사망 시각 비교, 타이머 없음) → 입장 때 배치된 자리(플레이어 = 첫 PlayerStart)에서 최대 HP × `ReviveHpPercent`로 부활, `SC_REVIVE`(2015: id·위치·HP·`correction_seq`) AOI 방송, 대기 시간은 `SC_DEAD.revive_delay_ms`로 전달, 패널티 없음. 사망 상태 = `stat.hp == 0`(전용 필드 없음, 늦게 본 사람은 스폰 때 hp 0이면 사망 자세)
- **D-149** (2026-11-16) 사망 중 이동은 서버가 조용히 버리고 클라도 이동·공격 입력 차단, 채팅·핑·조준 끄기는 허용. 시체는 AOI에 남김, 사망 중 재접속은 살아서 입장(HP = 최대 × `ReviveHpPercent`로 저장, D-151). 사망 연출은 몽타주 + 멈춤 유지·부활 때 해제(AnimBP `bIsDead` 전환은 백로그), 부활 UI = `WBP_Revive`, 봇은 자기 `SC_DEAD` 후 대기 → `CS_REVIVE`

## 장비 시스템

- **D-066** (2026-11-04) 전투 중 교체 허용, 시전·사망 중 거부, 무기 교체 시 행동 잠금(1000ms), 최대 HP가 줄면 현재 HP를 자름, 결과는 `SC_CHANGE_EQUIP`(스탯 포함) AOI 방송 (일부: 열 이름은 D-116)
- **D-116** (2026-11-07) 장착 빈도 제한 캐릭터당 200ms `MovementTable.ServerEquipMinIntervalMs`, 스왑 잠금 열은 `ServerWeaponSwapLockMs` — 서버 열은 `Server` 접두사
- **D-117** (2026-11-07) 무기 해제(itemId 0)도 스왑 잠금, 스왑 잠금 중 방어구 교체는 허용(시전 애니메이션 중만 `ACTING` 거부)
- **D-114·D-115** (2026-11-07) 거부는 본인 전용 `SC_CHANGE_EQUIP_RESULT`(`EquipResult`), 클라가 코드별 문구를 `ShowSystemMessage`로. `SC_CHANGE_EQUIP`은 성공 방송 전용
- **D-067·D-114** (2026-11-07) 개발용 장비창 `WBP_Equipment`는 미제작, 콘솔 `Equip <part> <itemId>`가 대신(MC4.4 장비창이 생기면 삭제). 보유·레벨 제한은 A3 (일부: 기본 장비 지급은 D-118)
- **D-067** (2026-11-04) 장비만 DB 저장(`Player_Equipped`), `ItemDbId`는 A3 전 0, 캐릭터 이름 = 로그인 ID
- **D-119** (2026-11-07) 로그인 HP = DB `Hp`를 장비 반영 최대치 안으로(D-151), `Player_Equipped` 슬롯별 검증 후 `RecalcStat`, 체형이 아니면 `LOGIN_RESULT_SERVER_ERROR` (일부: 슬롯 6 → 7칸)
- **D-120** (2026-11-07) 장비 저장 = 변경 즉시 7칸 스냅숏(한 트랜잭션), 플레이어당 동시 1건·진행 중 변경은 합침, 실패는 다음 변경·퇴장 때 재시도 — 측정: CPU +2%p 이내 (일부: 6 → 7칸)
- **D-130** (2026-11-09) 방패 = 보조손 슬롯 `OFFHAND`(EquipPart 7) 정적 메시, 막기 동작·판정 없음, 스탯 효과 지금 0, 한손검 전용(아니면 거부, 다른 무기로 바꾸면 서버가 자동 해제)
- **D-139** (2026-11-10) `EQUIP_RESULT_NEED_SWORD`(8), 자동 해제는 보조손 해제 패킷 먼저·무기 변경 다음, 로그인 때 어긋나면 보조손만 비움, 7101 천·7301 판금 방패(보너스 0), Sword끼리 교체는 방패 유지, 해제는 언제나 허용

## 캐릭터·아트·장비 제작

- **D-063** (2026-11-04) 플레이어 = 매니퀸 공용 스켈레톤 모듈형: 슬롯별 파츠 교체 + 장비가 덮는 몸 숨김(Leader Pose), 무기는 정적 메시 소켓 부착 (일부: 베이스 출처·브랜치 문구 제외)
- **D-006** (2026-10-17) Paragon Rampage는 몬스터용 (일부: 플레이어 영웅은 D-063으로 대체)
- **D-079** (2026-11-04) 베이스 바디 = Fab CoreC 키트 남/여(`SK_Mannequin`과 본 89개 이름·계층 동일), `Content/CoreC/`는 git 제외·Fab에서 설치 (일부: 부위 분할은 D-083·D-086)
- **D-083** (2026-11-04) 몸은 직접 분할하지 않음, 장비 메시는 키트 의상과 같은 뼈대(89본·이름·순서·바인드 포즈) — 다른 기준 자세로 바인딩하면 팔·소매가 벌어짐
- **D-086** (2026-11-05) 몸 = 키트 `Body_Combined` 하나(리더 = 보이는 몸, 몸 섹션에 마스크 MID) + 눈·이·헤어만 별도, 장갑용 `HideHands` — 몸 컴포넌트 6 → 1, 드로우콜 6 → 2
- **D-085** (2026-11-05) 몸 마스킹: `T_BodyMask_<M|F>` + `M_Body_Masked`/`MI_Body_<M|F>`, 상의 = 몸통·팔, 하의 = 다리·엉덩이(손목·발목 8cm 남김), 신발 = 발, 아이템이 덮어씀 — 통째 숨김은 소매 안이 빔
- **D-084** (2026-11-04) 몸·헤어 머티리얼은 키트 원본(만화 셰이더 망점 포함, 헤어 HairA05), 체형별 경로는 BodyTable (일부: 부위 조립은 D-086으로 대체)
- **D-081** (2026-11-04) 몸 폴리는 키트 원본 LOD0 유지(남 약 3.7만·여 약 4.9만), 장비 슬롯 예산은 그대로 (일부: `body_parts` LOD 생성은 삭제됨)
- **D-070** (2026-11-04) 슬롯 예산 상의 6천·하의 5천·장갑 2천·장화 3천·투구 4천·무기 5천, 텍스처 2048. 투구·무기는 정적 메시 부착, 의상만 스켈레탈, 캡슐 42/96 (일부: 슬롯 6 → 7, 몸 예산은 D-081)
- **D-080** (2026-11-04) 아트 = 반실사(실사 비율·PBR + painterly 텍스처), 셀 셰이딩·망점 없음(몸·헤어는 D-084 예외), 장비는 이 스타일로 새로 생성
- **D-074** (2026-11-04) 디자인은 자체 제작: 참고 게임 이름을 프롬프트에 넣지 않고 실제 세트·캐릭터·문양을 복제하지 않음 (일부: 고사실 스타일은 D-080으로 대체)
- **D-071** (2026-11-04) 서양 중세 판타지, 장비는 재질 티어(천 → 가죽 → 판금), 등급은 데이터·아이콘 테두리로만, 아이콘은 최종 메시 SceneCapture (일부: 스타일은 D-080, 이펙트는 D-141)
- **D-073** (2026-11-04) 10m에서 티어 구분·50m는 실루엣, 세트 주조색 통일, 테두리 `#9D9D9D`·`#1EFF00`·`#0070DD`, 아이콘 512², 아이템당 머티리얼 1개 — 테두리는 UI에서(미구현)
- **D-101** (2026-11-04) 장비는 모두 VARCO로 새로 생성, 키트 기본 의상(Techwear01)을 의상 틀로(틀 상자 맞춤, 6cm 안 정점은 틀 가중치, 틀 PA)
- **D-092** (2026-11-05) 상의·하의·장화는 키트 의상 사진 → `EditImage` → 3D, 프롬프트 "한 겹 닫힌 옷, 장식은 평평하게", 의상 VARCO 폴리 = 슬롯 예산 (일부: 장갑은 D-093, 정적 장비는 D-098)
- **D-078** (2026-11-04) 의상 맞춤 원칙: 슬롯별 허용 본으로 가중치 정리, 관통 보정, UE simplify로 예산까지, 의상 머티리얼 양면, 머티리얼 슬롯은 생성 옵션 (일부: `jm_modular`는 삭제, 지금은 `jm_kit.garment`)
- **D-093** (2026-11-05) 장갑 = 키트 손 형상(`fit.method="kit_hand"`) + `glove_paint` 무늬(패턴 `SourceArt/Patterns/`), VARCO 텍스처 얼룩은 `Texture3D`로 재텍스처
- **D-095** (2026-11-06) 판금 장갑 = `glove_paint` `Plate` 스타일(패턴은 컨셉 이미지에서, ORM도 칠함), 정적 부착 투구는 `fit: "helm"`(균일 배율) (일부: 확인 대기 문구는 낡음)
- **D-097** (2026-11-06) 투구 = 정적 메시 + `head` 소켓, `fit: "helm"`(헤어 뺀 머리 상자 균일 배율, `yaw`), 메시는 VARCO 단일 이미지 (일부: 3/4 시점 입력은 D-098)
- **D-098** (2026-11-06) 단단한 정적 장비는 눈높이 입력·고폴리(3만) 생성 → UE 축소로 예산·LOD 3단계(`attach.target_tris`), 투구 사슬 제거 — VARCO Remesh는 텍스처 깨짐
- **D-094** (2026-11-06) 장비 공용 `M_Equip_PBR`(BaseColor·Normal·ORM = AO/거칠기/금속도), FBX 가져오기 인스턴스는 부모를 교체(`use_pbr_master`)
- **D-137** (2026-11-10) 상의 어깨 가중치는 안 바꿈(보정 모프는 후속), 장갑 손목 `fit.clean_cuff`, 활 `attach.offset` [2,0,0], 검 쥐는 위치 유지 (일부: 아이콘·이펙트 처리는 D-138·D-142)
- **D-139** (2026-11-10) 방패 아트: 천 둥근 참나무·판금 강철, 3만 생성 → LOD0 5천, `hand_l` 손등 방향 앞면(`fit: "shield"`) — Mixamo 동작이 팔뚝에 찬 자세
- **D-091** (2026-11-05) Mutable 보류(Beta 해제 또는 공개 API 때 재검토), 조립은 교체 가능한 인터페이스 뒤에. 성능은 MC5 20명 측정 → `FSkeletalMeshMerge` → Mutable 순
- **D-141** (2026-11-12) FX = 무기 3종 역할당 1~3개, 이름 `Content/FX/<분류>/P_<역할>_<이름>`, Water 이미터 삭제, 화살 메시 `/Game/Equipment/Common/SM_Arrow`
- **D-141** (2026-11-12) AnimTrail 소켓 `FX_Weapon_Bottom/Top`은 체형별 리더 메시 `hand_r`에, 텔레그래프는 `M_Radius_Indicator` 메시로 별도 제작
- **D-142** (2026-11-12) Paragon Greystone·Sparrow 팩 삭제, FX 의존 에셋은 `Content/FX/Dep/<팩>/`으로 Move, Rampage 팩은 몬스터용 유지 — FX가 팩을 계속 참조하고 있었음

## 애니메이션

- **D-033·D-068·D-106** (2026-10-27) 타격 시점: `AnimNotify_SkillHit`을 `ExportSkillTiming.bat`이 `SkillTable.csv`(`HitDelayMs`·`AnimMs`)로 추출, 서버가 읽어(`ServerData.cpp`) 그 시각에 판정(`Room::CastSkill`). AnimNotify는 연출 전용
- **D-068** (2026-11-04) 공용 ABP + 무기 타입별 레이어(`ALI_Weapon`), 비전투 시 무기는 등에 — 지금은 활만, 값이 빈 타입(검)은 항상 손(MC4.3 확장), 피격엔 안 꺼냄(`SC_ATTACK` 즉시 손, `SheatheDelaySec` 후 등, 처음 보이면 등) — 클라 연출 (일부: 이름은 D-122, 출처는 D-128)
- **D-122** (2026-11-07) 플레이어 공용 AnimBP = `ABP_PlayerBase`(`/Game/Character/Mannequin/Anims/`, 부모 `UJMAnimInstance`), 레이어 `ABP_Layer_<무기>` — 몬스터는 스켈레톤이 달라 공유 불가
- **D-127** (2026-11-08) `IgnoreRootMotion`(위치는 서버 권위), 레이어 `LocomotionLayer`·`UpperBodyLayer`, `UpperBody` 슬롯은 `spine_01` 위, `DefaultSlot` 전신, 레이어 기본 구현 = 맨손
- **D-127** (2026-11-08) AnimGraph 편집은 `UAnimAssetBuilder` + `jm_mcp.AnimBuilderTools`, 기존 AnimBP는 복제본에서 먼저 컴파일·덤프 확인
- **D-032** (2026-10-27) 몽타주 재생 범위는 슬롯으로: `UpperBody` = 상체(하체는 이동), `DefaultSlot` = 전신 (일부: 영웅 AnimBP·402 교체는 낡음)
- **D-128** (2026-11-09) 모션 출처: 매니퀸 템플릿 → Mixamo → VARCO Animate → 모션 없이 소켓 전환, GASP·Lyra는 안 씀. Mixamo FBX 원본은 `SourceArt/Mixamo/`(`*.fbx` git 제외)
- **D-128** (2026-11-09) Mixamo 다운로드: FBX Binary·Without Skin·30fps·Keyframe Reduction none·In Place. 원본 재배포 금지, 리타깃 결과 uasset은 커밋 (상세: `Docs/ExternalAssets.md`)
- **D-125** (2026-11-08) 플레이어 모션에 Paragon 애니메이션은 안 씀(이펙트는 사용), Fab 유료 팩은 보류 (일부: 출처 순서는 D-128)
- **D-129** (2026-11-09) 리타기터 `RTG_Mixamo_Mannequin` 하나(FUZZY 자동 매핑, `*Metacarpal` 8개 제외, `JM_AutoAlign`) (일부: Root Motion·Pin IK는 D-132)
- **D-131** (2026-11-09) 검: Pro Sword and Shield Pack, 1D 속도 블렌드스페이스(무기 타입마다), 2001 Downward Slash, 피격 Sword And Shield Impact(`UpperBody`), 돌진은 필요 시 서버 이동 별도 설계 (일부: 2002는 D-132)
- **D-132** (2026-11-09) 이름 `OneHandSword`(`Anims/OneHandSword/`, `MX_OneHandSword_*`), 리타기터 Root Motion 끔 + `Pin IK Bones`, 제자리 = In Place 방식(수평 이동 성분을 pelvis·IK 본에서 뺌)
- **D-132** (2026-11-09) 2002 = `sword and shield attack`(도약 내려찍기), 달리기 샘플 1.31배, `BP_TestPlayer` 메시 오프셋은 `BP_MyPlayer`에서 복사(하드코딩 안 함)
- **D-126·D-134** (2026-11-09) 활은 평소 등에 멘 채 맨손 이동, 기본 공격은 바로 꺼내 쏨, 스킬 조준 중 시위 당긴 자세·느린 이동·크로스헤어 고정 4방향 조준 걷기
- **D-134** (2026-11-09) `WeaponStance` 0 등 / 1 손(활 든 이동) / 2 조준, 넣기 = `standing disarm bow` 상체 재생 + 손이 등에 닿는 시점 소켓 전환, 값은 WeaponTypeTable — 클라 연출 전용
- **D-139** (2026-11-10) 방패 착용 시 왼팔: `HasOffhandItem` → `UJMAnimInstance.LeftArmOverrideAlpha` 0/1(레이어 AnimBP 수정 없음)

## UI

- **D-038** (2026-10-28) 전투 HUD = `WBP_CombatHud`(`UCombatHudWidget`) + `WBP_SkillSlot`, 쿨타임은 덮개 + 남은 초, 값은 서버 패킷, 입력은 막지 않음(서버가 거부). `WBP_Info`는 C-3에서 삭제됨
- **D-039** (2026-10-28) 인게임 `BP_GameMode`의 부모 = `AIngameGameModeBase`, `HUDWidgetClass = WBP_CombatHud`
- **D-050** (2026-10-30) 시스템 메시지 = 공용 모달 `WBP_SystemMessage`(`UJMUIManager::ShowSystemMessage`, Notice/Waiting, FIFO), 입장 전 에셋 미리 로드
- **D-138** (2026-11-10) 스킬 아이콘 `Content/UI/Icons/Skill/T_Icon_Skill_<WeaponType>_<Slot>`, 원본 `SourceArt/Icons/`, VARCO 제작, Greystone·Sparrow 자산은 이펙트만 사용
- **D-148** (2026-11-16) HUD = HP 바·수치 + 내 이름·레벨(`ObjectInfo.name`·`level`) + EXP 바(보상 전까지 0) + 액티브 칸 5개(빈 칸/스킬/아이템 구조, 지금은 칸 N = 무기 스킬 슬롯 N, 숫자키 1~5, 아이템 넣기·서버 저장은 소비 아이템이 생길 때). 골드는 인벤토리에서. 경험치·레벨·골드 보상(A2.3b)은 마일스톤 2 뒤

## 도구·엔진·외부 서비스

- **D-069·D-107** (2026-11-04) VARCO 전 기능·사운드, Mixamo, Fab 무료 에셋 허용, Blender 등 DCC는 안 씀. VARCO 포인트는 예상 비용 승인 후, 출처는 `ExternalAssets.md`
- **D-089** (2026-11-05) 엔진 = UE 5.8 확정(되돌리지 않음, 5.6 유지·별도 브랜치 시험을 대체) (일부: U5~U8 순서·브랜치 처리는 완료, Mutable은 D-091)
- **D-090** (2026-11-05) 에디터 MCP = Epic 공식(`127.0.0.1:8000/mcp`), 임의 Python 실행 도구 없음, 프로젝트 도구는 `jm_mcp` 툴셋(인자 `X | None = None`) — 공식 서버에 인증 없음
- **D-141** (2026-11-12) FX 손질 도구 = C++ `UFxEditLibrary`(JMEditorTools) + `jm_kit.fx_fix` + MCP `fx_inspect`·`fx_apply`·`fx_trail_notify` — Python이 Cascade 이미터를 못 읽음

## 채택하지 않은 안

다시 제안하지 않도록 남깁니다(D-145, 2026-11-15 사용자 승인). `→` 왼쪽이 버린 안의 원본 행, 오른쪽이 지금 유효한 결정. `(추정)` = 원본에 이유가 없어 전후 행에서 끌어낸 것.

- **D-005·D-006·D-008 → D-063** (2026-10-17 → 11-04) 플레이어 = Paragon 영웅(Greystone·Sparrow) + 고유 무기, 무기 아이템 = 스탯 + 머티리얼 교체만 — 버린 이유: "완전히 일반적인 MMO 스타일" 요구, 모듈형을 프로토타입으로 검증, 막혔던 활·방패 분리는 무기를 정적 메시 소켓에 붙이면 해당 없음
- **D-007 → D-064** (2026-10-17 → 11-04) 스킬 = 영웅 종속(`SkillTable.HeroId`) — 버린 이유: 직업 없음·아무 무기나 장착으로 "무기가 영웅에 고정"이라는 전제가 사라짐 (추정)
- **D-005·D-007 → D-068** (2026-10-17 → 11-04) 영웅별 AnimBP(무기별 AnimLayer 폐기) — 버린 이유: 위와 같은 전제 소멸 (추정), 10-03 AnimLayer 방침(D-106) 부활
- **D-021 → D-022** (2026-10-20 → 10-21) 쿼터뷰 고정 + 커서 항상 표시 — 버린 이유: 쿼터뷰가 마음에 들지 않음, 서버는 방향·대상·지점만 검증해 영향 없음
- **D-074 → D-080** (2026-11-04) 아트 = 고사실(high-fidelity), 검은사막 계열 참고 — 버린 이유: 기준인 키트 얼굴·피부가 "만화풍까지는 아님" 수준의 중간 스타일
- **D-096 → D-097·D-098** (2026-11-06) 판금 투구 = 키트 머리 형상 + 무늬 칠하기(`kit_head`), 3/4 위 시점·사슬 포함·4천 폴리 직접 생성 — 버린 이유: 머리 모양을 그대로 따라가 어색, 휘지 않는 투구는 소켓 부착이 현업 방식, 저폴리 직접 생성은 돔이 무너짐(4차까지 실패)
- **D-077·D-079 → D-083 → D-085·D-086** (2026-11-04 → 11-05) 몸 = 직접 7부위 분할(`body_parts`) → 키트 부위 조립 + 통째 숨김 — 버린 이유: 분할 도구가 매니퀸 자세(161본)로 바인딩해 키트(89본)와 어긋남, 통째 숨김은 소매 안이 빔, 마스킹을 쓰면 부위 분할은 비용만 남음(몸 컴포넌트 6 → 1, 드로우콜 6 → 2)
- **D-011 → D-012 → D-013 → D-142** (2026-10-17 → 11-12) Paragon 사용 에셋을 영웅 폴더로 Move(Vault 재설치 복원 → 즉시 Move → 작업 끝에 일괄 Move) — 버린 이유: 재설치는 백업·재설치 단계가 번거로움, 즉시 Move는 필요 범위 미확정·경로 변경 잦음, 일괄 Move는 FX가 팩을 계속 참조(197개)해 팩 삭제를 선택
- **D-082 → D-084** (2026-11-04) 몸 피부 = 키트 만화 셰이더에서 `UseComic?`만 끈 `MI_Skin_*`(망점 제거) — 버린 이유: 기록 없음(MC1 재작업 때 삭제, 사용자가 키트 원본 머티리얼 선택)
- **D-025** (2026-10-27) 서 있을 때 상체 블렌드(이동 속도로 Blend Weight 제어) — 버린 이유: 서 있을 때 공격 모습이 어색하지 않음(당시 Paragon 모션 기준 — Mixamo 모션으로는 MC4.2 중간 테스트에서 재확인)
- **D-045·D-047 → D-090** (2026-10-30 → 11-05) 에디터 MCP = uemcp 0.1.0 — 버린 이유: ★0·기여자 1, PIE가 Simulate 모드뿐·`ue_stop_play` 오동작, 공식 MCP가 U5 시험 통과
- **D-125 → D-128** (2026-11-08 → 11-09) 모션 출처에 GASP·Lyra 포함 — 버린 이유: GASP는 맨손 이동만·Lyra는 총기만이라 검·활에 쓸 모션이 없음(조사), 사용자 결정
