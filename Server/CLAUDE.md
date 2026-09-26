# Server/CLAUDE.md

이 문서는 `Server/` 디렉토리에서 작업할 때 참고하는 가이드입니다. 프로젝트 전체 구조는 [루트 CLAUDE.md](../CLAUDE.md)를 참고하세요.

## 빌드 & 실행

Visual Studio 2022에서 `Server/Server.sln`을 열거나 `msbuild`를 사용해 다음 3개 프로젝트 중 하나를 빌드합니다. 솔루션 구성은 `Debug|x64`, `Debug|x86`, `Release|x64`, `Release|x86` 4가지입니다.

```
msbuild Server.sln /p:Configuration=Debug /p:Platform=x64 /t:GameServer
msbuild Server.sln /p:Configuration=Debug /p:Platform=x64 /t:DummyClient
msbuild Server.sln /p:Configuration=Debug /p:Platform=x64 /t:NavGenerator
```

- `GameServer` (`Server/Server/Server.vcxproj`) — 실제 게임 서버, 진입점은 `gameserver.cpp`.
- `DummyClient` (`Server/DummyClient/`) — 같은 프로토콜을 사용하는 헤드리스 부하테스트/봇 클라이언트. UE 클라이언트 없이 서버 로직을 테스트할 때 사용. 봇 개수는 런타임 인자가 아니라 `pch.h`의 `MAX_TEST`(현재 50000)/`MAX_CLIENTS`(=`MAX_TEST * 2`) 상수로 컴파일타임에 정해지므로, 바꾸려면 값을 고치고 재빌드해야 합니다. 수동 검증 절차는 [Docs/TESTING.md](../Docs/TESTING.md) 참고.
- `NavGenerator` (`Server/NavGenerator/`) — Recast/Detour 내비메시 데이터를 오프라인으로 생성하는 도구. 사용법: `NavGenerator.exe <GeoBinPath> <NavOutputPath>` (인자를 생략하면 기본 맵 `ParagonSample`을 사용해 `../MapResource/Export/<맵>/Geometry/<맵>_Geo.bin`을 읽어 `../Server/Resource/Map/<맵>_Geo.nav`로 출력). **UE에서 맵 지오메트리를 다시 export할 때마다 재실행해야 합니다.** 런타임에는 `NavmeshManager`가 이 출력을 사용합니다.

서드파티 라이브러리(Recast/Detour, Protobuf, ODBC용 `sql.h`/`sqlext.h`)는 `Server/ThirdParty/`에 있습니다. 서버에는 자동화된 테스트 스위트가 없습니다 — 검증은 `GameServer`를 빌드해서 `DummyClient`나 UE 클라이언트로 붙여보는 방식으로 합니다.

프로토콜(생성된 `.pb.h`/`.pb.cc`)은 `Server/Server/Protocol/`에 복사되어 들어옵니다. 이 파일들은 [Common/Protocol/CLAUDE.md](../Common/Protocol/CLAUDE.md)에서 관리하며, 절대 손으로 수정하지 마세요.

## 서버 아키텍처

서버는 단일 프로세스 · 멀티스레드 IOCP 서버이며, 다음 흐름으로 구성됩니다: 네트워크 I/O → 커넥션별 `Session` → `PacketHandler` → 게임 로직이 오너별 `JobQueue` 위의 `Job`으로 디스패치됨.

