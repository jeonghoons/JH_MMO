---
name: ue-build
description: UE 클라이언트(JM) C++를 고친 뒤 명령줄로 컴파일할 때 사용. 에디터가 켜져 있으면 저장 → 닫기 → Build.bat → 에디터 재실행 → MCP 재연결까지 묻지 않고 직접 처리한다.
---

# UE 클라이언트 빌드 (에디터 재시작 포함)

에디터가 켜져 있으면 Live Coding 때문에 `Unable to build while Live Coding is active`로 막힌다. 사용자는 닫기·빌드·재실행을 Claude가 **묻지 않고 직접** 하길 원한다(2026-11-12).

## 순서

1. **에디터 확인**: PowerShell `Get-Process UnrealEditor -ErrorAction SilentlyContinue`. 없으면 3번으로 바로 간다(묻지 않음).
2. **저장 후 닫기** (켜져 있을 때)
   - MCP `call_tool` → `AssetTools.save_assets([])`로 저장되지 않은 에셋을 전부 저장한다. MCP가 연결돼 있지 않으면 사용자에게 저장 여부를 확인한다.
   - PowerShell: `$p = Get-Process UnrealEditor; $p.CloseMainWindow() | Out-Null; if (-not $p.WaitForExit(90000)) { taskkill /PID $p.Id /T /F }`
3. **빌드**
   ```
   "C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat" JMEditor Win64 Development -Project="<저장소>\UEClient\JM\JM.uproject" -WaitMutex
   ```
   새 모듈(UMG, AssetTools 등)이 필요하면 `JM.Build.cs`/`JMEditorTools.Build.cs`를 먼저 고친다.
4. **재실행**: `UnrealEditor.exe <uproject>`를 **Bash `run_in_background`로** 띄운다. `Start-Process`나 `cmd /c start`로 분리하면 세션 종료 훅이 정리하지 못한다.
5. **MCP 대기**: `curl -s -X POST http://127.0.0.1:8000/mcp`가 응답할 때까지 2초 간격으로 확인(보통 10초 안팎) → 바로 도구 호출. 첫 호출이 ECONNRESET이면 한 번만 재시도. `/mcp` 재연결은 필요 없다.

## 함정

- 5.8 전환 커밋이 없는 브랜치에서 엔진만 바꾸면 `BuildSettingsVersion` 불일치로 UBT가 시작부터 실패한다.
- 컴파일러 버전 판정은 MSVC 폴더 이름이 아니라 `cl.exe` ProductVersion 기준이다.
- 소스 인코딩 함정(CP949·BOM)은 `UEClient/JM/CLAUDE.md` "알려진 함정" 참고.
- 서버·LoadTest는 에디터와 무관하게 MSBuild로 빌드한다(`run-server` 스킬).
