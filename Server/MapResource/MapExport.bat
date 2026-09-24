@echo off
setlocal

if "%~1"=="" (
    echo Usage: MapExport.bat ^<MapName^> [Debug^|Release]
    exit /b 1
)

set "MAP_NAME=%~1"
set "CONFIG=%~2"
if "%CONFIG%"=="" set "CONFIG=Debug"

REM ===== 로컬 UE 5.6 설치 경로에 맞게 직접 채워야 합니다 =====
set "UE_EDITOR_CMD=C:\Program Files\Epic Games\UE_5.6\Engine\Binaries\Win64\UnrealEditor-Cmd.exe"
set "UPROJECT=%~dp0..\..\UEClient\JM\JM.uproject"

set "GEO_BIN=%~dp0Export\%MAP_NAME%\Geometry\%MAP_NAME%_Geo.bin"
set "NAV_OUT=%~dp0..\Server\Resource\Map\%MAP_NAME%_Geo.nav"
set "NAVGEN_EXE=%~dp0..\x64\%CONFIG%\NavGenerator.exe"

echo === [1/2] Unreal Editor Commandlet: %MAP_NAME% Geometry/Logic 익스포트 ===
"%UE_EDITOR_CMD%" "%UPROJECT%" -run=MapExport -Map=%MAP_NAME% -unattended -nopause
if errorlevel 1 (
    echo [MapExport] 언리얼 커맨드릿 실행 실패
    exit /b 1
)

echo === [2/2] NavGenerator: %MAP_NAME% 네비메시(.nav) 빌드 ===
"%NAVGEN_EXE%" "%GEO_BIN%" "%NAV_OUT%"
if errorlevel 1 (
    echo [MapExport] NavGenerator 실행 실패
    exit /b 1
)

echo === MapExport 완료: %MAP_NAME% ===
endlocal
