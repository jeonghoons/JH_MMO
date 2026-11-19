# 전투/스킬 시스템 목표 구조

이 문서는 스킬 시스템의 **목표 아키텍처**만 기록합니다. 현재 구조에서 이 목표로 옮기는 마이그레이션 순서/시점은 정하지 않으며, 실제 작업 시점에 판단합니다.

## 현재 상태 (2026-11-07 MC3.1 완료 시점)
- 서버는 `Common/DataTable/SkillTable.csv`를 읽고(`DataManager`), 플레이어 스킬은 **`WeaponType` + `Slot`**(`DataManager::GetSkillBySlot`), 몬스터 스킬은 `MonsterTable.SkillId`로 찾습니다(`Character::GetSlotSkill` — `Player`/`Monster`가 각각 구현). 클라이언트는 슬롯만 보내고 서버가 해석합니다.
- 행: 1001(맨손), 2001/2002(검), 3001/3002(활), 9001(몬스터). 타겟팅 타입·지면 장판·행동 시간(`AnimMs`)·공용 행동 잠금은 구현되어 있습니다.
- 장착 시스템(MC3.2) 전이라 플레이어의 무기 타입은 항상 `Unarmed`입니다. 피해 공식(`BaseDamage` + 공격력)은 MC3.1b에서 들어갔고, 플레이어 공격력은 무기 보너스가 생기는 MC3.2부터 0이 아니게 됩니다.
- (2026-10-28 A2.2 완료 시점에는 `PlayerType`(영웅) + `Slot`으로 찾았고 행은 301/401/402/501/502였습니다.)

## 목표 구조

### 스킬의 종속 대상: 무기 타입 (2026-11-04, Phase MC)
- 직업이 없으므로 스킬은 **장착 무기 타입**에 종속됩니다: `Unarmed` / `Sword` / `Bow`, 몬스터는 `Monster`. SkillTable의 `PlayerType` 열을 `WeaponType` 열로 바꿉니다(MC3.1). 2026-10-17 "스킬은 영웅에 종속"과 2026-10-01 "맨손은 공격/스킬 불가"를 대체합니다.
- 무기당 **기본 공격(슬롯 0) + 스킬 1개(슬롯 1)**로 시작하고, 슬롯 2~5는 CSV 행만 추가해 확장합니다(클라이언트 입력·HUD도 슬롯 배열). 맨손은 기본 공격만 있습니다.
- SkillTable ID 앞자리 = 무기 종류: 맨손 1xxx, 검 2xxx, 활 3xxx, 몬스터 9xxx. 기존 수치를 그대로 옮깁니다.
  | 새 ID | 기존 | 내용 |
  |---|---|---|
  | 1001 | — | 맨손 기본 공격: 검 기본 공격의 약 60%, 사거리(반경) 150 |
  | 2001 / 2002 | 401 / 402 | 검 기본 공격(논타겟 부채꼴) / 광역 강타 |
  | 3001 / 3002 | 501 / 502 | 활 기본 공격(타겟팅 투사체) / 지면 장판 |
  | 9001 | 301 | 몬스터 공격 — `MonsterTable.SkillId`(필수)로 연결 |
- 서버 조회: 캐릭터의 현재 무기 타입 + 요청 슬롯 → 스킬. 무기를 바꾸면 쓸 수 있는 스킬 목록이 바뀌고, `MovementTable.ServerWeaponSwapLockMs` 동안 시전이 막힙니다(착용·해제 모두, MC3.2 구현)([DESIGN_CHARACTER_EQUIPMENT.md](DESIGN_CHARACTER_EQUIPMENT.md)).

### 피해 공식 (2026-11-04)
- **피해 = 스킬 `BaseDamage` + 공격력(`AttackDamage` = 기본 0 + 무기 보너스)**. 시전 순간에 확정해 즉시 판정·투사체·장판에 모두 같은 값을 씁니다(시전 후 무기를 바꿔도 이미 나간 공격은 그대로).
- 체형(MALE/FEMALE) 기본 스탯은 같습니다(PlayerStatTable은 레벨별·체형 공용). 검·활의 공격 속도 차이는 모션 길이(`AnimMs`)가 만듭니다.
- (2026-11-07 MC3.1b 구현) 플레이어·몬스터가 **같은 공식**을 씁니다. 몬스터 공격력은 장비 없이 `MonsterTable.AttackDamage`로 **고정**입니다(9001 = BaseDamage 40 + 20 = 60). 서버 `Room::CastSkill`이 피해를 계산해 `ExecuteSkillHit` → `CombatProcessor::ProcessSkillHit` → 장판(`CreateZone`)까지 그 값을 넘깁니다.
- 방어력(받는 피해 감소, 예: 피해 − 방어력)은 나중에(A2.3 무렵) 넣습니다. 피해가 적용되는 곳이 `Character::OnDamaged` 한 곳입니다.

