# 애니메이션 구조 (공용 ABP + 무기 타입별 레이어)

목표 구조만 기록합니다. 구현 순서는 ROADMAP Phase MC(MC4.2·MC4.3)를 따릅니다.
(2026-11-04 개정: 2026-10-17 "영웅별 AnimBP, 무기별 AnimLayer 폐기"를 번복하고 2026-10-03 AnimLayer 방침을 부활. [DECISIONS.md](DECISIONS.md) 참고)

## 구조
- 플레이어는 체형과 상관없이 **공용 AnimBP `ABP_PlayerBase`** 하나를 씁니다(Manny/Quinn 같은 스켈레톤).
- 무기 타입마다 달라지는 부분은 **Animation Layer Interface `ALI_Weapon`**으로 분리하고, 무기 타입별 레이어 AnimBP `ABP_Layer_Unarmed` / `ABP_Layer_Sword` / `ABP_Layer_Bow`가 구현합니다.
  - 레이어 범위: 대기·이동 블렌드스페이스(무기 쥔 자세), 상체 포즈. 공통 상태머신(이동/점프/사망)은 `ABP_PlayerBase`에 둡니다.
  - 레이어 이름은 `LocomotionLayer`(입력 포즈 없음, Main States의 `Locomotion` 스테이트가 사용)와 `UpperBodyLayer`(입력 포즈 있음, 상태머신 출력 뒤). 스테이트·그래프 이름과 겹치면 안 됩니다.
  - `ABP_PlayerBase` AnimGraph: Main States → `UpperBodyLayer` → 캐시 → [기본 | `Slot UpperBody`] `spine_01` 레이어 블렌드 → `Slot DefaultSlot` → Foot IK → 결과. `ABP_PlayerBase`의 레이어 기본 구현은 맨손(링크된 레이어가 없을 때).
  - 루트 모션은 쓰지 않습니다(`RootMotionMode = IgnoreRootMotion`, 위치는 서버 권위).
  - 무기 타입이 바뀌면(`SC_CHANGE_EQUIP` 반영 시) `LinkAnimClassLayers`로 해당 레이어를 연결합니다. 어떤 레이어를 쓰는지는 데이터(무기 타입 → 레이어 클래스)로 정합니다.
- 공격·스킬은 몽타주입니다. 이름은 `AM_<WeaponType>_<Action>`(예: `AM_Sword_Attack`, `AM_Bow_Skill1`), 위치는 `Content/Character/Mannequin/Anim/<WeaponType>/`. 어떤 몽타주를 쓰는지는 SkillTable의 클라 전용 `Montage` 열이 가리킵니다. 상체 슬롯 `UpperBody`(이동 중 시전)와 전신 `DefaultSlot`을 기존처럼 씁니다.
- 피격·사망 몽타주는 무기와 상관없는 공용 1벌로 시작합니다.
- 몬스터 Rampage는 기존 `ABP_Rampage`와 몽타주를 그대로 씁니다.

## 공격 타이밍
- **공격 타이밍은 C++ 타이머가 아니라 AnimNotify**입니다. 몽타주의 `AnimNotify_SkillHit` 위치를 `ExportSkillTiming.bat`이 SkillTable `HitDelayMs`·`AnimMs`로 내보냅니다(기존 도구 그대로).
- 클라이언트 AnimNotify는 시전·연출 타이밍에만 쓰고, 대상 산출과 데미지는 서버가 결정합니다([DESIGN_COMBAT_SKILL.md](DESIGN_COMBAT_SKILL.md)).

## 무기 등에 메기 (클라이언트 연출 전용)
- 비전투 시 무기는 **등 소켓**에 붙어 있습니다. 처음 보이는 캐릭터도 등에 멘 상태로 나타납니다.
- `SC_ATTACK`을 받으면 **즉시** 손 소켓으로 옮깁니다(꺼내는 모션 때문에 서버 `HitDelayMs`를 늦추지 않음).
- 마지막 공격·피격 후 `SheatheDelaySec`(5초, `MovementTable` 클라 전용 열 예정)이 지나면 넣는 모션 → 등 소켓.
- 프로토콜 변경이 없고 서버는 이 상태를 모릅니다.
- (2026-11-09 MC4.2d-2 구현, 활 먼저 — `DECISIONS.md`) 값 위치: WeaponTypeTable `SheatheDelaySec`(0이면 항상 손, 지금 검)·`SheatheMontage`·`SheatheSwapSec`(넣기 동작 중 등 소켓으로 옮기는 시각), 아이템별 등 부착 = ItemTable 생성 열 `BackSocket`·`BackM`·`BackF`(items.json `back`, `fit: "back"` = 몸 공간의 중심·방향·시위 쪽으로 지정). 조준 시작도 꺼내기 조건이고, 피격은 손에 든 동안 타이머만 다시 시작. 구현: `AJMPlayer`(DrawWeapon·넣기 타이머) + `UEquipmentComponent::SetWeaponOnBack` → `ULeaderPoseAppearance`.
- **무기 자세 `WeaponStance`**(`UJMAnimInstance`): 0 = 등에 멤(맨손 이동), 1 = 손에 듦(무기 이동), 2 = 스킬 조준. `ABP_Layer_Bow`의 `LocomotionLayer`가 Blend Poses by Int로 `BS_Idle_Walk_Run` / `BS_Bow_Locomotion`(1D 속도) / `BS_Bow_AimWalk`(2D 방향×속도)를 고른다. 조준 중 직접 조종하는 캐릭터는 카메라 방향을 보고(`bUseControllerDesiredRotation`) 옆·뒤로 걷는다.

## 모션 출처
- 우선순위(`DECISIONS.md` 2026-11-09): ① 매니퀸 템플릿에 이미 있는 모션(맨손 이동·점프·공격, 사망) → ② Mixamo(검·활 레이어 전체, 공용 피격, 활 조준 이동 — 사용자가 FBX 다운로드, `RTG_Mixamo_Mannequin` 하나로 리타깃) → ③ Mixamo에 없거나 불가능하면 VARCO(`Rig` → `Animate`) → ④ 셋 다 없으면 모션 없이 소켓 전환 + 블렌드. GASP·Lyra는 쓰지 않으며 외부 DCC도 쓰지 않습니다.
- 한손검·활 모션은 템플릿에 없으므로 MC4.2에서 확보합니다. 출처는 `ExternalAssets.md`에 기록합니다.

## 무기 타입 추가 시 해야 할 일
레이어 AnimBP + 공격·스킬 몽타주(Skill Hit 노티파이 포함)를 만들고, 무기 타입 → 레이어 데이터와 SkillTable 행, ItemTable 무기 행을 추가합니다. 코드 수정은 필요 없어야 합니다(무기 타입 enum 추가는 예외).
