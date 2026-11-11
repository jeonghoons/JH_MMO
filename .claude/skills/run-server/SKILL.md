---
name: run-server
description: GameServer를 빌드·실행하거나 종료할 때 사용(PIE 확인, 원시 소켓·LoadTest 시험 준비). Debug 재빌드, Server/Server 작업 폴더, 백그라운드 실행, 끝난 뒤 종료까지.
---

# GameServer 빌드·실행

## 빌드 (PIE 전에는 Debug를 반드시 다시 빌드)

Release만 빌드하면 Debug exe가 옛 스키마로 남아 로그인이 `LOGIN_RESULT_SERVER_ERROR`로 실패한다(2026-10-30).

```
msbuild Server/Server/Server.vcxproj /p:Configuration=Debug /p:Platform=x64 /p:SolutionDir=<저장소>\Server\
```

솔루션의 `/t:Server`는 동작하지 않는다. `ServerCore`·`BotEngine`은 참조로 함께 빌드된다. 실행 중인 서버가 있으면 exe를 덮어쓰지 못하므로 먼저 종료한다.

## 실행

- 작업 폴더는 반드시 `Server/Server` — DataTable·`Resource/Map`·`../MapResource`가 상대 경로라 다른 곳에서 띄우면 데이터를 못 찾고 종료된다.
- `..\x64\Debug\GameServer.exe`를 **Bash `run_in_background`로** 띄운다(세션 종료 훅이 정리할 수 있게, `Start-Process` 분리 금지).
- SQL Server Express가 필요하다.
- 서버를 띄웠으면 사용자에게 알린다. 실행 중에는 사용자가 VS로 빌드할 때 "쓰기용으로 열 수 없습니다" 오류가 난다.

## 종료

서버는 저절로 꺼지지 않는다(LoadTest 봇은 `--duration` 뒤 종료). 해당 ROADMAP 항목이 끝나면(커밋 직후) 직접 종료한다:
`taskkill /IM GameServer.exe /T /F`
