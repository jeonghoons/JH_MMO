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

**입력 없이 움직이기(2026-11-20 DG3b 2인 PIE)**: `world_step`의 `pie_*` 단계는 `args.world`(0부터)로 PIE 창을 고른다. `pie_walk`(`{"world", "x", "y"(m) 또는 "path": [[x, y], ...], "tol", "timeout"}` — 바로 돌아오고 뒤에서 걸음, `{"status": true}`로 상태·위치, 직선으로만 가므로 막히면 `path`로 돌아감), `pie_exec`의 `Portal`(포털 반경 안에서)·`AutoAttack <횟수> 0`, `pie_revive`(부활 창이 떠 있으면 버튼 처리 함수를 부름 — 죽었는지 확인하는 데도 씀: `clicked` 1). 판정은 창별 `pie_chars`의 클래스별 수(`BP_MyPlayer_C`·`BP_Player_C`(다른 플레이어 + 상점 NPC)·`BP_Npc_C`(몬스터)) — 시야 반경(50m) 안에서 비교할 것. 하급 몬스터(1001)는 비선공이라 죽으려면 한 대만 치고 맞는다(여러 번 치면 먼저 잡음). `pie_walk`는 이동 입력 함수를 건너뛰므로 죽은 캐릭터·이동 잠금 중에도 로컬에서 움직인다(서버로는 안 나감 — 사망 여부는 `pie_revive`의 `clicked`로 본다). `AutoAttack`은 카메라 방향(걷기 도구는 카메라를 돌리지 않음 — 스폰 때 방향)으로 나가므로 몬스터가 그쪽 앞 반경 안에 오게 선다. 돌아다니는 몬스터를 쫓을 때는 `ProgrammaticToolset.execute_tool_script`로 `pie_chars` → `pie_walk`를 한 호출에 묶는다(2026-11-21 DG3c-2). 화면 캡처 `shot.py … editor`에는 첫째 창만 찍힌다(PIE 창이 작으면 HUD 글자가 흐려 판독이 안 됨). 서버 로그는 실행 중 잠겨 있어 종료 뒤에 읽는다. 시험 계정 상태(HP·인벤토리)가 이전 시험의 영향을 받았는지 먼저 본다.

## 5. 1차 판정 도구: `pp_*` 단계 (2026-11-22 무기별 사전 점검)

게임 C++는 성공 로그가 거의 없으므로(실패 경고만) 캡처 전에 `jm_kit/pie_probe.py`로 PIE 월드를 읽어 1차 판정하고, 캡처는 2차로 쓴다. 모두 `WorldTools.world_step`(name은 아무 값, 예 `continent01`).
- `pp_probe {"radius_m", "max_monsters"}`: 내 몽타주·재생 위치, 소켓에 붙은 무기·방패(`Equip_Weapon_Static`·`Equip_Offhand_Static`의 메시·소켓), HUD HP·MP 글자, 가까운 몬스터 HP 바 비율, 살아 있는 파티클, 카메라 상태.
- `pp_rec_start {"seconds", "cmds", "cmds_at": [[ms, cmd]], "shots_ms": [ms], "side_view": {"dist_m", "angle", "height_m"}, "max_monsters"}` → `pp_rec_read`: 매 프레임 바뀐 것만 시각(ms)과 함께 기록(몽타주 시작·끝, 소켓 변경, 새 파티클과 주인·위치, 몬스터 HP 변화, 내 HP 변화 `my_hp`(그 순간 내 몽타주·위치), 몬스터 몽타주 변화 `monster_anim`). `cmds`·`cmds_at`은 `AutoAttack`·`AttackStats`·`Equip`·`HighResShot`만. `shots_ms`는 그 시각에 `HighResShot 1`(파일 `Saved/Screenshots/WindowsEditor/`), `side_view`는 캐릭터 옆 고정 카메라(끝나면 스프링 암 끝으로 되돌림, 꼬이면 `pp_cam_reset`).
- `pp_aim`: 가장 가까운 몬스터로 컨트롤러 회전(스프링 암 중심 기준 — 카메라 위치 기준이면 회전 뒤 시선이 어긋나 목표 지정 스킬이 대상을 못 잡음). `pp_fx {"paths"}`: 파티클 에셋의 이미터별 반복·길이. `pp_trail {"sockets"}`: 몸 메시 소켓 위치(몽타주 노티파이는 5.8 Python에서 읽기 막힘).
- 함정: 몬스터 1001은 돌아다녀 근접 사거리(한손검 1.9m·앞 180°) 경계에서 자주 빗나감 → 넓은 스킬(2002·4002)이나 화살비(3002)로 먼저 맞혀 끌어온 뒤 근접 시험. 맞는 중에는 `HIT_STUN` 거부가 섞임(규칙). 몬스터 옆에서 오래 기다리면 시험 캐릭터가 죽음. `HighResShot`은 가끔 검은 화면(다시 찍으면 정상). 시험 계정 가방이 가득이면 무기 해제·방패 자동 해제가 `INVENTORY_FULL`.