### 타겟팅 타입 추가
`SkillData`에 새 축을 추가합니다:
- `ESkillTargetingType{Targeted, NonTargeted}` — 기존 `HitShape`(판정 모양)와는 별개 축입니다. Targeted는 마우스 커서 아래 대상(액터)을 지정하고, NonTargeted는 방향/위치를 지정합니다.
- 기본 공격은 무기 타입별로 다릅니다: 맨손·검 = NonTargeted(카메라 방향), 활 = Targeted(크로스헤어 아래 몬스터). 스킬은 데이터별로 Targeted/NonTargeted가 혼재합니다. (2026-10-20 결정, 2026-11-04 영웅 → 무기 타입)
- HitShape에 지면 장판(Zone)을 추가합니다: 시전 지점(사거리 안)에 원형 영역을 만들고 DurationMs 동안 TickMs마다 영역 안의 적대 대상에게 피해를 줍니다. 활 1번 스킬(3002)이 첫 사용처입니다.

### 클라이언트 입력/조준 설계 (검은사막 방식, 2026-10-21)
- 카메라는 3인칭 마우스 회전, 전투 중 마우스 커서는 숨김. `Left Alt` 을 누르는 동안 자유 커서(UI 클릭), 인벤토리 같은 창이 열려 있으면 커서가 유지됩니다. WASD는 카메라 기준 이동입니다.
- 조준은 **화면 중앙(크로스헤어)** 기준입니다.
  - 맨손·검(옛 전사): 일반공격·1번 스킬 모두 논타겟 — 카메라가 바라보는 방향(yaw)으로 시전합니다. 시전할 때 캐릭터가 그 방향을 봅니다.
  - 활(옛 궁수): 일반공격은 타겟팅 — 크로스헤어 아래(약간의 허용 반경)의 몬스터를 지정하고, 없으면 아무 일도 하지 않습니다. 1번 스킬은 논타겟 지면 지정 — 크로스헤어가 가리키는 지면 지점에 장판을 놓고, 사거리를 넘으면 사거리 끝으로 제한합니다.
- 좌클릭 = 일반공격 / 조준 중에는 시전 확정. 1번 키 = 논타겟 스킬 조준 모드 진입(범위 인디케이터 표시). 우클릭 = 조준 취소.
- ~~조준/범위조정 단계는 클라이언트 로컬 전용이며 서버에 아무것도 전송하지 않습니다. 캐스팅 중 이동속도 제한은 없습니다.~~ → **2026-11-08 번복, 2026-11-09 세부 확정(MC4.2d-1, `DECISIONS.md`)**: 스킬 조준(숫자키 → 범위 선택) 중에는 **느리게 이동**하고 다른 사람에게도 보이므로, 조준 켜기/끄기를 상태가 바뀔 때만 서버에 보냅니다(`CS_SKILL_AIM{slot, active, seq}` → `SC_SKILL_AIM` AOI 방송, 늦게 보인 캐릭터는 `ObjectInfo.aim_skill_id`).
  - 서버: 켜기 검증(생존·조준 가능한 스킬 `AimMaxMs > 0`·쿨타임·빈도 `ServerAimMinIntervalMs`), 이동 예산 속도 × SkillTable `AimMoveSpeedMul`, 해제 = 클라 취소·`CS_ATTACK`(검증 전, 실패해도)·사망·무기 교체·`AimMaxMs` 초과. 거부·시간 초과는 본인에게 `false` + seq.
  - 클라: 내 조준 상태는 로컬이 권위(진입 전 쿨타임·사망·빈도 검사, `MaxWalkSpeed` = 스탯 × 배율, 로컬 `AimMaxMs` 초과 시 취소 전송, 서버 `false`는 seq가 같을 때만 반영). 원격은 `SC_SKILL_AIM`과 `SC_ATTACK`·`SC_DEAD`·무기 `SC_CHANGE_EQUIP`으로 반영하고, 애니메이션은 `UJMAnimInstance::bIsSkillAiming`.
