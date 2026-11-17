# ROADMAP

포트폴리오 MMORPG(서버·클라이언트 구조 시연: 몬스터 사냥 → 아이템 드랍 → 유저 거래 → 동접 테스트)의 진행 순서입니다. 설계는 `DESIGN_*.md`, 지금 유효한 결정은 [DECISIONS_CURRENT.md](DECISIONS_CURRENT.md)(경위·번복은 원본 [DECISIONS.md](DECISIONS.md)), 시험 기록은 [TESTING.md](TESTING.md)에 둡니다.
순서는 마일스톤 3개입니다(2026-11-16 재설계, D-147): **1 — 한 바퀴**(사냥 → 드랍 → 줍기 → 장착 → 거래·경매가 처음부터 끝까지 동작) → **2 — 수치**(성능 개선·동접 5,000 부하) → **3 — 산출물**. 서버 기능은 최소 UI와 짝지어 만들고, 외형·애니메이션·이펙트 다듬기는 동결합니다.

## 상태 표기와 완료 기준
- 표기: `- [예정]`·`- [진행]`·`- [확인 대기]`·`- [완료]`·`- [보류]`·`- [폐기]` + ID 작업명 + (이유 또는 비고). 아이콘은 쓰지 않습니다.
- 한 번에 한 항목만 진행하고, 상태가 바뀌면 즉시 갱신합니다. 끝난 항목은 한 줄로 줄여 맨 아래 "완료 이력"으로 옮깁니다(상세는 `TESTING.md`·`DECISIONS.md`).
- 남은 항목마다 "완료:"를 둡니다. 확인 종류는 `Claude 테스트`(빌드·원시 소켓·LoadTest 등) / `사용자 PIE` / `사용자 승인`.
- 검증 게이트(D-147, D-019·D-146 완화): 서버 기능 항목은 각 항목의 완료 기준에 **더해** 오류 테스트(비정상 입력, 중간 접속 해제, 중복·상태 불일치 요청) + **200명 짧은 부하 램프**를 통과하고 `TESTING.md`에 기록해야 `[완료]`입니다. 1,000 → 5,000 램프는 항목마다 하지 않고 마일스톤 2(A5.4)에서 한 번에 합니다. 완료 기준에 "부하 테스트"라고만 적힌 것은 200명 램프를 뜻합니다. 설계 문서·UI·산출물 항목은 해당 없음.
- 콘텐츠 동결(D-147): 외형·애니메이션·이펙트는 "기능이 동작하면 통과"입니다. 어색한 점은 원인 조사 없이 백로그 "콘텐츠 다듬기"에 한 줄만 적습니다.

## 현재 위치 (2026-11-17)
- 지금: 2026-11-16 로드맵 재설계(D-147). P0·A2.3a·A2.4(사망·부활) 완료, A2.5 DB 저장 연결 완료(사용자 PIE 확인, 2026-11-17). A3.1 [DESIGN_ITEMS_TRADE.md](DESIGN_ITEMS_TRADE.md) 완료(D-152~D-159, 사용자 승인), A3.2 데이터 테이블·A2.7 중복 접속 차단·**A3.3 인벤토리**(D-160~D-165) 완료. **A6.1 인벤토리 UI**(+ MC4.4 장착·해제, 칸 위치 서버 저장 — D-166) 완료(사용자 PIE 확인). **A3.4 사망 시 드랍 롤**·**A3.5 줍기·버리기** 완료(Claude 테스트 — 소유권 거부·개방 포함, 2026-11-17). **A6.2 드랍 표시·줍기 입력·버리기 버튼** 완료(사용자 PIE 확인, 2026-11-17). **A3.6 소비 아이템 사용 + 액티브 칸** 완료(사용자 PIE 확인, 2026-11-17). **A4.1·A4.2 1:1 거래(서버)** 완료(Claude 테스트, 2026-11-17 — 사망 시 취소만 미확인). **A6.3a 거래 UI** 완료(사용자 PIE 확인, 2026-11-17). 2026-11-17 사용자 결정(D-167)으로 A4.3보다 먼저 **MN1 네발 몬스터 파일럿(늑대 1종, VARCO)** 진행 중, 그 다음이 **A4.3 Room 간 경매장**. 골드는 드랍으로 먼저 들어오고(D-152) 경험치·레벨 보상은 A2.3b로 마일스톤 2 뒤.
- 미뤄 둔 것: MC4.2 중간 테스트(2026-11-15~16 Claude가 일부 진행, 결과는 백로그 "콘텐츠 다듬기")·MC4.3·MC5.1·M3 Paragon Move·U8.
- 참고: 동시성 감사 F1~F10·P1 설계 주의점은 [AUDIT_2026-11-06_CONCURRENCY.md](AUDIT_2026-11-06_CONCURRENCY.md) — 코드가 바뀌었을 수 있으니 함수명으로 찾아 다시 확인. A1.3 병목 근거는 `TESTING.md` 2026-10-31. 기준선 재측정 방법·인자는 [PERF_BASELINE_2026-11-06.md](PERF_BASELINE_2026-11-06.md) §1.
- 브랜치: 개발은 `Jeonghoon` 하나, `main`은 Phase·마일스톤 끝에 병합(+태그)하는 안정판, 시험은 짧은 `exp/…` 브랜치(`DECISIONS.md` 2026-11-12). `content/dev`는 사용 종료.
- Claude가 못 보는 것: 에디터의 애니메이션·이펙트 모양 — SceneCapture PNG, 스크린샷, 블루프린트 노드 복사 텍스트(Ctrl+C)로 확인.
- 절차(UE 빌드·서버 실행·PIE 준비·프로토콜 재생성·VARCO·Mixamo)는 `.claude/skills` 참고. 봇 체형은 `--body male|female`.

## 마일스톤 1 — 한 바퀴 (플레이 흐름)
위에서부터 순서대로 합니다. 서버 기능 바로 뒤에 그 기능을 PIE로 볼 최소 UI를 둡니다. 큰 항목(A3 이후)은 차례가 되면 코드를 읽고 하위 단계로 따로 계획합니다. 완료된 A0·A1·A2.1~A2.2g·A2.6·R(C-1~C-7·U1)·S1·MC0~MC4.1은 완료 이력에.

### 1-2. 성장·저장

### 1-3. 아이템·드랍·인벤토리

### 1-3b. 몬스터 추가 (2026-11-17 사용자 결정, D-167 — A4.3보다 먼저)
- [확인 대기] MN1 네발 몬스터 파일럿(늑대 1종, VARCO, 550포인트): 컨셉 이미지 → 3D(`tPose` 0) → `quadruped` 리깅 → 동작 6개(대기 `cat_stand`·걷기 `dog_walk`·달리기 `dog_gallop_left_lead`·사망 `dog_death_front`·공격 후보 `dog_stretch`·`dog_body_shake`) → FBX 내려받기 → UE 5.8 반입(`jm_kit.monster`, `/Game/Character/Monster/Wolf`)까지 끝. 결과: 동작 결과 내려받기 됨, 상위 표시 동작 0포인트로 됨, 말·사슴 동작은 거부(개·고양이 동작만), UE 반입 됨(기록 `TESTING.md` 2026-11-17 MN1). 남은 것: 사용자가 에디터에서 `Anims/AS_Wolf_*` 재생 확인 + 공격 동작 결정(네발 공격 동작이 VARCO에 없음) — 통과하면 클라 `template_id` → 외형 매핑과 나머지 종을 하위 항목으로 계획

