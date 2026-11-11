---
name: gen-packets
description: .proto를 고친 뒤 Protobuf 코드를 재생성하고 세 사본이 맞는지 확인할 때 사용. 새 패킷 추가 흐름(Enum → Protocol → 생성 → 서버·클라 핸들러 → 봇)의 생성 단계.
---

# 프로토콜 재생성

1. `.proto` 수정을 모두 끝낸다(`Enum.proto` → `Struct.proto` → `Protocol.proto`). 중간 상태에서는 생성하지 않는다.
2. 실행: `cmd //c "Common\\Protocol\\GanPackets.bat nopause"` — 인자가 있으면 `pause`를 생략한다. 종료 코드가 1이면 출력의 protoc 오류(대개 `.proto` 문법)를 고치고 다시 실행한다.
3. 확인: `python -I Common/Protocol/check_generated.py` → "세 곳의 생성 코드가 모두 .proto와 맞습니다".
4. `git status`의 `.pb.*`가 "M"인데 `git diff`가 비어 있으면 줄바꿈(LF)만 바뀐 것이라 무시해도 된다.
5. 다음 단계: 서버 `Server/Server/Packet/PacketHandler.cpp`(`Handle_CS_*`, `PacketHandler::Init()` 등록, 송신은 `PacketSerializer`) → UE `FPacketHandler`/`NetworkManager` → 봇이 보내거나 받아야 하면 `Server/BotEngine/BotEngine.cpp`의 `HandlePacket`/`WorkerMain`. 서버와 UE를 모두 다시 빌드한다(`run-server`, `ue-build` 스킬).

커밋 때 생성 파일 `git add`를 빠뜨리면 pre-commit 훅이 거부하고, 낡은 채로 응답을 끝내려 하면 Stop 훅이 막는다. 상세는 `Common/Protocol/CLAUDE.md`.
