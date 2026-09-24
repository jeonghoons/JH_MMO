# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project overview

JH_MMO is a custom-built MMO game stack with three top-level pieces that must stay in sync:

- `Server/` — a native C++ (IOCP-based) game server built with Visual Studio (`Server.sln`, `v143` toolset, x64/x86, Debug/Release).
- `UEClient/JM/` — an Unreal Engine 5.6 client project (`JM.uproject`).
- `Common/Protocol/` — the single source of truth for the network protocol (Protobuf `.proto` files), which is code-generated and copied into both `Server` and `UEClient` (see below).

There is no shared build system across these three — each is built independently with its own toolchain.

## Build & run

### Protocol (must be regenerated whenever a `.proto` file changes)

From `Common/Protocol/`, run `GanPackets.bat`. This invokes the bundled `protoc.exe` against `Enum.proto`, `Struct.proto`, and `Protocol.proto`, then copies the generated `.pb.h`/`.pb.cc` files into:
- `Server/Server/Protocol/`
- `UEClient/JM/Source/JM/Protocol/`
- `Common/Protocol/` itself

Both the server and the UE client compile their own copy of the generated protobuf sources — there is no shared build reference, so regenerating and letting the `.bat` re-copy is the only way to keep them in sync. Never hand-edit the `.pb.h`/`.pb.cc` files.

### Server

Open `Server/Server.sln` in Visual Studio 2022 (or use `msbuild`) and build one of the three projects:
- `GameServer` (`Server/Server/Server.vcxproj`) — the actual game server, entry point `gameserver.cpp`.
- `DummyClient` (`Server/DummyClient/`) — a headless load-test/bot client that speaks the same protocol, useful for testing server logic without the UE client.
- `NavGenerator` (`Server/NavGenerator/`) — offline tool that builds Recast/Detour navmesh data (`NavMesh_Output.obj`) consumed by the server at runtime via `NavmeshManager`.

Third-party libs (Recast/Detour, Protobuf, ODBC via `sql.h`/`sqlext.h`) live under `Server/ThirdParty/`. There is no automated test suite for the server — verification is done by building and running `GameServer` against `DummyClient` or the UE client.

### UE Client

Open `UEClient/JM/JM.uproject` in Unreal Editor 5.6 (or generate/build via UBT). The `JM` module depends on `ProtobufCore` (a thin module wrapping the vendored `libprotobuf.lib` under `UEClient/JM/Source/ProtobufCore/`) for talking to the server, plus standard `Sockets`/`Networking`/`UMG`/`EnhancedInput`.

## Server architecture

The server is a single-process, multi-threaded IOCP server modeled as: network I/O → per-connection `Session` → `PacketHandler` → game logic dispatched as `Job`s onto per-owner `JobQueue`s.