### 1-4. 거래·경매장
- [예정] A4.3 Room 간 경매장: 전역 `AuctionService`(자체 JobQueue + DB 트랜잭션), 등록 시 에스크로, 구매 시 골드·아이템 한 트랜잭션, 만료 반환. 즉시 구매만·수수료 없음, 대금·구매품·반환은 수령함(`Player_Mail`)에서 수령(D-159). 완료: 다른 Room 두 유저의 등록→구매→양쪽 수령 성공, 동시 구매 시 한 명만 성공, 재시작 후에도 등록·수령함 유지
- [예정] A6.3b 경매장 UI(기능 우선, 결과 안내는 U1 시스템 메시지 창). 완료: 경매 등록·구매, 거부·실패 안내가 시스템 메시지 창에 표시(사용자 PIE)

### 1-5. 마감
- [예정] MS1 마감: 2인 PIE로 사냥 → 드랍 → 줍기 → 장착 → 1:1 거래 → 경매 등록·구매를 처음부터 끝까지 시연하고 `main` 병합(+태그). 완료: 시연 통과(사용자 PIE) + 병합 사용자 승인

---

## 마일스톤 2 — 수치 (성능·부하, 동접 5,000 확정 목표)
매번 A1.3 매트릭스로 재측정해 `TESTING.md`에 전/후 기록합니다(같은 PC 측정, 분산 2,500명 이상은 한계로 기록). 시작할 때 기준선을 다시 잽니다(마일스톤 1이 서버 코드를 바꿈).

### 2-1. 송신 개선
- [예정] P1a 송신 지표 먼저: `NetStats`에 `WSASend` 호출 수/s와 호출당 메시지 수(WSABUF 평균) 추가, `metrics.csv` 재시작 헤더 중복 수정(+ 실행 ID 열), 측정 CSV·서버 로그 보관 규칙. 수정 전 값 기록(핫존 100 WSABUF 평균 ≈ 1 예상). 완료: 새 지표·실행 ID 열이 `metrics.csv`에 나오고 재시작 시 헤더 중복 없음, 측정 CSV·서버 로그 보관 규칙을 `TESTING.md`에 적고 수정 전 값(핫존 100 WSABUF 평균) 기록(Claude 테스트)
- [예정] P1b 송신 묶기: 방 작업 안에서는 `Session::Send(pushOnly)`로 큐에만 넣고 넣은 세션을 모아 두었다가 세션당 `WSASend` 1회. **작업 묶음 끝만이 아니라 잡 N개(16~64) 또는 1~2ms마다 보내기**(`ExecuteJobs` 독점 — 감사 F5 — 도 함께 해결), 보내기는 `_isProcessing` 해제 전, 모은 세션 목록은 RAII로 비움(감사 §7). 프로토콜 변경 없음. `WorkerThreads`는 기본 유지(늘리면 악화). 완료: 같은 매트릭스(분산 1,000 / 핫존 25·50·100 / 로그인 1,000, 각 2회 이상)로 기존 합격선(D-057) 유지 + 목표(`PERF_BASELINE_2026-11-06.md` §5) 핫존 100 핑 p99 ≤ 50·틱 지연 p99 ≤ 20·서버 CPU 코어1 환산 ≤ 25%, 분산 1,000 핑 p99 ≤ 40, 핫존 100 WSABUF 평균 ≥ 10, 핫존 100 워커 1(P1 전) 대비 비교를 `TESTING.md`에 기록 + chaos 램프 후 세션 잔류 0(Claude 테스트) + 구현 후 동시성 리뷰(최상위 모델 high, 감사 §7 점검)

### 2-2. 본 부하 테스트 (동접 1,000 → 2,500 → 5,000)
- [예정] A5.0 연결 정리(2026-11-06 감사 F4·F3): `Disconnect`에서 `CancelIoEx`/소켓 닫기, 유휴 세션 타임아웃, `_currPlayer` 동기화(`atomic<shared_ptr>` 또는 `Session::_lock`). 종료 시 문제 F6~F10은 정리 단계 후보. 완료: 수신을 멈춘 클라이언트를 서버가 끊은 뒤 핸들 수·송신 큐 감소, 유휴 타임아웃 후 세션·플레이어 0, 로그인+입장 연속 500회와 chaos 램프 강제 종료 10회 후 잔류·크래시 0(Claude 테스트) + `_currPlayer` 코드 리뷰(재현 절차는 감사 문서 F3·F4)
- [예정] A5.1 봇 `is_dummy` 로그인 우회 제거(별도 계정 대량 생성 또는 서버 설정 플래그로 제한). 완료: 기본 설정에서 `is_dummy` 우회 로그인 거부 + 정한 방식으로 봇 1,000명 로그인 램프 통과(Claude 테스트) — 2026-11-16 메모(A2.5 시험): 개별 계정 봇은 전원 PlayerStart에서 시작하는데 직선 이동이 내비메시 검증에 막혀 47m 밖 몬스터에 닿지 못함(피해 패킷 0). 더미 봇처럼 흩어 배치하는 방법을 함께 정해야 hunt 프로필이 동작. 2026-11-16 메모(A3.4 시험): 원시 소켓 클라이언트는 z를 바닥 높이에 맞춰 낮춰 보내자 PlayerStart에서 몬스터 6m 앞까지 갔음 — 서버 `CanMove`가 보낸 z 위아래 3m의 내비메시만 찾으므로, 봇이 z를 시작 높이(290)로 고정해 보내는 것이 막히는 원인일 수 있음(미확인). 2026-11-17 메모(A3.5 시험, 코드 확인): `NavmeshManager::CanMove`는 **서버에 저장된 내 위치(마지막으로 받아들인 z)** 위아래 3m에서 시작 폴리곤을 찾고 목표까지는 수평 레이캐스트만 함 — z를 290으로 고정한 채 바닥이 3m 넘게 내려간 곳에 서면 그 뒤 모든 이동이 거부됨(거리 0 이동은 지형 검사를 건너뛰어 z만 고칠 수 있음). 봇은 이동 패킷의 z를 바닥 높이(몬스터·드랍의 z + 약 117cm)에 맞춰 낮춰 보내야 하고, 그래도 직선 경로가 내비메시 경계에 막히는 구간이 있어 우회가 필요함
- [예정] A5.2 봇 행동 프로파일: 이동, 몬스터 공격, 줍기, 거래/경매. 완료: 프로파일(이동·사냥·줍기·거래·경매)별 짧은 램프에서 실패·`logDrop` 0(Claude 테스트)
- [예정] A5.3 클라이언트 지표: RTT p50/p95/p99, 접속 성공/실패, packets/s, CPU/메모리 CSV. 완료: 램프 1회의 CSV 생성(Claude 테스트)
- [예정] A5.4 단계 램프(1,000 → 2,500 → 5,000), 병목 발견→수정→재측정 기록. 시나리오: 다수의 작은 Room vs 한 Room 밀집. 필요 시 `TCP_NODELAY`, 다중 AcceptEx, 레이트 리밋, 송신 큐 상한. 완료: 단계별 결과와 개선 서사가 `TESTING.md`에 기록(Claude 테스트) + 사용자 승인

