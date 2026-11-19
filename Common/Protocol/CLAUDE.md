# Common/Protocol/CLAUDE.md

이 문서는 `Common/Protocol/` 디렉토리에서 작업할 때 참고하는 가이드입니다. 프로젝트 전체 구조는 [루트 CLAUDE.md](../../CLAUDE.md)를 참고하세요.

`Common/Protocol/`은 네트워크 프로토콜(Protobuf `.proto` 파일)의 단일 진실 공급원입니다.

## 프로토콜 재생성 (`.proto` 파일을 수정할 때마다 반드시 실행)

`Common/Protocol/`에서 `GanPackets.bat`을 실행하세요. 이 배치 파일은 번들된 `protoc.exe`를 `Enum.proto`, `Struct.proto`, `Protocol.proto`에 대해 실행한 뒤, 생성된 `.pb.h`/`.pb.cc` 파일을 다음 세 곳에 복사합니다.

- `Server/Server/Protocol/`
- `UEClient/JM/Source/JM/Protocol/`
- `Common/Protocol/` 자기 자신

서버와 UE 클라이언트는 각각 생성된 protobuf 소스를 **자체적으로** 컴파일합니다 — 공유되는 빌드 참조가 없으므로, 재생성 후 이 배치 파일이 다시 복사하게 하는 것이 두 쪽을 동기화하는 유일한 방법입니다. 생성된 `.pb.h`/`.pb.cc` 파일은 절대 손으로 수정하지 마세요.

- 실행: 더블클릭은 인자 없이(끝에 `pause`). Claude·스크립트는 `cmd //c "Common\\Protocol\\GanPackets.bat nopause"`처럼 **인자를 하나 주면** `pause`를 생략합니다. bat이 스스로 자기 폴더로 이동하고 `protoc.exe`를 전체 경로로 부르므로 어느 폴더에서 실행해도 됩니다. 실패하면 종료 코드 1과 실패 메시지를 냅니다. `GanPackets.bat`은 **CP949** 파일이라 Edit 도구로 고치지 말고 바이트 단위로 고칩니다.
- 검사: `python -I Common/Protocol/check_generated.py` — `.proto`를 임시 폴더에서 다시 생성해 세 사본과 비교(줄바꿈 무시)합니다.
- 자동 방어(2026-11-12, ROADMAP W1):
  - **git pre-commit 훅** `.githooks/pre-commit` — 스테이징된 `.proto`·`.pb.*`가 서로 맞지 않으면 커밋을 거부합니다(재생성 뒤 `git add`를 빠뜨린 경우 포함). **클론마다 한 번** `git config core.hooksPath .githooks`를 실행해야 켜집니다.
  - **Claude Code Stop 훅**(`.claude/settings.json`) — Claude가 응답을 끝낼 때 작업 폴더의 생성 코드가 낡았으면 종료를 막고 재생성하라는 메시지를 돌려줍니다. 프로토콜 파일이 HEAD와 같으면 protoc를 돌리지 않습니다. 여러 `.proto`를 차례로 고치는 도중에는 검사하지 않습니다(PostToolUse를 쓰지 않는 이유).

## 프로토콜 컨벤션

- `Enum.proto`는 `PacketId`(와이어 식별자, `CS_*`=클라이언트→서버는 1000부터, `SC_*`=서버→클라이언트는 2000부터 시작)와 공유 게임플레이 enum(`MoveState`, `ObjectType`, `PlayerType`, `EquipPart`)을 정의합니다.
- `Struct.proto`는 여러 패킷에서 재사용되는 공유 값 타입(`PositionInfo`, `StatInfo`, `ObjectInfo`)을 정의합니다.
- `Protocol.proto`는 방향별로 그룹화된 실제 패킷 메시지(`CS_*_PACKET` / `SC_*_PACKET`)를 정의합니다.

## 새 패킷을 추가할 때

1. `Enum.proto`에 enum 값 추가
2. `Protocol.proto`에 메시지 추가
3. `GanPackets.bat` 실행
4. `Server/Server/Packet/PacketHandler.cpp`에 `Handle_CS_*` 구현 (`PacketHandler::Init()`에 등록, 송신 패킷 생성은 같은 폴더 `PacketSerializer`) — 서버 구조 세부 내용은 [Server/CLAUDE.md](../../Server/CLAUDE.md) 참고
5. UE 클라이언트의 `FPacketHandler`/`NetworkManager`에 대응하는 처리 구현 — 클라이언트 구조 세부 내용은 [UEClient/JM/CLAUDE.md](../../UEClient/JM/CLAUDE.md) 참고
