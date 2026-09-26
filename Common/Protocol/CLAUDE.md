# Common/Protocol/CLAUDE.md

이 문서는 `Common/Protocol/` 디렉토리에서 작업할 때 참고하는 가이드입니다. 프로젝트 전체 구조는 [루트 CLAUDE.md](../../CLAUDE.md)를 참고하세요.

`Common/Protocol/`은 네트워크 프로토콜(Protobuf `.proto` 파일)의 단일 진실 공급원입니다.

## 프로토콜 재생성 (`.proto` 파일을 수정할 때마다 반드시 실행)

`Common/Protocol/`에서 `GanPackets.bat`을 실행하세요. 이 배치 파일은 번들된 `protoc.exe`를 `Enum.proto`, `Struct.proto`, `Protocol.proto`에 대해 실행한 뒤, 생성된 `.pb.h`/`.pb.cc` 파일을 다음 세 곳에 복사합니다.

- `Server/Server/Protocol/`
- `UEClient/JM/Source/JM/Protocol/`
- `Common/Protocol/` 자기 자신

서버와 UE 클라이언트는 각각 생성된 protobuf 소스를 **자체적으로** 컴파일합니다 — 공유되는 빌드 참조가 없으므로, 재생성 후 이 배치 파일이 다시 복사하게 하는 것이 두 쪽을 동기화하는 유일한 방법입니다. 생성된 `.pb.h`/`.pb.cc` 파일은 절대 손으로 수정하지 마세요.

## 프로토콜 컨벤션

- `Enum.proto`는 `PacketId`(와이어 식별자, `CS_*`=클라이언트→서버는 1000부터, `SC_*`=서버→클라이언트는 2000부터 시작)와 공유 게임플레이 enum(`MoveState`, `ObjectType`, `PlayerType`, `EquipPart`)을 정의합니다.
- `Struct.proto`는 여러 패킷에서 재사용되는 공유 값 타입(`PositionInfo`, `StatInfo`, `ObjectInfo`)을 정의합니다.
- `Protocol.proto`는 방향별로 그룹화된 실제 패킷 메시지(`CS_*_PACKET` / `SC_*_PACKET`)를 정의합니다.

## 새 패킷을 추가할 때

1. `Enum.proto`에 enum 값 추가
2. `Protocol.proto`에 메시지 추가
3. `GanPackets.bat` 실행
4. `Server/Server/PacketHandler.cpp`에 `Handle_CS_*` 구현 (`PacketHandler::Init()`에 등록) — 서버 구조 세부 내용은 [Server/CLAUDE.md](../../Server/CLAUDE.md) 참고
5. UE 클라이언트의 `FPacketHandler`/`NetworkManager`에 대응하는 처리 구현 — 클라이언트 구조 세부 내용은 [UEClient/JM/CLAUDE.md](../../UEClient/JM/CLAUDE.md) 참고