- 확정 시 방향/대상/지점을 서버로 1회 전송하고, 서버는 이 값을 신뢰하지 않고 사거리·대상 유효성을 다시 검증합니다(변경 없음).
- **선반영(2026-11-19 CP1, D-211~D-213)**: 내 스킬·공격은 서버 답을 기다리지 않고 입력 즉시 시작합니다 — 몽타주·무기 꺼내기·쿨타임 표시·스킬 이펙트·이동 잠금·방향 고정(`AJMMyPlayer::CastSkillSlot`). 피해·HP·사망·피격 연출은 서버 패킷이 와야 보입니다.
  - 요청 순번: `CS_ATTACK.seq`(1부터 증가)를 서버가 `SC_ATTACK.seq`(확정)나 `SC_ATTACK_RESULT.seq`(거부)로 돌려줍니다. 클라이언트는 응답을 기다리는 시전을 하나만 두고(`FPendingCast`), 응답 전에는 다음 시전을 보내지 않습니다(2초 안에 응답이 없으면 대기만 풂).
  - 확정(`ConfirmCast`): 순번이 있는 내 `SC_ATTACK`은 다시 재생하지 않습니다. 서버가 장판 지점을 다르게 보정했으면 이펙트만 옮기고, 서버가 다른 스킬로 확정했으면(무기가 서로 달랐던 순간) 먼저 시작한 것을 되돌리고 서버 값으로 재생합니다.
  - 거부(`RollbackCast`): `SC_ATTACK_RESULT{result, seq, skill_slot, remain_ms}`는 거부일 때만 본인에게 옵니다. 순번이 기다리는 시전과 같으면 먼저 재생한 몽타주를 멈추고(그 몽타주만), 이펙트·쿨타임 표시·이동 잠금·방향 고정·범위 표시를 시전 전으로 되돌립니다. `ACTING`·`COOLDOWN`은 `remain_ms`로 로컬 시각을 서버에 맞춥니다.
  - 사전 검사(`CanCastLocally`): 사망·앞 동작 중(`AnimMs ÷ 재생 배율`)·쿨타임·응답 대기 중이면 보내지 않습니다. 서버만 아는 상태(피격 경직, 무기 교체 잠금, 대상이 방금 죽음)는 거부로 옵니다.
  - 허용 오차: 클라이언트는 누른 시각부터, 서버는 받은 시각부터 재므로 지연이 흔들리면 다음 요청이 조금 일찍 도착합니다. 서버는 `[Network] AttackEarlyToleranceMs`(100ms) 이하로 남은 앞 동작 잠금·스킬 쿨타임은 통과시키고 남아 있던 시간을 다음 잠금·쿨타임에 더합니다(공격 주기는 그대로). 무기 교체 잠금·피격 경직·몬스터에는 적용하지 않습니다.

### 서버 검증
- 서버가 사거리/쿨다운/자원을 재검증한 뒤, **영향받는 대상을 서버가 직접 산출**합니다(클라이언트가 보낸 대상/히트 리스트는 신뢰하지 않습니다).
- 새 패킷(스킬 시전 요청 등)이 필요하면 [Common/Protocol/CLAUDE.md](../Common/Protocol/CLAUDE.md)의 절차(Enum.proto → Protocol.proto → `GanPackets.bat` → 서버/클라이언트 핸들러)를 따릅니다.
- **대상 몸 반경(2026-11-18 MN1, D-198)**: 판정 범위에 맞는 대상의 몸 반경 `Character::GetBodyRadius()`를 더합니다(플레이어는 0 = 중심점, 몬스터는 `MonsterTable.BodyRadiusCm` = 기준 캡슐 반경 42cm × `Scale`). 부채꼴은 반경 + 몸 반경, 직선·투사체는 폭 + 몸 반경 × 2·사거리 + 몸 반경, 장판(`Room::UpdateZones`)은 반경 + 몸 반경, 대상 지정 시전 사거리(`Room::CastSkill`)는 `CastRange × 1.1 + 몸 반경`입니다. 그래서 큰 몬스터일수록 더 멀리서 맞습니다(검 기본 공격 190 → 하급 219, 중급 236, 보스 249cm).
- **공격 중 이동 잠금(2026-11-18 CB1, D-199)**: 서버는 판정 시점(시전 뒤 `HitDelayMs`)의 시전자 위치에서 시전 때 확정한 방향으로 판정하므로, 플레이어의 모든 공격·스킬은 시전부터 휘두르기가 끝날 때(`SkillTable.MoveLockMs` — 몽타주의 `UAnimNotify_SwingEnd` 시각, 기본 공격은 `AttackSpeed`로 나눔)까지 이동·몸 방향을 잠급니다. 서버는 시전 확정 때 `Player::BeginMoveLock`으로 잠금 끝 시각을 정하고 잠금 동안 받는 이동 합계를 `MovementTable.ServerMoveLockSlackCm`(120cm)까지만 인정하며(넘으면 보정), 클라이언트(`AJMMyPlayer`)는 이동 입력을 받지 않고 즉시 멈춰 멈춘 자리를 서버에 알립니다. 시전부터 판정 시점까지 판정 범위를 바닥에 그립니다(`DrawAimIndicator`, 임시 디버그 도형·내 화면만).