- **IOCP core** (`IocpCore`, `IocpEvent`, `Listener`, `ServerService`): raw accept/recv/send completion loop. `worker_thread` in `gameserver.cpp` just spins calling `IocpCore::Dispatch()`; there's no logic in the network threads themselves.
- **Job/JobQueue pattern** (`Job.h`, `JobQueue.h`): this is the server's concurrency model. Any object that needs to be accessed safely from multiple IOCP worker threads (a `Room`, in practice) owns a `JobQueue` and only mutates its own state inside a `Job` executed off that queue (`Room::PushJob` / `Room::ReserveJob`). `JobQueue` itself is an `IocpObject` — pushing a job posts a completion event so it gets picked up and drained (`ExecuteJobs`) by an IOCP worker thread. Never touch a `Room`'s internal state directly from outside a job — route through `PushJob`/`ReserveJob`.
- **Sessions & packets**: `Session` owns `RecvBuffer`/`SendBuffer`. `PacketHandler::ProcessPacket` parses raw bytes into a Protobuf message keyed by `PacketId` (see `Common/Protocol/Enum.proto`) and dispatches to a `Handle_CS_*` function via the `GPacketHandler` table.
- **World model**: `RoomManager` owns one or more `Room`s (currently a single room is created at startup). Each `Room` owns its own `GameMap` (a uniform grid of `Cell`s used for area-of-interest / view culling — see `ViewUpdate`, `Room::UpdateView`, `BroadcastAOI`) and its own `NavmeshManager` (Recast/Detour navmesh, built offline by `NavGenerator`) for movement validation and monster pathing.
- **Entities**: `GameObject` → `Character` → `Player` / `Monster`. Combat resolution goes through `CombatProcessor` and `Room::CharacterAttack` / `ExecuteSkillHit` / `ApplyDelayedDamage`, with skill/character tuning data in `ServerData.h` (`DataManager`, hardcoded `_skillTable`/`_characterTable` — not yet database- or datatable-driven).
- **Persistence**: `DBConnection`/`DBConnectionPool`/`DatabaseWorker` wrap raw ODBC (`sql.h`/`sqlext.h`) calls to SQL Server; `DatabaseWorker` runs DB work asynchronously off a dedicated queue rather than blocking IOCP threads.
- **Auth/lobby**: `AuthLobby` handles login/signup before a `Player` is handed off into a `Room`.
- **`pch.h`** is the project-wide precompiled header — it pulls in the Windows/Winsock/ODBC/DirectXMath headers, the generated `Protocol.pb.h`, and the global singletons (`GRoomManager`, `GTimer`, `GDBWorker`, `GLobby`) declared as `extern` there and defined in `gameserver.cpp`.

## Protocol conventions

- `Enum.proto` defines `PacketId` (the wire discriminator, `CS_*` = client→server starting at 1000, `SC_*` = server→client starting at 2000) plus shared gameplay enums (`MoveState`, `ObjectType`, `PlayerType`, `EquipPart`).
- `Struct.proto` defines shared value types (`PositionInfo`, `StatInfo`, `ObjectInfo`) reused across multiple packets.
- `Protocol.proto` defines the actual packet messages, grouped by direction (`CS_*_PACKET` / `SC_*_PACKET`).
- When adding a new packet: add the enum value in `Enum.proto`, the message in `Protocol.proto`, run `GanPackets.bat`, then implement `Handle_CS_*` in `Server/Server/PacketHandler.cpp` (registered in `PacketHandler::Init()`) and the corresponding handling in the UE client's `FPacketHandler`/`NetworkManager`.

## UE client architecture (`UEClient/JM/Source/JM/`)

- `Network/` — `NetworkManager` (owns the socket/connection), `NetworkWorker` (recv thread), `NetworkSession`, `FPacketHandler` (dispatches incoming `SC_*` packets, mirrors the server's `PacketHandler` role), `SendBuffer`.
- `Protocol/` — generated Protobuf sources copied in by `GanPackets.bat`; do not edit by hand.
- `Game/` — `JMGameInstance` (top-level persistent state), `JMObjectManager` (maps server object IDs to client actors), `JMAssetDataManager`/`EquipAssetData` (appearance/equipment lookup tables), `ModularAppearanceComponent`/`ModularEquipmentComponent` (drive the modular character's visual parts from equip data), `IngameGameModeBase`, `LoginGameMode`.
- `Character/`, `Animation/`, `Player/` — `JMCharacterBase`, `JMAnimInstance`, `JMPlayer`/`JMMyPlayer` (locally-controlled vs. remote), `JMPlayerController`, `NpcCharaceter` (server-driven NPCs/monsters).
- `UI/` — `JMUIManager` (central UI/widget-stack manager), `LoginWidget`, `ChatWidget`, `HpBarWidget`.

The client is intentionally thin on gameplay authority: position, stats, combat outcomes, and equip state are all server-driven via `ObjectInfo`/`SC_*` packets; client-side components mostly reflect that state rather than compute it.

## Language note

Source comments and commit messages in this repo are frequently in Korean; match that when editing existing files with Korean comments.
