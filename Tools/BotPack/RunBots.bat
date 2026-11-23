@echo off
rem A5.4c bot runner for the laptop. Double-click, or: RunBots.bat [server_ip]
cd /d "%~dp0"
if "%~1"=="" (
  powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0run_bots.ps1"
) else (
  powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0run_bots.ps1" -ServerIp %1
)
pause
