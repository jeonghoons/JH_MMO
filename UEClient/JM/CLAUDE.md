# UEClient/JM/CLAUDE.md

이 문서는 `UEClient/JM/` 디렉토리에서 작업할 때 참고하는 가이드입니다. 프로젝트 전체 구조는 [루트 CLAUDE.md](../../CLAUDE.md)를 참고하세요.

## 빌드 & 실행

언리얼 에디터 5.6에서 `UEClient/JM/JM.uproject`를 열거나(또는 UBT로 생성/빌드) 사용합니다. `JM` 모듈은 서버와 통신하기 위해 `ProtobufCore`(`UEClient/JM/Source/ProtobufCore/`에 벤더링된 `libprotobuf.lib`를 감싸는 얇은 모듈)에 의존하며, 그 외 표준 `Sockets`/`Networking`/`UMG`/`EnhancedInput`도 사용합니다.

에디터 없이 컴파일만 확인하려면 UBT를 커맨드라인으로 사용합니다 (`<UE_INSTALL_DIR>`은 로컬 언리얼 엔진 5.6 설치 경로로 바꿔서 사용하세요):

```
"<UE_INSTALL_DIR>\Engine\Build\BatchFiles\Build.bat" JMEditor Win64 Development -Project="<repo>\UEClient\JM\JM.uproject" -WaitMutex
```

프로토콜(생성된 Protobuf 소스)은 `UEClient/JM/Source/JM/Protocol/`에 복사되어 들어옵니다. 이 파일들은 [Common/Protocol/CLAUDE.md](../../Common/Protocol/CLAUDE.md)에서 관리하며, 절대 손으로 수정하지 마세요.

## UE 클라이언트 아키텍처 (`UEClient/JM/Source/JM/`)

- `Network/` — `NetworkManager`(소켓/연결 소유), `NetworkWorker`(수신 스레드), `NetworkSession`, `FPacketHandler`(수신되는 `SC_*` 패킷 디스패치, 서버의 `PacketHandler`와 대응되는 역할), `SendBuffer`.
- `Protocol/` — `GanPackets.bat`으로 복사되는 생성된 Protobuf 소스. 손으로 수정 금지.
- `Game/` — `JMGameInstance`(최상위 영속 상태), `JMObjectManager`(서버 오브젝트 ID ↔ 클라이언트 액터 매핑), `JMAssetDataManager`/`EquipAssetData`(외형/장비 조회 테이블), `ModularAppearanceComponent`/`ModularEquipmentComponent`(장비 데이터로 모듈러 캐릭터의 외형 파츠를 구동), `IngameGameModeBase`, `LoginGameMode`.
- `Character/`, `Animation/`, `Player/` — `JMCharacterBase`, `JMAnimInstance`, `JMPlayer`/`JMMyPlayer`(로컬 제어 vs 리모트), `JMPlayerController`, `NpcCharaceter`(서버 주도 NPC/몬스터).
- `UI/` — `JMUIManager`(중앙 UI/위젯 스택 매니저), `LoginWidget`, `ChatWidget`, `HpBarWidget`.

클라이언트는 의도적으로 게임플레이 권한이 얇습니다(thin client) — 위치, 스탯, 전투 결과, 장비 상태는 모두 `ObjectInfo`/`SC_*` 패킷을 통해 서버가 주도하며, 클라이언트 컴포넌트는 대부분 그 상태를 계산하기보다는 반영(reflect)만 합니다.

## 네이밍 컨벤션

`JM`은 프로젝트명(J=개발자 이름, M=MMORPG)이자 클래스 접두사입니다. **언리얼 엔진에 이미 존재하는 이름과 겹치는 클래스/함수에만** `JM` 접두사를 붙이고(`JMPlayer`, `JMCharacterBase`, `JMGameInstance`, `JMAnimInstance` 등), 겹치지 않는 이름은 접두사 없이 짓습니다.

## 설계 문서

- [Docs/DESIGN_CHARACTER_EQUIPMENT.md](../../Docs/DESIGN_CHARACTER_EQUIPMENT.md) — 플레이어 캐릭터를 Greystone/Rampage/Sparrow에서 매니퀸(Manny/Quinn)으로 전환하는 계획, `EquipAssetData`/`ModularEquipmentComponent` 관련 확장(스탯 필드 등).
- [Docs/DESIGN_ANIMATION.md](../../Docs/DESIGN_ANIMATION.md) — `JMCharacterBase::UpdateWeaponAnimation`의 AnimInstance 전체 교체 방식을 Animation Layer Interface로 리팩토링하는 목표 구조.
- [Docs/DESIGN_COMBAT_SKILL.md](../../Docs/DESIGN_COMBAT_SKILL.md) — 쿼터뷰 조준 입력(1번 키 논타겟 스킬 조준, 좌클릭 시전) 및 스킬 캐스팅 패킷 설계.