### 2-3. 병목으로 드러나면 하는 것
A5.4 램프에서 병목으로 확인된 것만 합니다(순서는 측정 결과로). 5,000 목표를 채우는 데 필요 없으면 `[폐기]`로 닫습니다.
- [예정] P2 이동 패킷 묶기: 틱마다 수신자별로 그 틱에 움직인 대상들의 위치를 패킷 하나에(새 패킷, `.proto` → `GanPackets.bat` → 서버·UE 핸들러·봇). 한 틱 안의 즉시 송신이 합쳐지는 영향과 A2(속도 크기 변화 시 송신)·`RemoteCatchUpSpeedMul` 재조정을 여기서 판단(`DECISIONS.md` 2026-11-07). 완료: 매트릭스 전/후 측정 + 클라이언트 보간 확인(사용자 PIE) + MC4.1b 회귀 측정 — 봇 `--profile turn --turn-send --turn-interval 550 --move-interval 200` 3명 60초 + 관찰자 `NetMoveStats`가 MC4.1b 결과(최대 1.23배, 1.2배 초과 0.02%, 오차 16/83cm)보다 나빠지지 않을 것
- [예정] P3 거리별 전송 빈도: 가까운 수신자에게는 매 이동, 먼 수신자(예: 25m 밖)에게는 일부만(멈춤·방향 전환은 항상). 거리·비율은 `WorldTable`. 완료: 매트릭스 전/후 측정 + 먼 수신자 기준 MC4.1b turn 시나리오 회귀 측정(간격이 길어진 대상의 외삽·따라잡기 확인)
- [예정] P4 몬스터 깨우기 범위 분리: `MonsterTable` 깨우기 반경(예: 20m), 시야(50m) 안이라도 그 밖이면 보이기만 하고 AI는 잠 — 행동트리 개선 때 재검토. 완료: 매트릭스 전/후 깨어 있는 몬스터 수·틱 본문 p99 기록 + 원시 소켓으로 깨우기 반경 밖 몬스터의 이동 패킷 0·반경 안 진입 시 깨어남 확인(Claude 테스트) + 반경 경계 출입 시 동작(사용자 PIE)
- [예정] A4.4 DB 작업은 `DatabaseWorker` 비동기 큐 + 결과를 Room/서비스 Job으로 복귀. 완료: DB 응답을 인위로 늦춘 부하 중에도 틱 지연 p99 ≤ 20ms(D-057) 유지, DB 결과 도착 전 접속 해제 시 결과 폐기(Claude 테스트) — 2026-11-16 메모: 로그인·가입·장비·진행 상황 저장은 이미 `DatabaseWorker` 큐 + 로비/Room 잡 복귀 구조(MC3.3·A2.5). 남은 것은 새 DB 경로(인벤토리·거래·경매)를 같은 구조로 두었는지 점검 + DB 지연 주입 시험

---

## 마일스톤 2 뒤 — 성장
- [예정] A2.3b 서버+클라이언트: 경험치/레벨 보상 패킷, HUD EXP 바·레벨 갱신(자리는 A2.3a). 골드는 드랍으로 A3.4에서 먼저 들어옴(D-152) — 처치 즉시 골드 보상을 따로 둘지는 이때 판단. 레벨에 따른 스탯 반영도 여기서(A2.2a 이월 — 피해 공식 자체는 MC3.1b에서 구현). (2026-11-16 A2.3에서 분리해 마일스톤 2 뒤로, D-148 — 다음 레벨 EXP 열 등 테이블은 이때 정함) 완료: 몬스터 처치 시 서버 계산대로 경험치·골드·레벨업 반영 — 원시 소켓·오류·부하 테스트(Claude 테스트) + HUD 표시(사용자 PIE)

---

## 마일스톤 3 — 산출물
- [예정] A7.1 아키텍처 다이어그램. 완료: 사용자 승인
- [예정] A7.2 벤치마크 보고서(환경, 램프 프로파일, 지연/CPU/메모리 그래프, 병목 개선 서사). 완료: 사용자 승인
- [예정] A7.3 루트 README 재작성(서버 권위 지점, SQL Server 설정, 부하 테스트 방법, 알려진 한계). 완료: 사용자 승인
- [예정] MC4.5 `Fx` 연결: 스킬별 `SkillTable.csv` `Fx` 열 연결(기본 공격 2001/3001 타격·투사체, 버프·텔레그래프는 기능이 생길 때). 완료: 2001 타격·3001 투사체 이펙트가 `SkillTable.csv` `Fx` 열로 재생(사용자 PIE) — 2026-11-16: 연결은 하되 어떤 이펙트를 쓸지는 이때 정함(지금은 쓸 만한 것을 모아 둔 상태, `FX_CATALOG.md`)
- [예정] A7.4 선택: 시연 영상. 완료: 사용자 승인

범위 제외: 다중 맵·포털, 3명 이상 영웅·콤보·스킬트리, 비밀번호 해시·토큰(알려진 한계로 문서화), 다중 머신 확장, Prometheus/Grafana, CI, UI 연출.

---

## 백로그 (순서 미정)
차례가 정해지면 위 트랙의 항목으로 올립니다. 근거·위치는 괄호 안.

### 콘텐츠 다듬기 (2026-11-16 동결, D-147)
마일스톤 1~2 동안 하지 않습니다. 영상 준비(A7.4) 때 필요한 것만 올립니다.
- [보류] MC4.2 `ABP_PlayerBase` + `ALI_Weapon` + 무기 타입별 레이어·몽타주, `ExportSkillTiming.bat` (+ 장갑 손가락 맞춤 — 무기 쥐기와 함께, `DECISIONS.md` 2026-11-05). 2026-11-08 하위 단계로 분할(`DECISIONS.md`). a·b·c·d-1 완료, 나머지는 중간 테스트 대기. 중간 테스트에서 함께 볼 것: 제자리에 서서 공격할 때(맨손·검·활) 상체·하체가 어색하지 않은지 — 어색하면 이동 속도로 상체 Blend Weight 제어를 재검토(D-025는 Paragon 모션 기준 판단, D-145). 완료: 하위 항목 모두 완료 + MC4.2 중간 테스트(사용자 PIE), 제자리 공격 판정 결과를 `TESTING.md`에 기록 (2026-11-16 로드맵 재설계 — 중간 테스트를 백로그로)
  - [확인 대기] MC4.2d 활 + 스킬 조준 동기화 — 2026-11-09 두 단계로 분할(d-1 완료). 완료: 하위 항목(d-1·d-2) 모두 완료
    - [확인 대기] MC4.2d-2 활 애니메이션·등에 메기: `ABP_Layer_Bow`(조준 = Longbow 팩 aim walk + 당김 상체), `AM_Bow_Attack`·`AM_Bow_Skill1`, 활 등 메기(MC4.3 장치 먼저 적용) (2026-11-10 구현, 확인은 중간 테스트 — 사용자 결정). 볼 것: 손에 쥔 활 위치·각도(손 앞에 뜨지 않음), 조준 걷기 재생 배율(×1.5~2.3) 또는 `AimMoveSpeedMul` 0.25, 기본 공격 1.73초, 등 위치, **시위 움직임**(권장 A = 활 메시의 시위를 지우고 활 끝 → 오른손 V자 줄을 따로 그림, B = 활 스켈레탈 변환·시위 뼈, C = 정적 유지 — 테스트 때 결정). 확인 후 MC4.2d-1 임시 화면 표시 `[SkillAim]`(`AJMCharacterBase::SetSkillAiming`의 `AddOnScreenDebugMessage`) 삭제(2026-11-09 사용자 결정). 완료: 위 볼 것을 중간 테스트에서 사용자 PIE로 확인, 시위 방식 결정, `[SkillAim]` 임시 표시 삭제
  - [확인 대기] MC4.2e 장갑 손가락 맞춤 + 팔을 어깨 높이로 든 자세(활 당김·검 스킬)의 천·판금 상의 어깨·소매 뭉침 + Paragon 애니메이션 참조 0 + 문서 정리 (2026-11-10 구현, 확인은 중간 테스트 — `TESTING.md` 2026-11-10). 볼 것: 장갑 손목 가장자리(톱니 제거)·활 쥔 손(+2cm 손가락 쪽)·검 쥔 손, 스킬1·활 당김 때 상의 어깨 접힘(가중치는 그대로 — 키트 상의와 같은 자리, 거슬리면 보정 모프를 후속으로). 완료: 위 볼 것을 중간 테스트에서 사용자 PIE로 확인
  - [확인 대기] MC4.2f 방패(선택 장비, `OFFHAND` 슬롯·서버 한손검 전용 검증·무기 변경 시 자동 해제, 7101 천·7301 판금) (2026-11-10 구현, 확인은 중간 테스트 — `DECISIONS.md`·`TESTING.md` 2026-11-10). 볼 것: 대기·공격·스킬1 때 방패 위치·각도(손등 방향 앞면, 손바닥에서 4cm), 방패 착용 시 왼팔 원본·미착용 시 맨손 덮기, 활·맨손으로 바꿀 때 방패 사라짐(서버가 해제 먼저 방송), 판금 방패 색이 어두운지. 완료: 방패 착용·미착용 검 동작, 활로 바꿀 때 방패 해제 PIE 확인(사용자 PIE)
  - 2026-11-15~16 Claude 사전 확인 결과(TestLevel 자동 스크립트·정지 화면, `TESTING.md` 기록 전): ① 남성 + 천 방패 검 대기에서 칼끝이 방패를 뚫음 → 천 방패 `gap_cm` 4 → 9로 수정·PIE 확인(`items.json`·`ItemTable.csv` 7101·`DT_ItemTable`, 커밋 전) ② 한손검 궤적이 실제 화면에서 안 보임 — 수명 0.125초·주황·가산 머티리얼, `P_Trail_Blunt`로 바꿔도 같음(원인 미확정: 수명·밝은 배경·동적 파라미터 후보) ③ `P_Life_Death`의 검은 조각은 원본 디자인(재, 불투명 마스크) — A2.4에서 사망 처리에 붙여 판정 ④ `P_Debuff_Burn`은 불씨 하나라 약함(강도 결정 필요) ⑤ 판단 필요: 제자리 공격 상·하체(검 중간·맨손 큼·활 자연스러움), 시위(A안 권장, 화살도 손에 없음), 검 스킬1 도약 높이 ⑥ 문제 없음: 방패 위치·각도·판금 색, 활 쥔 손, 부활·냉기 시전, 어깨 ⑦ 미확인: 조준 걷기 속도감·기본 공격 길이감, 활 등에 멘 모습, 장갑 손목 근접, 서버 PIE 항목 전부. 도구: `ATestPlayer` 자동 스크립트(`Saved/TestAuto.txt`)·콘솔 `TestFx`(커밋 전).
