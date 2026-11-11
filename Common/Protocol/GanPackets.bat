@echo off
rem 사용법: GanPackets.bat [nopause]
rem   인자를 하나라도 주면 마지막 pause를 생략합니다(Claude·스크립트 실행용). 더블클릭 실행은 인자 없이.
rem   실패하면 종료 코드 1을 돌려주고 성공 메시지를 출력하지 않습니다.
setlocal
cd /d "%~dp0"

set "OUT_DIR=.\Generated"
set "SERVER_DIR=..\..\Server\Server\Protocol"
set "CLIENT_DIR=..\..\UEClient\JM\Source\JM\Protocol"

if exist %OUT_DIR% rmdir /s /q %OUT_DIR%
mkdir %OUT_DIR%

rem 전체 경로로 실행: NoDefaultCurrentDirectoryInExePath 환경에서도 현재 폴더의 protoc.exe를 찾도록
"%~dp0protoc.exe" --cpp_out=%OUT_DIR% .\Enum.proto .\Struct.proto .\Protocol.proto
if errorlevel 1 goto :fail

if not exist %SERVER_DIR% mkdir %SERVER_DIR%
xcopy /y "%OUT_DIR%\*" "%SERVER_DIR%\"
if errorlevel 1 goto :fail

if not exist %CLIENT_DIR% mkdir %CLIENT_DIR%
xcopy /y "%OUT_DIR%\*" "%CLIENT_DIR%\"
if errorlevel 1 goto :fail

xcopy /y "%OUT_DIR%\*" ".\"
if errorlevel 1 goto :fail

rmdir /s /q %OUT_DIR%

echo =========================================================
echo [JH_MMO] 패킷 코드가 서버 및 클라이언트에 동기화되었습니다.
echo =========================================================
if "%~1"=="" pause
exit /b 0

:fail
if exist %OUT_DIR% rmdir /s /q %OUT_DIR%
echo =========================================================
echo [JH_MMO] 패킷 코드 생성 또는 복사에 실패했습니다. 위의 오류를 확인하세요.
echo =========================================================
if "%~1"=="" pause
exit /b 1