### 플레이어 사망·부활 (2026-11-16, A2.4, D-149)
- 사망: HP가 0이 되면 서버가 `SC_DEAD{object_id, revive_delay_ms}`를 AOI에 방송합니다(`revive_delay_ms` = 플레이어의 부활 대기 시간, 몬스터는 0). 사망 상태는 전용 필드 없이 `stat.hp == 0`으로 판정하고, 시체는 방과 AOI에 그대로 남습니다(늦게 본 사람은 `SC_ADD_OBJECT`의 hp 0을 보고 바로 사망 자세).
- 사망 중 요청: 이동은 조용히 버림, 공격·장비 교체(`EQUIP_RESULT_DEAD`)·조준 켜기는 거부, 채팅·핑·조준 끄기는 허용. 클라이언트도 이동·공격 입력을 막습니다.
- 부활(요청형): 사망 후 `WorldTable.ReviveDelaySec`가 지나면 클라이언트가 부활 버튼(`WBP_Revive`)으로 `CS_REVIVE`를 보냅니다. 서버는 사망 여부와 대기 시간(사망 시각 비교, 타이머 없음)을 검증하고, 통과하면 입장 때 배치된 자리(플레이어 = 맵의 첫 PlayerStart, 더미 봇 = 입장 때 뽑힌 자리)로 옮겨 HP를 최대치 × `WorldTable.ReviveHpPercent`로 채우고, 이동 보정 순번을 올린 뒤(늦게 온 옛 이동을 버리기 위해) `SC_REVIVE{object_id, pos_info, hp, correction_seq}`를 AOI와 본인에게 보냅니다. 살아 있거나 대기 시간 전이거나 중복인 요청은 조용히 버립니다. 패널티는 없습니다.
- 사망 중 접속을 끊고 다시 들어오면 살아 있는 상태로 입장합니다(DB HP 저장은 A2.5에서 재검토).
- 클라이언트 연출: 사망 몽타주(BodyTable `DeathMontage`)를 재생하고 끝 자세에서 멈춤, 부활 때 몽타주를 풀고 서버 위치로 순간이동합니다. AnimBP `bIsDead` 상태 전환은 하지 않습니다(백로그).

## 알아둘 간극
- 스킬 조회 키가 `PlayerType`에서 "현재 무기 타입"으로 바뀌므로, 캐릭터가 장착 무기 타입을 들고 있어야 하고 `CombatProcessor`/`Room::CastSkill`(옛 이름 `CharacterAttack`)의 조회부가 바뀝니다(MC3.1~MC3.2).
- 몬스터는 `PlayerType`이 아니라 `MonsterTable.SkillId`로 스킬을 찾게 됩니다.

## Test 프로젝트 병합 시 서버 권위 전환 방침
`UEProjects/Test`(클라이언트 테스트 공간)는 서버 없이 `IGameBackend`/`ULocalGameBackend`로 서버 동작을 흉내 냈습니다. 병합 시 "나중에 서버가 할 일"로 설계된 동작은 모두 이 저장소의 서버 권위 동작(CS_ 패킷 → 서버 검증/계산 → SC_ 패킷 반영)으로 바꿉니다.

- 스킬/공격 시전(`RequestAttack`/`RequestCastSkill`) → CS_ 패킷. 사거리/쿨다운은 서버가 재검증합니다.
- Test의 "클라이언트가 AnimNotify 시점에 판정하고 맞은 대상을 보고"하는 방식은 채택하지 않습니다. 클라이언트는 AnimNotify로 시전·연출 타이밍만 맡고, 대상 산출·데미지·사망은 서버(`CombatProcessor`, `Room::CastSkill`)가 결정합니다.
- 스폰/데미지/사망(`OnSpawnMonster`/`OnDamage`/`OnMonsterDead`) → 기존 SC_ 패킷과 `FPacketHandler`.
- 아이템 드랍/줍기/장착(`RequestPickupItem`/`RequestEquip`/`OnDropItem`/`OnEquipChanged`) → 신규 CS_/SC_ 패킷이 필요하며, 서버에 아이템 테이블이 아직 없으므로 별도 작업으로 다룹니다.
- 몬스터 AI/드랍 계산은 서버 책임이며 클라이언트에 만들지 않습니다.
- `IGameBackend`/`ULocalGameBackend`는 병합하지 않습니다.
- 스킬 쿨타임/데미지 등 수치의 권위는 서버(`ServerData.h`)에 있고, 클라이언트 데이터는 연출용으로 한정합니다.
