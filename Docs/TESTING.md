# 수동 테스트 절차

서버에는 자동화된 테스트 스위트가 없습니다. 아래는 `DummyClient`로 서버 로직을 수동 검증하는 절차입니다. 아키텍처/빌드 전반은 [Server/CLAUDE.md](../Server/CLAUDE.md)를 참고하세요.

## 기본 절차

1. `GameServer`를 빌드하고 실행합니다: `msbuild Server.sln /p:Configuration=Debug /p:Platform=x64 /t:GameServer`
2. `DummyClient`를 빌드하고 실행합니다 (같은 명령의 `/t:DummyClient`).
3. 봇 개수를 바꾸고 싶다면 `Server/DummyClient/pch.h`의 `MAX_TEST`(현재 50000) / `MAX_CLIENTS`(= `MAX_TEST * 2`) 상수를 수정한 뒤 재빌드합니다 — 런타임 인자로는 조절되지 않습니다.
4. 서버 콘솔/로그에서 접속·이동·전투 등 확인하려는 패킷 흐름을 관찰합니다.

## 새 패킷/기능을 추가했을 때 체크리스트

- [ ] `GanPackets.bat`을 실행해 `Server`/`UEClient` 양쪽에 최신 프로토콜이 복사됐는지 확인
- [ ] `GameServer`가 경고 없이 빌드되는지 확인
- [ ] `DummyClient`로 봇을 붙여 새 패킷이 의도한 `Handle_CS_*`/클라이언트 핸들러로 도달하는지 확인
- [ ] 여러 봇을 동시에 붙였을 때 `Room`의 `PushJob`/`ReserveJob` 경로 밖에서 상태가 바뀌지 않는지 확인 (동시성 이슈)