- [보류] MC4.3 비전투 시 무기 등에 메기 (활은 MC4.2d-2에서 먼저 적용 — 검에 같은 장치 확장). 완료: 검·활이 공격·조준 시 손, `SheatheDelaySec` 뒤 넣기 동작 → 등, 피격으로는 꺼내지 않음, 처음 보이는 원격 캐릭터는 등(사용자 PIE 2인) (2026-11-16 로드맵 재설계 — 콘텐츠 동결)
- [보류] MC4.5 PIE 확인: 사망·부활·화상·냉기 시전의 본 이미터와 한손검 공격 궤적(`AM_OneHandSword_Attack` Trail 0~1.5초) 확인 후 구간·폭 조정(중간 테스트). 완료: 사망·부활·화상·냉기 시전 본 이미터가 매니퀸 몸에서 재생되고 한손검 궤적 구간·폭 값 확정(사용자 PIE) (2026-11-16 로드맵 재설계 — 콘텐츠 동결, 사전 확인 결과는 위 MC4.2 메모 ②~④)
- [보류] MC5.1 오류·부하 테스트, 클라이언트 성능 측정, SceneCapture 시트 승인(`TESTING.md`), `main` 병합(사용자 승인). 클라이언트 성능은 20명(남녀 각 10) 측정 → 기준 초과 시 `FSkeletalMeshMerge` 시험(`DECISIONS.md` 2026-11-05). 완료: 측정 전에 클라이언트 성능 기준 수치 결정(사용자 승인) + 테스트·측정 결과 `TESTING.md` 기록(Claude 테스트) + 시트·병합 사용자 승인 (2026-11-16 로드맵 재설계 — `main` 병합은 MS1 마감에서, 클라이언트 성능 측정·시트 승인은 영상 준비 때 판단)
- [보류] M3 Paragon 에셋 최종 Move: 사용 중인 Paragon 에셋을 새 폴더로 정리해 커밋(재다운로드 파일은 커밋하지 않음) — 절차는 아래. 완료: `ParagonRampage` 폴더 없이 PIE에서 몬스터 표시·공격·사망(사용자 PIE) + 끊긴 참조 0(Claude 테스트) (2026-11-16 로드맵 재설계 — 콘텐츠 동결)

**Paragon 에셋 최종 Move (2026-10-17·10-19 방침)** — 재다운로드한 Paragon 팩은 커밋하지 않고(`.gitignore`), 쓰는 에셋과 의존 에셋만 `Content/Character/<이름>/`으로 **Move**해 커밋합니다. Greystone·Sparrow는 2026-11-12 삭제 완료(2단계, 완료 이력). 남은 것은 Rampage(`BP_Npc`가 `Character/Rampage/`의 `AM_Rampage_Attack/Hit/Death`·`ABP_Rampage`를 쓰고, 메시·스켈레톤·머티리얼·텍스처·시퀀스는 아직 팩 원본)와 `Characters/Global`. 한 단계씩 사용자에게 묻고 진행합니다.
1. Rampage 폴더와 `Characters/Global`을 Move → Fix Up Redirectors → `BP_Npc` 확인 → Save All → `ParagonRampage` 삭제(`ParagonProps`는 `ParagonSample` 맵 정리 때 — 백로그) → PIE 확인(Reference Viewer로 의존 확인)
2. (완료 — Greystone·Sparrow 삭제, 완료 이력 "M3 Paragon 에셋 최종 Move 2단계")
3. 끝날 때마다 `Content/Character/**`와 소스·문서만 체크포인트 커밋. 원본 `Paragon*` 폴더의 재다운로드 변경분은 커밋하지 않음(복원 또는 제외 방식은 그때 결정)

- [보류] U8 Mutable 시험(남녀) (2026-11-05 보류, `DECISIONS.md` — 재검토 조건: Mutable Beta 해제 또는 그래프 공개 API): 천 세트 7개로 Customizable Object, 지금 방식과 틈·관통, 드로우콜, 장비 교체 시간, 메모리를 측정 비교 → 채택 시 `DESIGN_CHARACTER_EQUIPMENT.md` 개정·`body_mask` 대체. 완료: 비교 측정을 `TESTING.md`에 기록(Claude 테스트) + 채택 여부 사용자 승인