- **IOCP 코어** (`IocpCore`, `IocpEvent`, `Listener`, `ServerService`): accept/recv/send 완료 루프를 직접 처리합니다. `gameserver.cpp`의 `worker_thread`는 `IocpCore::Dispatch()`를 반복 호출할 뿐이며, 네트워크 스레드 자체에는 로직이 없습니다.
- **Job/JobQueue 패턴** (`Job.h`, `JobQueue.h`): 서버의 동시성 모델입니다. 여러 IOCP 워커 스레드에서 안전하게 접근해야 하는 객체(실제로는 `Room`)는 `JobQueue`를 소유하고, 그 큐에서 실행되는 `Job` 안에서만 자기 상태를 변경합니다(`Room::PushJob` / `Room::ReserveJob`). `JobQueue` 자체도 `IocpObject`라서, Job을 push하면 완료 이벤트가 포스트되어 IOCP 워커 스레드가 이를 집어서 처리합니다(`ExecuteJobs`). **`Room`의 내부 상태는 절대 Job 밖에서 직접 건드리지 말고 반드시 `PushJob`/`ReserveJob`을 통해야 합니다.**
- **세션 & 패킷**: `Session`이 `RecvBuffer`/`SendBuffer`를 소유합니다. `PacketHandler::ProcessPacket`이 원시 바이트를 `PacketId`(`Common/Protocol/Enum.proto` 참고)로 구분되는 Protobuf 메시지로 파싱하고, `GPacketHandler` 테이블을 통해 `Handle_CS_*` 함수로 디스패치합니다.
- **월드 모델**: `RoomManager`가 하나 이상의 `Room`을 소유합니다(현재는 시작 시 단일 룸만 생성). 각 `Room`은 자신의 `GameMap`(영역 관심(AOI)/시야 컬링용 균일 격자 `Cell` — `ViewUpdate`, `Room::UpdateView`, `BroadcastAOI` 참고)과 자신의 `NavmeshManager`(이동 검증 및 몬스터 경로탐색용 Recast/Detour 내비메시, `NavGenerator`로 오프라인 생성)를 각각 소유합니다.
- **엔티티**: `GameObject` → `Character` → `Player` / `Monster`. 전투 판정은 `CombatProcessor`와 `Room::CharacterAttack` / `ExecuteSkillHit` / `ApplyDelayedDamage`를 거치며, 스킬/캐릭터 밸런스 데이터는 `ServerData.h`(`DataManager`, 하드코딩된 `_skillTable`/`_characterTable` — 아직 DB나 데이터테이블 기반이 아님)에 있습니다.
- **영속성(Persistence)**: `DBConnection`/`DBConnectionPool`/`DatabaseWorker`가 raw ODBC(`sql.h`/`sqlext.h`) 호출로 SQL Server를 래핑합니다. `DatabaseWorker`는 IOCP 스레드를 막지 않도록 전용 큐에서 DB 작업을 비동기로 처리합니다. 연결 문자열은 `DatabaseWorker.cpp`에 하드코딩돼 있습니다 (`Driver={ODBC Driver 17 for SQL Server};Server=.\SQLEXPRESS;Database=GameServer;Trusted_Connection=Yes;`) — Windows 통합 인증이라 비밀번호는 없습니다. 로컬에서 서버를 실행하려면 SQL Server Express(기본 인스턴스명 `SQLEXPRESS`)와 ODBC Driver 17이 설치돼 있고 `GameServer` DB가 있어야 합니다. DB 스키마는 현재 SSMS로만 관리되고 저장소엔 없습니다 — `Server/Resource/DB/schema.sql`로 내보내 커밋하는 것을 권장합니다(SSMS: DB 우클릭 → Tasks → Generate Scripts → Schema only).
- **인증/로비**: `AuthLobby`가 `Player`가 `Room`에 들어가기 전 로그인/회원가입을 처리합니다.
- **`pch.h`**: 프로젝트 전역 미리 컴파일된 헤더입니다 — Windows/Winsock/ODBC/DirectXMath 헤더, 생성된 `Protocol.pb.h`, 그리고 전역 싱글턴(`GRoomManager`, `GTimer`, `GDBWorker`, `GLobby` — `pch.h`에 `extern`으로 선언, `gameserver.cpp`에 정의)을 여기서 끌어옵니다.

## 네이밍 컨벤션

Server 코드는 별도 접두사 규칙 없이 기능 위주로 클래스/함수명을 짓습니다. 전역 싱글턴에 `G` 접두사를 붙이는 것이 유일한 고정 관례입니다 (`GRoomManager`, `GTimer`, `GDBWorker`, `GLobby`).

## 알려진 함정 (Gotcha)

세션 중 발견한, 놓치기 쉬운 규칙을 여기에 누적합니다.

- `Room`의 내부 상태는 절대 `PushJob`/`ReserveJob` 밖에서 직접 변경하지 마세요 — 여러 IOCP 워커 스레드가 동시에 접근하는 객체이므로 레이스 컨디션이 발생합니다.

## 설계 문서

- [Docs/DESIGN_COMBAT_SKILL.md](../Docs/DESIGN_COMBAT_SKILL.md) — `SkillData`/`_skillTable`을 캐릭터 타입 종속에서 무기 타입 종속 + 타겟팅 타입(Targeted/NonTargeted) 구조로 바꾸는 목표 아키텍처(마이그레이션 순서 미정).
- [Docs/DESIGN_CHARACTER_EQUIPMENT.md](../Docs/DESIGN_CHARACTER_EQUIPMENT.md) — 장비 스탯을 캐릭터 최종 스탯에 반영하려면 서버에 아직 없는 아이템 데이터 테이블이 필요하다는 간극을 기록.
