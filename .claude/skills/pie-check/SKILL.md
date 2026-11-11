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

MCP `EditorApp`의 `StartPIE`/`StopPIE`로 시작·종료하고, 로그인 입력은 사용자가 한다. Claude는 애니메이션·이펙트 모양을 직접 볼 수 없으므로 `CaptureViewport` 스크린샷이나 사용자 확인으로 판단한다.