### 그 밖
- 사망 표현을 AnimBP 상태(`bIsDead`)로 전환: 지금은 사망 몽타주 + 끝 자세에서 멈춤, 부활 때 몽타주 해제(A2.4, D-149 — 콘텐츠 동결로 미룸). 사망·부활 이펙트(`P_Life_Death`·`P_Life_Resurrect`), 원격 플레이어 머리 위 HP도 같은 때
- 죽은 플레이어만 시야에 있어도 몬스터가 잠들지 않음(`Monster::SleepIfNoPlayer`가 `_viewList`가 비었는지만 봄) — P4 몬스터 깨우기 범위 분리 때 함께 (A2.4 판독, 2026-11-16)
- 서버를 창 닫기·강제 종료로 끄면 마지막 주기 저장 이후의 진행 상황(최대 `PlayerSaveIntervalSec`)이 사라짐 — 종료 시 전원 저장은 종료 처리(감사 F6~F10) 때 함께 (A2.5, 2026-11-16)
- 200명 램프의 틱 지연 p99가 구간에 따라 30~80ms로 튐(틱 본문 p99는 17ms 이하, 변경 전 서버도 같음 — 2026-11-16 A2.4 A/B). 아래 "유휴 상태 틱 지연" 항목과 같은 원인인지 마일스톤 2 기준선 재측정 때 확인
- 장착 지연 재측정(A3.3, D-163): 200명 램프(개별 계정 봇이 PlayerStart 한 곳에 몰림)에서 장착 요청 → 적용 p99가 75~230ms로 기준 50ms 초과. DB 실행은 p99 22ms 이하이고 같은 구간 틱 지연 p99와 같은 값이라 원인은 Room 잡 대기 — P1b(송신 묶기)와 A5.1(개별 계정 봇 흩어 배치) 뒤 같은 시나리오로 다시 잼 (`TESTING.md` 2026-11-16 A3.3)
- DB 연결 실패를 시작 때 못 잡음: `DBConnection::Connect`의 반환식 `(ret == SQL_SUCCESS || SQL_SUCCESS_WITH_INFO)`가 항상 참(`ServerCore/DB/DBConnection.cpp`), 재연결도 없음 — 첫 연결만 스키마 버전 확인으로 걸러짐. A4.4 DB 지연·장애 시험 때 함께 (2026-11-16 A3.3 리뷰)
- 스킬 판정 범위 재측정: 2001(반경 190·각도 180)·2002(각도 360) 수치의 측정 근거였던 Greystone 모션이 Downward Slash 등으로 바뀜 → 새 모션 기준으로 다시 재서 `SkillTable.csv` 확인 (DECISIONS D-034, 2026-11-12 W5)
- 로딩 화면 이미지: 사용자가 이미지를 구해 오면 `WBP_SystemMessage` 대기(Waiting) 배경에 적용 (수동 작업 기록, 2026-11-06 예정이었음)
- 연결 끊김 화면 처리(메시지·로그인 복귀): U1 시스템 메시지 창(`UJMUIManager::ShowSystemMessage`) 사용. 서버가 입장을 거부하거나 입장 중 끊기면 "방에 입장하는 중..." 창이 닫히지 않는 문제도 함께 (TODO, M0.5-1)
- 자체 제작 맵으로 교체 후 `ParagonSample` 맵·`KiteDemo`·`StarterContent`·`_Core/Materials·Environment` 제외·삭제 — 지금 `NetworkManager.cpp`가 인게임 레벨 이름을 하드코딩(`SwitchGameMapLevel(FName("ParagonSample"))`) (M0.5 이월, `DECISIONS.md` 2026-10-21)
- 하드코딩 → 데이터: 서버 주소/포트, 인게임 맵 이름, 캡슐/보간/카메라/이동 임계값, UI 위젯 이름·크기(`MovementTable`/`UITable`) (TODO 2026-10-20)
- 몬스터 데이터: `template_id` 기반 메시/AnimBP 로드(`DT_CharacterAsset`은 2026-11-10 삭제 — 필요하면 몬스터 전용 테이블을 새로 설계) (TODO)
- 렌더 설정(레이트레이싱 등) MMO 클라이언트 기준 재검토, 안드로이드 토큰 저장소 노출 확인 (TODO)
- `ProtobufCore/Include`(686개, 테스트·타 언어 생성기 포함)를 헤더만 남기고 `ThirdParty`로 정리 (TODO)
- 플레이어 ID 충돌: 플레이어 id = 세션 번호(접속마다 +1), 몬스터 id는 100001부터 → 재시작 없이 누적 접속 10만 회를 넘으면 겹침(`Listener.h`, `Room.h`). 오브젝트 id 발급 일원화 (TODO 2026-10-28 QA)
- 유휴 상태에서도 100ms 틱이 평균 약 10ms(최대 20~30ms) 늦음: Windows 타이머 해상도 + 다음 틱을 실행 시각 기준으로 예약. `timeBeginPeriod(1)` 또는 예정 시각 기준 예약 검토 (TODO 2026-10-28 QA)
- 빈 방이 삭제되지 않음(`RoomManager::Remove(room)` 비어 있음): 부하 테스트 후 방·몬스터가 남음(메모리 46MB, CPU 영향 미미) (TODO 2026-10-28 QA)
- 죽은 몬스터가 같은 피해 처리 안에서 `HIT`/`TRACE` 상태로 바뀌어 이동 패킷을 한 번 더 보냄(`Monster::OnDamaged`) (TODO 2026-10-28 QA)
- DB: 50자를 넘는 ID/비밀번호의 `MultiByteToWideChar` 버퍼 처리(종료 문자 보장 없음, `DatabaseWorker.cpp`), 평문 비밀번호 — 보안 단계(`is_dummy`는 A5.1) (TODO 2026-10-28 QA)
- 서버 빌드 형 변환 경고 4건 재확인 (TODO 2026-10-28 QA)
- 클라이언트 송신 스레드 1ms 폴링(`NetworkWorker.cpp`의 `FPlatformProcess::Sleep(0.001f)`) (TODO 2026-10-28 QA)
- `ExportSkillTiming.bat` 확인: 에디터를 닫을 때 돌려 SkillTable 1001이 "변경 없음"인지(MC4.2a 때 같은 규칙으로 CSV를 직접 씀) (MC4.2a, `TESTING.md` 2026-11-08)
- A0.4 재확인: `dt` 클램프와 맵 밖 좌표(`GameMap` 셀 클램프)는 재현 수단이 없어 코드 리뷰만 — 하니스로 재확인 (A0.4, `TESTING.md` 2026-10-20)

---

## 완료 이력
형식: `- [완료] ID 제목 (날짜, 근거 문서)`. 상세는 `TESTING.md`(T)·`DECISIONS.md`(D).

