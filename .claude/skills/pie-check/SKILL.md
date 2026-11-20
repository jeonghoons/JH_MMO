---
name: pie-check
description: 기능을 PIE/에디터에서 눈으로 확인할 준비를 할 때 사용. 서버가 필요한 확인인지 먼저 판단하고, 아니면 서버 없이 TestLevel + BP_TestPlayer로, 장비 외형은 L_KitTest로 확인한다.
---

# PIE·에디터 확인 준비

## 1. 서버가 필요한가 (먼저 판단)

- **필요**: 패킷, 서버 판정·계산, 동기화(원격 캐릭터, AOI), 로그인·장착 흐름 → `run-server` 스킬로 Debug 재빌드 후 실행, 에디터의 기본 맵에서 PIE.
- **불필요**: 애니메이션, 장비 외형, 이펙트, 몽타주 → 서버를 띄우지 않는다(2026-11-09 사용자 지적).

## 2. 서버 없는 확인: TestLevel

- `/Game/Maps/TestLevel` + `BP_TestPlayer`(`jm_kit.testbed`): 좌클릭/1번 = SkillTable 몽타주, `testbed_set_override`로 다른 몽타주 재생.
- 비교가 필요하면 같은 외형 캐릭터를 일렬로 세워 반복 재생(`preview_spawn`, 원본은 Y Bot)하고 **확인 후 바로 지운다**.
- 테스트 폰을 새로 만들면 메시 오프셋(Z·Yaw)이 실제 플레이어 BP와 같은지 확인한다. 2026-11-09 오프셋 누락으로 "오른쪽을 봄·공중에 뜸"을 애니메이션 문제로 오진했다.

## 3. 장비 외형: L_KitTest 정리 규칙

- `Dev/MC/L_KitTest`에는 **현재 실제로 쓰는 장비 세트만** 남녀 캐릭터에 입혀 애니메이션(idle·jog 등)과 함께 둔다.
- 시험·비교용 액터는 원점에서 떨어진 곳(예: y=800)에 고유 접두사로 놓고, 끝나면 `preview.clear(접두사)`로 지운다.
- 고정 액터(`preview.FIXTURES`: `T_Sun`·`T_Back`·`T_Sky`·`T_Atmos`·`T_Fog`·`T_PPV`·`T_Floor`·`T_Fill`·`T_CapG`)는 지우지 않는다.
- 새 세트가 확정되면 대표 배치에 포함하고 레벨을 저장한다. 시험 에셋은 git 제외 경로(`/Game/Dev/MU/…`)에 둔다.

## 4. 실제 PIE

MCP `EditorApp`의 `StartPIE`/`StopPIE`로 시작·종료한다. Claude는 애니메이션·이펙트 모양을 직접 볼 수 없으므로 스크린샷이나 사용자 확인으로 판단한다.

**Claude가 직접 로그인해 확인할 때(D-188)**: 서버를 띄우고(`run-server`) `UEClient/JM/Saved/TestLogin.txt`에 줄마다 `Test1 1234`, `Test2 1234`(PIE 창 번호 순서)를 쓴 뒤, 에디터에서 로그인 레벨 `DevMap`이 열린 상태로 `StartPIE`(`warmupSeconds` 15 안팎) → 로그인 화면이 1초 뒤 자동 로그인하고 서버가 알려 준 레벨로 들어간다. 확인: `WorldTools.world_step`의 `pie_chars`(캐릭터 위치·발 높이 − 지면), 화면은 `python -I -X utf8 Saved/World/shot.py <out.png> editor`(에디터 창 전체 캡처를 파일로 — base64가 대화에 들어오지 않게) → `Read`. 끝나면 `StopPIE`, **`TestLogin.txt`를 지운다**(남겨 두면 사용자의 PIE도 Test 계정으로 들어감). 에디터 PIE 설정이 2인이면 두 번째 창은 둘째 줄 계정으로 들어간다.

**입력 없이 움직이기(2026-11-20 DG3b 2인 PIE)**: `world_step`의 `pie_*` 단계는 `args.world`(0부터)로 PIE 창을 고른다. `pie_walk`(`{"world", "x", "y"(m) 또는 "path": [[x, y], ...], "tol", "timeout"}` — 바로 돌아오고 뒤에서 걸음, `{"status": true}`로 상태·위치, 직선으로만 가므로 막히면 `path`로 돌아감), `pie_exec`의 `Portal`(포털 반경 안에서)·`AutoAttack <횟수> 0`, `pie_revive`(부활 창이 떠 있으면 버튼 처리 함수를 부름 — 죽었는지 확인하는 데도 씀: `clicked` 1). 판정은 창별 `pie_chars`의 클래스별 수(`BP_MyPlayer_C`·`BP_Player_C`(다른 플레이어 + 상점 NPC)·`BP_Npc_C`(몬스터)) — 시야 반경(50m) 안에서 비교할 것. 하급 몬스터(1001)는 비선공이라 죽으려면 한 대만 치고 맞는다(여러 번 치면 먼저 잡음). 화면 캡처 `shot.py … editor`에는 첫째 창만 찍힌다. 서버 로그는 실행 중 잠겨 있어 종료 뒤에 읽는다. 시험 계정 상태(HP·인벤토리)가 이전 시험의 영향을 받았는지 먼저 본다.
