@echo off
setlocal

REM 스킬 몽타주의 Skill Hit 노티파이 시각으로 SkillTable.csv의 HitDelayMs를 갱신하고 DT_SkillTable을 Reimport한다.
REM 언리얼 에디터를 닫고 실행한다. 끝나면 서버를 재시작해야 판정 시점이 반영된다.

REM ===== 로컬 UE 5.6 설치 경로에 맞게 직접 채워야 합니다 =====
set "UE_EDITOR_CMD=C:\Program Files\Epic Games\UE_5.6\Engine\Binaries\Win64\UnrealEditor-Cmd.exe"
set "UPROJECT=%~dp0..\..\UEClient\JM\JM.uproject"

echo === SkillTimingExport: 몽타주 Skill Hit 노티파이 -^> SkillTable.csv HitDelayMs ===
"%UE_EDITOR_CMD%" "%UPROJECT%" -run=SkillTimingExport -Csv="%~dp0SkillTable.csv" -unattended -nopause
if errorlevel 1 (
    echo [SkillTiming] 실패. 위 로그의 [SkillTiming] 줄을 확인하세요.
    exit /b 1
)

echo === 완료. 서버를 재시작하세요. ===
endlocal
