# CLAUDE.md

이 문서는 Claude Code(claude.ai/code)가 이 저장소에서 작업할 때 참고하는 최상위 가이드입니다. 각 하위 프로젝트의 세부 내용은 아래 링크된 문서를 참고하세요.

## 프로젝트 개요

JH_MMO는 자체 제작 MMORPG 게임 프로젝트이며, 서로 동기화 상태를 유지해야 하는 3개의 최상위 구성 요소로 이루어져 있습니다.

- `Server/` — IOCP 기반 네이티브 C++ 게임 서버 (Visual Studio, `Server.sln`, v143 툴셋, x64/x86, Debug/Release). 자세한 내용은 [Server/CLAUDE.md](Server/CLAUDE.md) 참고.
- `UEClient/JM/` — 언리얼 엔진 5.6 클라이언트 프로젝트 (`JM.uproject`). 자세한 내용은 [UEClient/JM/CLAUDE.md](UEClient/JM/CLAUDE.md) 참고.
- `Common/Protocol/` — 네트워크 프로토콜(Protobuf `.proto` 파일)의 단일 진실 공급원(source of truth)이며, 코드 생성 후 `Server`와 `UEClient` 양쪽에 복사됩니다. 자세한 내용은 [Common/Protocol/CLAUDE.md](Common/Protocol/CLAUDE.md) 참고.

세 구성 요소는 공유 빌드 시스템이 없습니다 — 각각 독립적인 툴체인으로 빌드됩니다.

## 세 구성 요소를 모두 가로지르는 규칙

- **`.proto` 파일을 수정했다면 반드시 [Common/Protocol/CLAUDE.md](Common/Protocol/CLAUDE.md)의 절차대로 `GanPackets.bat`을 실행**해야 합니다. 생성된 `.pb.h`/`.pb.cc`는 `Server`와 `UEClient` 양쪽에 자동 복사되므로, 재생성을 빠뜨리면 서버-클라이언트 프로토콜이 조용히 어긋납니다(silent desync). 생성된 파일은 절대 손으로 수정하지 마세요.
- 새 패킷을 추가하는 전체 흐름(Enum → Protocol → 코드 생성 → 서버 핸들러 → 클라이언트 핸들러)은 [Common/Protocol/CLAUDE.md](Common/Protocol/CLAUDE.md)에 정리되어 있습니다.
- **런타임 상태 동기화는 항상 서버가 권위(authority)를 가집니다.** 위치, 스탯, 전투 결과, 장비 상태는 서버에서 계산되어 `ObjectInfo`/`SC_*` 패킷으로 클라이언트에 전달되며, 클라이언트는 그 값을 반영(reflect)할 뿐 직접 계산하지 않습니다. 서버 쪽 매커니즘(AOI/`BroadcastAOI`)은 [Server/CLAUDE.md](Server/CLAUDE.md), 클라이언트 쪽 반영 지점은 [UEClient/JM/CLAUDE.md](UEClient/JM/CLAUDE.md) 참고.

## 설계 문서

진행 중인 기능 설계는 `Docs/`에 별도로 기록합니다 — 완료된 구현이 아니라 목표 아키텍처이며, 마이그레이션 순서는 문서에 명시된 범위 내에서만 정해져 있습니다.

- [Docs/DESIGN_CHARACTER_EQUIPMENT.md](Docs/DESIGN_CHARACTER_EQUIPMENT.md) — 플레이어 캐릭터 베이스를 매니퀸(Manny/Quinn)으로 통일하는 전환, 장비(`EEquipPart`)/외형(`EAppearancePart`) 데이터 모델과 스탯 필드 추가 계획.
- [Docs/DESIGN_ANIMATION.md](Docs/DESIGN_ANIMATION.md) — 무기별 애니메이션을 Animation Layer Interface 구조로 전환하는 목표 아키텍처.
- [Docs/DESIGN_COMBAT_SKILL.md](Docs/DESIGN_COMBAT_SKILL.md) — 스킬을 무기 타입에 종속시키고 타겟팅 타입(Targeted/NonTargeted)을 추가하는 목표 구조, 쿼터뷰 조준 입력 설계.

## 언어 관련 참고사항

이 저장소의 소스 코드 주석과 커밋 메시지는 한국어로 작성된 경우가 많습니다. 기존 파일을 수정할 때는 그 파일의 언어(한국어 주석)에 맞춰 작성하세요.