### 마일스톤 1 — 한 바퀴
- [완료] P0 유령 플레이어 수정 — 수신 묶음 처리 중 끊기면 남은 패킷 버림, 입장 잡에서 세션 연결 확인, 중복 `SC_ADD_OBJECT` 제거 (2026-11-16, T)
- [완료] A2.3a HUD 정리 — 이름·레벨(`ObjectInfo.name`·`level`), 빈 EXP 바, 액티브 칸 5개(빈 칸/스킬/아이템 구조, 숫자키 1~5) (2026-11-16, D·T)
- [완료] A2.4 플레이어 사망·부활 — 요청형 부활(`CS_REVIVE`/`SC_REVIVE`, `WorldTable` `ReviveDelaySec`·`ReviveHpPercent`), 사망 중 이동·공격 차단, 부활 창 `WBP_Revive`(MCP로 제작) (2026-11-16, D-149·T)
- [완료] A3.1 아이템·거래 설계 문서 `DESIGN_ITEMS_TRADE.md` — 아이템 인스턴스(`Player_Item`)·인벤토리·드랍·소비 아이템·중복 접속·1:1 거래·경매(수령함)·패킷 번호, 사용자 승인 (2026-11-16, D-152~D-159)
- [완료] A3.2 데이터 테이블 — `DropTable.csv`(12행)·`EconomyTable.csv` + 로더(`GetDrops`·`RollDrops`·`GetEconomy`), `ItemTable` 열 5개와 HP 물약 8001(임시 아이콘), 시험용 `--roll-drops <kills>`, UE `FItemTableRow`·`EItemUseEffect`·`DT_ItemTable` 재임포트, `item_table.export`가 `Slot` `None` 행 건너뜀 (2026-11-16, T)
- [완료] A6.1 인벤토리 UI + MC4.4 장착·해제 + 인벤토리 칸 위치 — 한 창 `WBP_Inventory`(I 키, 장착 7칸 + 칸 격자 + 골드, 우클릭 장착·해제, 끌어다 놓기), `UItemSlotWidget`·`UInventoryWidget`, 칸 위치는 서버 저장(`Player_Item.BagSlot` 마이그레이션 0005, `CS_MOVE_ITEM`, 장착 교체는 옛 장비가 새 장비의 칸으로), 봇 `--slot-interval`. `WBP_Equipment`는 만들지 않음. 사용자 PIE 확인 완료 (2026-11-16, D-166·T)
- [완료] A3.3 인벤토리 — `Player_Item`(마이그레이션 0004, `Player_Equipped` 이전·삭제), 서버 `Inventory`·아이템 작업 공통 경로(`itemOp`: Room 검증 → DB 한 트랜잭션 → 성공 뒤 반영), 장착을 `item_db_id` 기준 + 보유·`RequiredLevel`·빈 칸 검사로 전환(장비 스냅숏 저장 폐지), 골드 증감 방식, 개발 지급 `CS_DEV_GIVE`(`[Debug] DevCommands`)·콘솔 `Give`, `SC_INVENTORY`·`SC_INVENTORY_UPDATE`·`SC_ITEM_RESULT`, 클라 `UJMInventoryManager`(빌드만, 화면 확인은 A6.1 때). 장착 지연 p99는 200명 램프에서 기준 50ms 초과(원인 = Room 잡 대기, 기록만 — 백로그) (2026-11-16, D-160~D-165·T)
- [완료] A6.3a 거래 UI — `UJMTradeManager`(거래 상태 사본·요청·안내), `UTradeWidget`/`WBP_Trade`(요청 화면 + 거래 화면), T 키 `IA_Trade` → `AJMMyPlayer::OnTradeInput`, 올리기 = 거래 중 인벤토리 우클릭·끌어다 놓기, 수량·골드 입력, `BP_GameMode.TradeWidgetClass`, 창 위젯 표시 `SelfHitTestInvisible`(`UJMUIManager`) (2026-11-17, 사용자 PIE 확인)
- [완료] A3.6 소비 아이템 사용 + 액티브 칸 — `CS_USE_ITEM`(1015)·`CS_SET_QUICKSLOT`(1016)·`SC_USE_ITEM`(2019), `SC_INVENTORY_UPDATE.quick_slots`, `Player_QuickSlot`(마이그레이션 0006), 서버 `Room::PlayerUseItem`/`PlayerSetQuickSlot`, 클라 액티브 칸 아이템 표시·수량·재사용, 숫자키 사용, 끌어다 놓아 올리기·우클릭 비우기, 인벤토리 우클릭 사용 (2026-11-17, D-154·T, 사용자 PIE 확인)
- [완료] A2.5 DB 저장 연결 — 스키마 버전 관리(`Schema_Version`·`migrate.ps1`·서버 시작 검사), SQL 한 파일(`DbQueries.h`), 로그인 때 DB 레벨·HP 반영, 진행 상황 저장(값이 달라졌을 때만, 주기 60초 + 퇴장) (2026-11-16 구현, 2026-11-17 사용자 PIE 확인, D-150·D-151·T)
- [완료] A4.1·A4.2 1:1 거래(서버) — 패킷 CS 1018~1023·SC 2020~2023, `TradeResult`·`TradePhase`, Room의 `TradeSession`(요청 → 수락 → 제시·잠금 → 양쪽 확정 → 한 트랜잭션 교환), 자동 취소(퇴장·거리·시간 초과·사망), 올린 행의 장착·버리기·사용 거부(`IN_TRADE`), `ItemOpKind::Trade`(`DbQueries::BuildItemTrade`), `Trade_Log`(마이그레이션 0007, `DB_SCHEMA_VERSION` 7), 지표 `trades`. 100회 반복에서 합계 보존·로그 행 수 일치. 사망 시 취소는 원시 소켓으로 확인하지 못함(MS1 시연 때 확인). 클라이언트 UI는 A6.3a (2026-11-17, T)
- [완료] A6.2 드랍 표시·줍기 입력·버리기 버튼 — `ADropItemActor`/`BP_DropItem`(임시 모양: 아이템 = 파란 상자, 골드 = 금색 원판), `UJMObjectManager::Drops`, F 키 `IA_Interact` → `CS_PICKUP_ITEM`, HUD 안내 `TxtInteract`, `WBP_Inventory` `BtnDrop` → `CS_DROP_ITEM` (2026-11-17, 사용자 PIE — "일단은 되는 것 같다")
- [완료] A3.5 줍기·버리기 — `CS_PICKUP_ITEM`(1013)·`CS_DROP_ITEM`(1014), 서버 `Room::PlayerPickupItem`/`PlayerDropItem`, `ItemOpKind::Pickup`·`Discard`(고정 쿼리 `ItemPickup`·`ItemDiscard`), 줍는 중 표시(`DropItem::picking`)로 경쟁 시 한 명만 획득, 버리면 발밑 80cm 안에 소유권 없는 드랍, 지표 `pickup(picked/discarded)`. 클라이언트 입력·표시는 A6.2 (2026-11-17, T)
- [완료] A3.4 사망 시 드랍 롤 — `ObjectInfo.item_count`(12), 서버 `DropItem`(`OBJECT_TYPE_ITEM`, `Room::_drops`·id 1,000,000,000~), `Monster::OnDead` → `Room::SpawnMonsterDrops`(막타 소유권 기록, 더미 봇·공격자 없음은 소유권 없음), 드랍 전용 AOI(`GameMap::_dropAoi` + `Player::dropView`, `UpdateDropView`), 소멸(`ExpireDrop`)·방당 상한, 지표 `drops(...)`, 클라는 ITEM 스폰 무시(표시는 A6.2). 소유권 거부·개방은 A3.5에서 확인 (2026-11-16, T)
- [완료] A2.7 중복 접속 차단 — `AuthLobby` 접속 목록(기존 접속 `SC_KICK` + 소켓 닫기 → 새 로그인은 이전 접속의 마지막 저장이 끝날 때까지 대기, `LOGIN_RESULT_BUSY`), `Room::NotifyClosedIfDone`, `[Network] LoginWaitTimeoutSec`·`LoginCloseForceSec`, 시험용 `[Debug] DbSaveDelayMs`, 클라이언트 `SC_KICK`·게임 중 끊김 → 로그인 화면(빌드만, PIE 확인은 A2.5 확인 때 함께) (2026-11-16, D-158·T)

### Phase W
작업 환경 개선(Claude Code), D-144·D-145·D-146.
- [완료] W0 개선 계획을 ROADMAP·DECISIONS에 등록 (2026-11-12)
- [완료] W1 프로토콜 재생성 방어 — `GanPackets.bat` 수정 + git pre-commit 훅 + Claude Stop 훅 (2026-11-12, 사용법은 `Common/Protocol/CLAUDE.md`)
- [완료] W2 판독 전용 `reader` 에이전트(`.claude/agents/reader.md`) (2026-11-12)
- [완료] W3 메모리 → 프로젝트 스킬 6개 이관, 함정 기록은 `UEClient/JM/CLAUDE.md`로 (2026-11-12 — 에디터를 닫는 `ue-build` 등 부수 효과가 있는 스킬은 첫 실제 사용 때 확인)
- [완료] W4 ROADMAP 정리(64KB → 35KB, 한글 상태 표기, 완료 이력 분리, `TODO.md` 통합) + 완료 기준 33개·문구 변경 검토 반영, 검증 게이트 문구 복원 (2026-11-12 정리, 2026-11-15 사용자 승인, D-146)
- [완료] W5 결정 로그 현행 요약 `DECISIONS_CURRENT.md` 신설 (2026-11-12, D-144)
- [완료] W6 "채택하지 않은 안" 12줄 + 요약에 빠졌던 D-074·D-089 추가 (2026-11-15, D-145)

