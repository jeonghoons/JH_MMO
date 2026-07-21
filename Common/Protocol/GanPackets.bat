@echo off
set "OUT_DIR=.\Generated"
set "SERVER_DIR=..\..\Server\Server\Protocol"
set "CLIENT_DIR=..\..\UEClient\JM\Source\JM\Protocol"

if exist %OUT_DIR% rmdir /s /q %OUT_DIR%
mkdir %OUT_DIR%

protoc.exe --cpp_out=%OUT_DIR% .\Enum.proto .\Struct.proto .\Protocol.proto

if not exist %SERVER_DIR% mkdir %SERVER_DIR%
xcopy /y "%OUT_DIR%\*" "%SERVER_DIR%\"

if not exist %CLIENT_DIR% mkdir %CLIENT_DIR%
xcopy /y "%OUT_DIR%\*" "%CLIENT_DIR%\"

rmdir /s /q %OUT_DIR%

echo =========================================================
echo [JH_MMO] 패킷 코드가 서버 및 클라이언트에 동기화되었습니다.
echo =========================================================
pause
