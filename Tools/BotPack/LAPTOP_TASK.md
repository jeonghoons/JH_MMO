# 노트북 Claude 작업 지시서 — A5.4c 부하 시험 봇 실행 (+ A5.4d 재측정)

## A5.4d 재측정 (2026-11-24 추가 — 지금 할 일은 이것)
서버에 P2 이동 묶음(새 패킷 `SC_MOVE_BATCH`, D-307)이 들어가 같은 단계를 다시 잽니다. 아래 "순서"를 그대로 따르되 **다른 점만**:
- `LoadTest.exe`가 새 빌드입니다(묶음 패킷을 읽음) — 반드시 pull한 뒤 실행합니다. 3번 사전 확인의 `--help` 첫 줄에 `--profile ... turn` 등이 보이면 됩니다.
- 실행: `powershell -NoProfile -ExecutionPolicy Bypass -Command "& .\run_bots.ps1 -Stages d_smoke,d_s1000_1,d_s1000_2,d_s2500_1,d_s2500_2,d_s5000_1,d_s5000_2"`
  (`-File`로 넘기면 쉼표 목록이 배열로 나뉘지 않아 "모르는 단계"로 바로 끝남 — 2026-11-24 노트북 Claude 확인)
  (`RunBots.bat`은 A5.4c 단계를 돌리므로 쓰지 않습니다.) 결과는 `results\d_<단계>\`, 환경은 `results\env_d.txt`에 생기고 A5.4c 결과는 그대로 남습니다.
- 정리 파일은 `results\status_d.md`(같은 형식)로 만듭니다 — `status.md`는 고치지 않습니다.
- 커밋 메시지: `측정: A5.4d 노트북 봇 결과(<끝낸 단계 목록>)`.

---

이 문서는 **노트북에서 실행 중인 Claude**가 읽고 그대로 수행하는 지시서입니다. 서버 PC의 Claude가 작성했고, 두 Claude는 git으로만 연락합니다(설계: `Docs/DECISIONS.md` D-305, 계획: ROADMAP A5.4c).

## 무엇을 하는가
서버 PC에서 게임 서버가 단계마다 새로 뜨고 내려갑니다. 이 노트북은 같은 단계 목록에 맞춰 봇(LoadTest.exe)을 붙여 부하를 주고, 결과를 `Tools/BotPack/results/`에 남겨 푸시합니다.

| 단계 | 노트북 봇 | 비고 |
|---|---|---|
| `smoke` | 20명, 30초 | 연결 확인 |
| `s1000_1`, `s1000_2` | 1,000명(초당 200), 125초 | |
| `s2500_1`, `s2500_2` | 2,500명(초당 200), 135초 | |
| `s5000_1`, `s5000_2` | 2,500명(초당 100, 계정 bot_2500~), 145초 | 서버 PC가 나머지 2,500명을 붙임 |

단계 사이의 맞춤은 자동입니다: 스크립트가 서버 포트(8888)가 열리면 시작하고, 끝나면 포트가 닫힐 때까지 기다린 뒤 다음 단계로 갑니다. 전체 약 30~40분입니다.

## 순서
1. **받기**: 이 저장소를 최신으로 받습니다(브랜치 `Jeonghoon`).
   - `git`이 PATH에 있으면 `git pull`.
   - 없으면 GitHub Desktop의 git을 씁니다: `%LOCALAPPDATA%\GitHubDesktop\app-*\resources\app\git\cmd\git.exe`. 그것도 안 되면 사용자에게 "GitHub Desktop에서 Fetch/Pull을 눌러 주세요"라고 요청합니다.
2. **서버 PC IP**: 사용자에게 물어봅니다(서버 PC Claude가 사용자에게 알려 줌). IP는 `Tools/BotPack/server_ip.txt`에 한 줄로 저장합니다 — 이 파일은 `.gitignore`에 있어 커밋되지 않습니다. **IP를 커밋 메시지·문서·결과 파일에 쓰지 마세요**(공개 저장소).
3. **사전 확인**:
   - `Tools\BotPack\LoadTest.exe --help`가 실행되는지(DLL 누락 오류가 없는지).
   - `Test-NetConnection <IP> -Port 8888` — 서버 PC가 smoke 단계를 띄워 두었으면 `TcpTestSucceeded : True`. False면 사용자에게 "서버 PC에서 방화벽 허용과 서버 대기를 확인해 달라"고 알립니다.
   - 전원이 충전기에 연결돼 있는지, 노트북 전원 모드가 "최고 성능"인지 확인합니다. 아니면 사용자에게 바꿔 달라고 요청합니다(바꾸지 못하면 그대로 진행하고 `status.md`에 적음).
4. **실행**: `Tools\BotPack` 폴더에서
   `powershell -NoProfile -ExecutionPolicy Bypass -File .\run_bots.ps1`
   (사람이 `RunBots.bat`을 더블클릭해도 같습니다.) 긴 실행이므로 백그라운드로 돌리고 진행 출력을 봅니다.
5. **감시** — 단계가 끝날 때마다 `results\<단계>\console.txt`의 `[summary]` 줄과 `bot_cpu.csv`를 봅니다:
   - `attempts`와 `ok`가 다르거나 `loginFail`·`giveUp`이 0이 아니면 기록.
   - `bot_cpu.csv`의 `cpu_pct_of_all`(노트북 전체 CPU 대비 %)이 90 이상으로 30초 넘게 이어지면 "봇 CPU 한계"로 기록. **멈추지는 말고** 계속 진행합니다(서버 PC Claude가 비율을 조정함).
   - 스크립트가 `error.txt`를 남기고 멈추면(서버 포트가 30분 동안 안 열림) 그 단계부터 다시 돌릴 수 있습니다: `.\run_bots.ps1 -Stages s2500_1,s2500_2,...`.
6. **`results\status.md` 작성** — 단계별 한 줄: 단계, 접속 성공/시도, 로그인 실패, 접속 시간 p99(`[summary] enter`), 핑 p99, 봇 CPU 최대(`cpu_pct_of_all`), 특이 사항. 맨 위에 노트북 환경 요약(`results\env.txt`에서 — Wi-Fi 정보 포함).
7. **커밋·푸시**:
   - 먼저 `git pull --rebase`(서버 PC가 그사이 푸시했을 수 있음).
   - `git add Tools/BotPack/results` — **이 경로만** 이름으로 추가합니다. 다른 변경이 보이면 커밋하지 말고 사용자에게 알립니다.
   - 커밋 메시지: `측정: A5.4c 노트북 봇 결과(<끝낸 단계 목록>)` + 빈 줄 + `Co-Authored-By: Claude <noreply@anthropic.com>`.
   - `git push`. 권한 문제로 실패하면 사용자에게 GitHub Desktop으로 푸시해 달라고 요청합니다.
   - 중간에 문제가 생겨 멈출 때도 그때까지의 결과와 `status.md`를 같은 방식으로 푸시합니다.
8. 사용자에게 "결과를 푸시했다"고 알리고 끝냅니다(서버 PC Claude가 pull해서 정리함).

## 하지 말 것
- `Tools/BotPack/results/` 밖의 파일을 고치거나 커밋하지 않습니다(스크립트에 문제가 있으면 고치지 말고 `status.md`에 적어 알림).
- `server_ip.txt`·IP 주소를 커밋하지 않습니다.
- 서버 PC에 8888 포트 말고 다른 접속(원격 데스크톱·공유 폴더 등)을 시도하지 않습니다.
- 봇 수·속도를 바꾸지 않습니다(서버 PC 실행기와 단계 목록이 맞아야 함).