### 트랙 A — A0. 서버 치명 오류 수정 + 최소 검증 하니스
- [완료] A0.1 `Session::Disconnect` null 체크 (2026-10-20, T)
- [완료] A0.2 `header.size` 상·하한 검증, 핸들러 배열 경계(서버·UE) (2026-10-20, T — UE는 컴파일만)
- [완료] A0.3 `JobQueue` 깨우기 누락·송신 큐 잔류·Timer busy-spin 수정 (2026-10-20, T)
- [완료] A0.4 `dt` 클램프·`StatComponent` null 안전·`GameMap` 경계·가입 PlayerType 검증 (2026-10-20, T — 재확인은 백로그)
- [완료] A0.5 Room 입장 경쟁 조건 + 맵 리소스 로딩 공유 (2026-10-20, T)
- [완료] A0.6 최소 검증 하니스 `Server/LoadTest`·`Metrics`·`CS_PING`/`SC_PONG`·악성 패킷 스크립트 (2026-10-20, T)
- [완료] A0.7 QA 버그 수정 C1·C2·H1·H2 + 가입 시 영웅 선택 (2026-10-28, T)

### 트랙 A — A2′. 몬스터·전투
- [완료] A2.1 몬스터 사망 제거 + 리스폰, 클라 `SC_DEAD`/`SC_REMOVE_OBJECT` 반영 (2026-10-20, T)
- [완료] A2.2 전투 입력·스킬 a~g (2026-10-28, D·T, `DESIGN_COMBAT_SKILL.md`)
  - [완료] A2.2a 프로토콜 + 서버(`CS_ATTACK` 확장·SkillTable 확장·지면 장판·LoadTest hunt) (2026-10-20, T)
  - [완료] A2.2b 클라 카메라/입력(3인칭 마우스 회전, `Left Alt` 자유 커서, `DT_MovementTable`) (2026-10-21, D — 쿼터뷰 고정안 폐기)
  - [완료] A2.2c 클라 조준(크로스헤어 기준, 조준 모드·인디케이터) (2026-10-21, T)
  - [완료] A2.2d 전투 연출(상체 레이어 블렌드·방향 고정·장판 표시) (2026-10-21, D — 이동 속도 Blend Weight는 하지 않기로)
  - [완료] A2.2e 전사 스킬 모션·이펙트(`Montage`·`Fx` 열, `Skill Hit` 노티파이 → `ExportSkillTiming.bat`) (2026-10-28, T)
  - [완료] A2.2f 전투 HUD(HP 바, 스킬 슬롯 쿨타임, `SC_ATTACK.cooldown_ms`) (2026-10-28, T)
  - [완료] A2.2g 행동 잠금·공격 속도 구조(`AnimMs`, 서버 공용 행동 잠금) (2026-10-28, D·T)
- [완료] A2.6 미사용 패킷 정리(C-1에서 삭제, 채팅은 C-4에서 구현) (2026-10-30)

### 트랙 A — R. 저장소 정리
- [완료] C-1 안 쓰는 코드·패킷 삭제 (2026-10-28, T)
- [완료] C-2 DB 정리(미사용 테이블·열 삭제, 백업 `GameServer_2026-10-28_preC2.bak`) (2026-10-29, T)
- [완료] C-3 미사용 에셋 삭제 + PIE 회귀 (2026-10-30, T)
- [완료] U1 시스템 메시지 창 `WBP_SystemMessage` + 방 입장 대기·에셋 미리 로드 (2026-10-30, D)
- [완료] C-4 채팅 복구 (2026-10-30, D·T)
- [완료] C-5 소스 폴더 구조화 + ServerCore 정적 라이브러리 분리 (2026-10-30, D)
- [완료] C-6 BotEngine 분리 + DummyClient 재작성(LoadTest 시각화) (2026-10-30, D·T)
- [완료] C-7 에디터 MCP 도구 결정 — Epic 공식 MCP, uemcp 폐기(U5·U6) (2026-11-05, D)
  - 참고(2026-11-03 조사, 채택 안 함): 대안 ChiR24/Unreal_mcp(EngineWire) ★1,003·기여자 27·v0.6.0·MIT — 에디터 안 C++ 실행·0.x 호환 깨짐이 위험. chongdashu 등 나머지는 관리 중단·클라우드·1인 개발

### 트랙 A — S. 서버 권위 이동
- [완료] S1 서버 권위 이동(S1a 서버 검증·보정, S1b 클라 보정 적용·외삽 상한, 내비메시 재생성) (2026-10-31, D·T, `DESIGN_CONTROL_CAMERA.md` — 한계: 봇 길 찾기 없음, z는 클라 값)

### 트랙 A — A1. 관측성·설정
- [완료] A1.1 설정 파일 외부화 `Server/Server/ServerConfig.ini` 계층형 설정 (2026-10-31, D·T)
- [완료] A1.2 비동기 로거 ServerCore `Utils/Logger`, 소스 UTF-8 통일 (2026-10-31, D·T)
- [완료] A1.3 AOI 재설계와 병목 탐색 (2026-10-31, D·T)
  - [완료] A1.3a 측정 도구 + 현재 격자 기준선 (2026-10-31, T)
  - [완료] A1.3b 시야 반경 R 50m·H 5m 결정 + 시야 감사 (2026-10-31, T)
  - [완료] A1.3c 큰 셀 + 쿼드트리 AOI(`AoiIndex`) (2026-10-31, T)
  - [완료] A1.3d 분석·보고 → P1~P4 (2026-10-31, T)
- [완료] A1.4 서버 CSV 로더로 `ServerData.h` 하드코딩 대체(= M2 서버 CSV 로더) (2026-10-20, T)

### 트랙 B — Phase M0~M4 (기존 Test 병합)
- [완료] M0 서버 자리(Seam) → 서버 권위 방침 기록(`DESIGN_COMBAT_SKILL.md`)
- [완료] M0 Test의 작업 절차·문서 구조·데이터 규약 이식
- [완료] M0 원본 에셋 팩 재다운로드(사용자, `ExternalAssets.md`)
- [완료] M0 `MovementTable` UE DataTable 구조 설계 (A2.2b에서)
- [완료] M0.5-0 `.gitignore`에 `Content/Paragon*/`·`StarterContent/`, 추적 중이던 437개 인덱스에서 제거 (2026-10-21)
- [완료] M0.5-1 클라 네트워크 안정화 (2026-10-27 — 끊김 화면 처리는 백로그)
- [완료] M0.5-2 `JMPlayer.cpp` 몽타주 로드 버그·조기 return 수정 (2026-10-21)
- [완료] M0.5-3 명명/배치 정리, CP949 주석 UTF-8 변환, `[CoreRedirects]` (2026-10-21)
- [완료] M0.5-4 Content 정리(깨진 참조·잔재 에셋·레거시 입력) (2026-10-21)
- [완료] M0.5-5 레이어 분리(송신 파사드·핸들러 보일러플레이트·UIManager 일원화) (2026-10-27)
- [완료] M1-1 기본 무기 본 스케일 0 숨김 검증 (현재는 사용 안 함)
- [폐기] M1-2 무기 분리 (활·방패 분리가 막힘)
- [폐기] M1-3 교차 리타게팅 (같은 이유)
- [완료] M1 캐릭터 베이스 결정: Paragon 영웅 + 고유 무기 (2026-10-17, D — 이후 MC로 대체)
- [완료] M2 Row 구조체 + `UDataManager`, `MovementTable` 임포트 (A2.2b에서)
- [완료] M2 서버 CSV 로더 → A1.4와 같음
- [폐기] M2 `UJMAssetDataManager` Mock DB 교체 여부 (클래스가 이미 삭제됨, 2026-10-30)
- [폐기] M3 HeroTable (모듈러 전환으로 대체 — Phase MC, 2026-11-04)
- [완료] M3 영웅별 AnimBP 연결 (A2.2d·A2.2e에서)
- [폐기] M3 WeaponItemTable + 무기 머티리얼 교체 + 장착 패킷 (모듈러 전환으로 대체 — MC3.1·MC3.2, 2026-11-04)
- [완료] M3 모듈러 코드/에셋 삭제 (남은 일은 M0.5-4·C-1에서)
- [완료] M4 SkillTable + 스킬 시전 패킷 (A2.2a~g에서)
- [완료] M4 AnimNotify는 연출 타이밍만, 판정은 서버 (A2.2e에서)
- [완료] M3 Paragon 에셋 최종 Move 2단계: Greystone·Sparrow FX 의존 243개 + Rampage FX 의존 272개를 `Content/FX/Dep/<팩>/`로 Move, `ParagonGreystone`·`ParagonSparrow`·옛 `Character/Greystone`·`Character/Sparrow` 삭제 (2026-11-12, D — 화살은 `/Game/Equipment/Common/SM_Arrow`로 복제)

