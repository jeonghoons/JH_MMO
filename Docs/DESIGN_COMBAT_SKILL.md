# 전투/스킬 시스템 목표 구조

이 문서는 스킬 시스템의 **목표 아키텍처**만 기록합니다. 현재 구조에서 이 목표로 옮기는 마이그레이션 순서/시점은 정하지 않으며, 실제 작업 시점에 판단합니다.

## 현재 상태
- `Server/Server/ServerData.h`의 `SkillData`는 **캐릭터 타입당 스킬 1개**로 하드코딩되어 있습니다(`_skillTable[301]`, `[401]`, `[501]` 등, typeId ↔ skillId 매핑).
- `HitShape{Sector,Line,Projectile}`만 존재합니다 — 판정 "모양"만 표현하고, 대상을 누가/어떻게 지정하는지(타겟팅) 개념이 없습니다.
- `Character::Attack(int& skillId)`에 타겟(대상) 정보가 없습니다.

## 목표 구조

### 스킬의 종속 대상 변경
- 스킬은 **영웅에 종속**됩니다(2026-10-17 결정, 무기가 영웅에 고정되었으므로 무기 종속 안을 대체). `SkillTable`의 각 행이 `HeroId`를 가지며, 영웅 한 행에 스킬 목록이 연결됩니다([DESIGN_CHARACTER_EQUIPMENT.md](DESIGN_CHARACTER_EQUIPMENT.md)).
- 기본 공격도 스킬 목록의 하나로 통합됩니다(영웅별 "기본 공격" 스킬 포함).

### 타겟팅 타입 추가
`SkillData`에 새 축을 추가합니다:
- `ESkillTargetingType{Targeted, NonTargeted}` — 기존 `HitShape`(판정 모양)와는 별개 축입니다. Targeted는 마우스 커서 아래 대상(액터)을 지정하고, NonTargeted는 방향/위치를 지정합니다.
- 기본 공격(우클릭)은 Targeted입니다. 스킬은 데이터별로 Targeted/NonTargeted가 혼재합니다.

### 클라이언트 입력/조준 설계 (쿼터뷰)
- WASD 이동, 우클릭 = 기본 공격(조준 중이면 캐스팅 취소).
- 1번 키 = 논타겟 스킬 조준 모드 진입 — 범위 인디케이터 표시, 마우스 회전으로 방향/범위 조정.
- 좌클릭 = 조준된 스킬 시전 확정.
- **조준/범위조정 단계는 클라이언트 로컬 전용이며 서버에 아무것도 전송하지 않습니다.** 좌클릭 확정 시에만 최종 위치/방향 데이터를 서버로 1회 전송합니다.
- 캐스팅 중 이동속도 제한은 없습니다 — 이동과 조준이 서로 간섭하지 않으므로 이동 동기화 로직(서버 계산 → 클라이언트 반영) 변경이 필요 없습니다.

### 서버 검증
- 서버가 사거리/쿨다운/자원을 재검증한 뒤, **영향받는 대상을 서버가 직접 산출**합니다(클라이언트가 보낸 대상/히트 리스트는 신뢰하지 않습니다).
- 새 패킷(스킬 시전 요청 등)이 필요하면 [Common/Protocol/CLAUDE.md](../Common/Protocol/CLAUDE.md)의 절차(Enum.proto → Protocol.proto → `GanPackets.bat` → 서버/클라이언트 핸들러)를 따릅니다.

## 알아둘 간극
- `Character::Attack`이 현재 타겟 정보를 전혀 다루지 않으므로, Targeted 스킬 도입 시 이 인터페이스 자체를 확장해야 합니다.
- 스킬을 영웅 종속으로 옮기면 "캐릭터 타입당 스킬 1개"였던 `_skillTable`의 키 구조가 "영웅 → 스킬 목록"으로 바뀌어야 합니다 — 이 변경이 `CombatProcessor`/`Room::CharacterAttack` 등 호출부에 미치는 영향은 구현 시점에 파악합니다.

## Test 프로젝트 병합 시 서버 권위 전환 방침
`UEProjects/Test`(클라이언트 테스트 공간)는 서버 없이 `IGameBackend`/`ULocalGameBackend`로 서버 동작을 흉내 냈습니다. 병합 시 "나중에 서버가 할 일"로 설계된 동작은 모두 이 저장소의 서버 권위 동작(CS_ 패킷 → 서버 검증/계산 → SC_ 패킷 반영)으로 바꿉니다.

- 스킬/공격 시전(`RequestAttack`/`RequestCastSkill`) → CS_ 패킷. 사거리/쿨다운은 서버가 재검증합니다.
- Test의 "클라이언트가 AnimNotify 시점에 판정하고 맞은 대상을 보고"하는 방식은 채택하지 않습니다. 클라이언트는 AnimNotify로 시전·연출 타이밍만 맡고, 대상 산출·데미지·사망은 서버(`CombatProcessor`, `Room::CharacterAttack`)가 결정합니다.
- 스폰/데미지/사망(`OnSpawnMonster`/`OnDamage`/`OnMonsterDead`) → 기존 SC_ 패킷과 `FPacketHandler`.
- 아이템 드랍/줍기/장착(`RequestPickupItem`/`RequestEquip`/`OnDropItem`/`OnEquipChanged`) → 신규 CS_/SC_ 패킷이 필요하며, 서버에 아이템 테이블이 아직 없으므로 별도 작업으로 다룹니다.
- 몬스터 AI/드랍 계산은 서버 책임이며 클라이언트에 만들지 않습니다.
- `IGameBackend`/`ULocalGameBackend`는 병합하지 않습니다.
- 스킬 쿨타임/데미지 등 수치의 권위는 서버(`ServerData.h`)에 있고, 클라이언트 데이터는 연출용으로 한정합니다.
