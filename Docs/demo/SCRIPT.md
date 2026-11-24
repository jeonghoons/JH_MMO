# A7.4 시연 영상 — 만든 방법 (D-311)

최종본: 1편 3분 16초, 1080p, 한국어 자막(음성 없음). 영상 파일은 저장소에 넣지 않고 YouTube 일부 공개로 올립니다(링크는 A7.3 README).
처음에는 사용자가 플레이·OBS 녹화를 했으나 "길고 난잡"해 전부 다시 찍었습니다. 최종본은 **Claude가 PIE를 자동 조작하고 ffmpeg로 클립을 녹화** → 사용자가 Clipchamp로 자르기·순서 편집 → Claude가 자막을 입힌 것입니다.

## 도구

| 무엇 | 어디 | 하는 일 |
|---|---|---|
| 촬영 서버 | `Server/PerfData/2026-11-24_A7.4_demo/run_demo.sh` | Release, A5.4d와 같은 설정 + 고성능 전원. `BG=`(배경 봇, 더미 로그인), `PCBOTS=`(DummyClient, 계정 bot_0~), `PIE_COUNT=`, `TOWNPCT=`, `PORTALNORANGE=true`(포털 거리 검사 끔 — 순간이동), `DATA=<폴더>`(데이터 사본). 끝낼 때 같은 폴더에 `stop` 파일 |
| 데이터 사본 | `Tools/Demo/demo_data.py <폴더>` | 촬영 서버만 읽는 `Common/DataTable` 사본 — 1001 HP 120·공격력 5, 1004 HP 60, 1005 HP 600 |
| 시험 계정 | `prepare_accounts.sh` / `prepare_accounts.sh restore` | Q1 고정 상태 → 촬영 상태(Test2 Lv5 경험치 2300·강철 무기 3종·MP 물약, Test1 Lv30 근위 장비). 촬영 뒤 restore |
| 퀘스트 건너뛰기 | `quest_skip.sql` | Test2 퀘스트 1 진행을 15로(StopPIE → SQL → StartPIE) |
| 자동 조작 | `world_step` `pie_play`(`UEClient/JM/Content/Python/jm_kit/world.py`) | 타임라인 `[대기 ms, 단계, args]` — 단계는 `pie_exec`·`pie_walk`(`follow_cam` = 카메라가 등 뒤를 따라감)·`ui_pie`·`pp_aim`과 `approach`(살아 있는 가장 가까운 몬스터 앞으로)·`hunt`(접근 → 공격 → HUD 경험치로 처치 확인 → 줍기, 목표 수만큼)·`wait_walk`·`goto` |
| 개발 콘솔 명령 | `AJMPlayerController` | `Interact`(F)·`QuickSlotSet`·`QuickSlot`(숫자 키)·`TradeOffer` — 키·칸 입력과 같은 요청 |
| 녹화 | `Tools/Demo/rec.ps1 -Name C03 -Seconds 24` | 화면 복제 캡처(ddagrab) + NVENC, mp4. `place_win.ps1`(PIE 창을 제목 줄 없이 화면에 맞춤)·`taskbar.ps1 -Hide/-Restore`(작업 표시줄 자동 숨김) |
| 카드·그래프 | `Tools/Demo/make_cards.py`, `Tools/Benchmark/make_figures.py --demo` | `Docs/demo/cards/` 5장(제목·서버 구조·그래프 2·감사합니다), 그래프는 내부 항목 ID·날짜 없이 `Docs/demo/fig/` |
| 자막 | `Docs/demo/subs.srt` → `Tools/Demo/srt2ass.py` → `Tools/Demo/burn.ps1 -Mode burn -Srt Docs/demo/subs.ass [-Overlay "<png>\|<시작>\|<끝>"]` | 줄 앞 `{\an8}` = 화면 위(게임 장면 — 아래에 HUD), `{L}` = 왼쪽 1280px 위(부하 장면 — 오른쪽은 DummyClient), 없음 = 아래(카드). 장면 경계는 `burn.ps1 -Mode sheet` 프레임 시트로 찾음 |

## 클립 (녹화 원본: `C:\Users\kjhkjh\Videos\JH_MMO_demo\C01~C13.mp4`)

| 클립 | 내용 | 서버 |
|---|---|---|
| C01·C02 | 경비대장에게 퀘스트 수락 → 동문(F) → 사냥터 | 촬영 1·2: 데이터 사본 + 배경 봇 30(마을 10·사냥터 20) + PortalNoRange |
| C03~C05 | 한손검(레벨 업)·활(화살비)·양손검 — 무기마다 처치·줍기 | 〃 |
| C06·C06b | 투구·상의 장착, 스킬로 MP 소모 → 액티브 칸 MP 물약 | 〃 |
| C07 | (퀘스트 15마리 DB로 건너뜀) 퀘스트 완료 보상 | 〃 |
| C08·C09 | 2창: 경매 등록 → 검색·구매 → 판매 알림 → 수령, 파티 초대·수락 | 〃 |
| C10·C10b·C11 | 던전 입구(순간이동으로 도착) → 함께 입장 → 첫 구역 → (나머지 화면 밖) → 보스 처치·클리어 | 〃 |
| C12·C13 | 동접: PC DummyClient 2,500 + 노트북 2,500 + PIE 1 = 약 4,950명, PIE `t.MaxFPS 30` | 원본 데이터, `PCBOTS=2500 PIE_COUNT=1` |

## 기록과 수치
- 서버 기록: `Server/PerfData/2026-11-24_A7.4_demo/runs/` — `play`(촬영 1·2 후반), `shoot1_part/play_C1-C3`, `load`(촬영 3, `rec_marks.txt` = 녹화 시작 시각), `try1/`(사용자 시험 촬영). 촬영 중 서버 ERROR 0.
- 동접 장면 녹화 구간: 인원 약 4,900~4,980명, 틱 지연 p99 중앙값 23ms(마을)·41ms(사냥터). 에디터·봇·서버가 PC 한 대를 함께 쓴 상태이며, 합격 판정은 `Docs/BENCHMARK.md` 측정(6ms)을 따릅니다. 영상 자막에 그대로 적었습니다.
- 영상에 밝힌 단축: 퀘스트 15마리 건너뜀, 던전 몬스터 체력 낮춤(촬영 전용 데이터).

## 자동 조작에서 알게 된 것 (다시 찍을 때)
- 아이템 요청(줍기·칸 등록·사용)을 같은 프레임에 둘 보내면 뒤의 것이 `TOO_FREQUENT` — 1초 이상 띄움.
- 드랍은 시체에서 떨어지는 연출 뒤에 생김 → 처치 1.5초 뒤 줍기(+1초 뒤 한 번 더).
- MP가 가득이면 MP 물약은 거부(`NOT_USABLE`) → 스킬로 먼저 소모.
- 던전 안은 통로라 직선 `approach`가 벽에 막힘 → `NavGenerator --query ... route`로 꺾임점을 얻어 `pie_walk path`.
- 포털 F(`Interact`)는 트리거 반경(4m) 안에서만 → 2m 안으로 걸어간 뒤.
- 콘솔로 파티를 맺으면 파티 창이 열린 채 남아 커서·카메라가 막힘 → `UIToggle Party`로 닫음.
- 부하 장면: PIE 프레임 제한이 없으면 PC CPU 100%로 서버 틱이 밀림 → `t.MaxFPS 30`. DummyClient 창은 가려지기 쉬움 → `place_win.ps1 -Title DummyClient`.