### 트랙 B — Phase MC
- [완료] MC0.1 결정 기록 + 프로토타입 인수인계서 (2026-11-04, D)
- [완료] MC0.2 `DESIGN_ART_CONCEPT.md` (2026-11-04)
- [완료] MC0.3 설계 문서 4개 개정 (2026-11-04)
- [완료] MC1.1 매니퀸 60개를 `Content/Character/Mannequin/`으로 이식 (2026-11-04, `ExternalAssets.md`)
- [완료] MC1.2 기준 캐릭터 = 키트 조립 그대로(`jm_kit.preview` + `kit_profiles.json`) (2026-11-04, D·T)
- [완료] MC1.3 뼈대 검사 도구 `jm_kit.skeleton_check` (2026-11-04, T)
- [완료] MC2.1 의상 맞춤 도구 재작성 `jm_kit.garment` (2026-11-04, T)
- [완료] MC2.2 천 세트 7개(VARCO 2,300포인트) (2026-11-05, D·T)
- [완료] MC2.3 판금 세트 7개(투구 = 눈높이 이미지 → 3만 폴리 → UE 축소 LOD, VARCO 누적 3,250포인트) (2026-11-06, T)
- [완료] MC2.4 아이콘 14개 `T_Icon_<Item>`(`jm_kit.icon`) (2026-11-07, T)
- [완료] U1~U4 UE 5.8 엔진 전환·회귀 (2026-11-05, T)
- [완료] U5 Epic 공식 MCP 전환·시험(= C-7), `jm_mcp` 툴셋 (2026-11-05, T)
- [완료] U6 uemcp 삭제 (2026-11-05)
- [완료] U7 브랜치 병합·새 출발(`a4a748a`), 워크트리 제거 (2026-11-05)
- [완료] MC3.1 `ItemTable` + SkillTable 무기 ID 재번호 + StatTable MALE/FEMALE + `MonsterTable.SkillId` (2026-11-07, D·T)
- [완료] MC3.1b `PlayerStatTable`·몬스터 ID 1001·피해 공식 (2026-11-07, D·T)
- [완료] MC3.2 `CS_/SC_CHANGE_EQUIP` + 서버 검증·스왑 잠금 + 콘솔 `Equip` (2026-11-07, D·T)
- [완료] MC3.3 `Player_Equipped` 저장·기본 장비·마이그레이션 (2026-11-07, D·T)
- [완료] MC4.1 `UEquipmentComponent`·`ULeaderPoseAppearance`·`BodyTable`·`ABP_PlayerBase` (2026-11-07, D·T)
- [완료] MC4.1b 원격 이동 방향 전환 튐(`MoveSendTurnDeg` 30 + `RemoteCatchUpSpeedMul` 1.3) (2026-11-07, D·T — 남은 다듬기는 P2·P3)
- [완료] MC4.2a 구조 + 맨손 + 사망(`ALI_Weapon`·`WeaponTypeTable`) (2026-11-08, D·T — `ExportSkillTiming` 확인은 백로그)
- [완료] MC4.2b Mixamo 시범 리타깃 + 일괄 리타깃 도구 (2026-11-09, D·T, `ExternalAssets.md`)
- [완료] MC4.2c 한손검(`OneHandSword`) + 공용 피격, 스킬 2002 도약 내려찍기 (2026-11-09, D·T, `ExternalAssets.md`)
- [완료] MC4.2d-1 스킬 조준 동기화(`CS_/SC_SKILL_AIM`) (2026-11-09, T)
- [완료] MC4.5 FX 라이브러리 1~5단계: Paragon 파티클 193개 → 채택 51개 손질(`UFxEditLibrary`·`jm_kit.fx_fix`) (2026-11-12, `FX_CATALOG.md`)

### 수동 작업 기록 (에디터, 요약)
절차는 대화창으로 안내하고 여기에는 결과만 남깁니다.
- [완료] Greystone·Sparrow 몽타주 Duplicate·AnimBP Rename (2026-10-19) — 옛 폴더는 2026-11-12 삭제
- [완료] Rampage: `BP_Npc` → `Character/Rampage/` 몽타주·`ABP_Rampage`, SkillTable 301 `Montage` = `AM_Rampage_Attack` (2026-10-30 확인)
- [완료] 장판 이펙트 `P_Sparrow_RainofArrows`·A2.2e-1a·A2.2e-1b·전사 판정 조정(2026-10-27~28), A2.2f·A2.2g·A0.7 위젯·DT 작업(2026-10-28), M0.5-4 Content 정리(2026-10-21), U1 `WBP_SystemMessage`(2026-10-30)
- [완료] MC1.1 매니퀸 Move (2026-11-04), MC2.1b Fab 키트 `Content/CoreC`(git 제외) 설치 — 몸 부위·`MI_Skin_*`은 삭제 (2026-11-04)
- [폐기] MC1.3 `jm_modular.body_parts` 몸 부위 12개·`L_BodyTest` (2026-11-04 결과물 삭제)
- [폐기] MC2.1 프로토타입 FBX 5개 임포트·`Dev/MC/ProtoFit`·`Dev/MC/Fit` (2026-11-04 결과물 삭제)
- [완료] MC4.1 `ABP_PlayerBase` 상태·전이 5개는 사용자가 직접 연결 — 공식 MCP가 중첩 그래프에 노드를 못 만듦 (2026-11-07)
- [완료] MC4.2a·MC4.2b·MC4.2c·MC4.2d-2·MC4.2e 에셋 생성·리타깃·DT 재임포트 (2026-11-08~10, 도구로 처리 — `AnimBuilderTools`·`RetargetTools`)

### VARCO 흐름 노드 (JH_MMO 플로)
투구 정면 컨셉 `531e1ef7…`, 3/4 시점 편집 `c1511cae…`, 3차 3D `8456f6d4…`(실패), 초록 배경 `aa21a66f…`, 4차 3D `4e325e94…`(실패), 사슬 뺀 눈높이 `a0bb8331…`, **5차 3D `ad0e9513…`(채택, 출력 0)**. 판금 상의 컨셉 `cb90b725…`가 세트 스타일 참고.
