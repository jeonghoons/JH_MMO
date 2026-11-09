# External Assets (git에 포함되지 않음)

용량/라이선스 문제로 아래 에셋은 저장소에 올리지 않습니다. clone 후 직접 설치하세요. 엔진 버전: **Unreal Engine 5.6**

현재 `UEClient/JM/Content`의 에셋은 다른 프로젝트에서 필요한 것만 이주한 상태입니다. 원본 팩을 전부 다시 받은 뒤 작업을 이어갈 예정이므로, 받은 팩은 아래 표에 기록해 두세요.

| 폴더 | 이름 | 링크 | 용도 | 설치 방법 |
|---|---|---|---|---|
| `Content/ParagonGreystone` | Paragon: Greystone | (Fab 링크 기입) | 영웅 후보(검+방패) | Fab 라이브러리 → Add to Project → `JM` |
| `Content/ParagonSparrow` | Paragon: Sparrow | (Fab 링크 기입) | 영웅 후보(활) | 위와 동일 |
| `Content/ParagonRampage` | Paragon: Rampage | (Fab 링크 기입) | 기존 사용 중 | 위와 동일 |
| `Content/ParagonProps` | Paragon: Props | (Fab 링크 기입) | 환경 소품(미사용 확인 필요) | 위와 동일 |
| `Content/KiteDemo` | Kite Demo | (Fab 링크 기입) | 데모 에셋(미사용 확인 필요) | 위와 동일 |
| `Content/LevelPrototyping`, `Content/StarterContent` | UE 템플릿 기본 에셋 | - | 프로토타이핑용 | 새 프로젝트 템플릿에서 가져오기 |
| `Content/CoreC` | Stylized Handpainted Modular Character Starter Kit: Male & Female + Outfit (CoreCharacters, 무료) | https://fab.com/s/485d4e7206ec | **플레이어 베이스 바디(남/여)**, 2026-11-04 MC2.1b | Fab 내 라이브러리 → Add to Project → `JM`. 몸 부위는 키트 메시를 그대로 사용(`jm_kit/kit_profiles.json`) |
| (Test) `Content/Polyphoria` | (에셋 이름 기입) | (Fab 링크 기입) | 모듈러 검토용, 병합 여부 미정 | 위와 동일 |
| (Test) `Content/Pirate`, `Adventures_woman` | (에셋 이름 기입) | (Fab 링크 기입) | 모듈러 캐릭터 검토용, 캐릭터 베이스 결정 후 폐기 여부 확정 | 위와 동일 |

> 팩을 다시 받을 때는 기존 이주본과 경로가 겹치므로, 덮어쓰기 전에 `Content/Character/*`(ABP, DT, BP)와 `DT_Appearance`/`DT_Equip`은 git으로 커밋된 상태인지 확인하세요.

## 직접 추가한 이미지 (원본 PNG는 git 제외, 가져온 텍스처 에셋은 git 포함)

| 에셋 | 원본 | 출처 | 비고 |
|---|---|---|---|
| `Content/Character/Greystone/Icons/T_Greystone_Icon_Skill1` | `UEClient/JM/SourceArt/Icons/Original/SwordSkill.png` | 사용자 제공 임시 아이콘(출처 미기재) | 2026-10-28. 그려진 체크무늬 배경을 투명 처리·256px 축소한 `SourceArt/Icons/T_Greystone_Icon_Skill1.png`를 가져옴. 정식 아이콘으로 교체 예정 |
| `Content/Character/Sparrow/Icons/T_Sparrow_Icon_Skill1` | `UEClient/JM/SourceArt/Icons/Original/ArrowSkill.png` | 위와 같음 | 위와 같음 |

## 엔진 템플릿에서 가져온 에셋 (git 포함)

| 에셋 | 원본 | 출처 | 비고 |
|---|---|---|---|
| `Content/Character/Mannequin/` (Meshes, Materials, Textures, Rigs, Anims/Unarmed, Anims/Death — 60개, 약 100MB) | `UE_5.6/Templates/TemplateResources/High/Characters/Content/Mannequins` | Epic UE 5.6.1 템플릿 매니퀸(Manny/Quinn, `SK_Mannequin`), 사용 가능 여부는 사용자 확인(DECISIONS 2026-11-04) | 2026-11-04 MC1.1. 원본과 바이트 동일 확인 후 에디터에서 Move. `Anims/Pistol`·`Anims/Rifle`(68개)은 무기 타입과 무관해 가져오지 않음(git 제외 경로 `Content/Characters/Mannequins/`에 남음). 몇몇 공격 모션에 템플릿 원본부터 있던 없는 경로 참조(`EditableAnimations/LS_*`, `SKM_Manny`)가 있으나 재생에는 영향 없음 |
| ~~`Content/Character/Mannequin/Body/SK_Body_<M\|F>_<Part>`~~ | — | — | 2026-11-04 삭제(MC1 재작업 — 키트 부위 메시를 그대로 사용) |

## Mixamo 애니메이션 (FBX 원본은 git 제외, 가져온 uasset·리타깃 결과는 git 포함)

출처: Adobe Mixamo(https://www.mixamo.com, Adobe 계정 로그인, 무료). 원본 FBX 재배포 금지 — `UEClient/JM/SourceArt/Mixamo/*.fbx`는 `.gitignore`. 가져온 uasset·리타깃 결과를 공개 저장소에 커밋하는 것은 사용자 결정(DECISIONS 2026-11-09).

**다운로드 설정(모두 같게):** 캐릭터 업로드 없이 기본 **Y Bot**. Format **FBX Binary**, Frames per Second **30**, Keyframe Reduction **none**. 애니메이션은 **Without Skin**, 이동 동작은 **In Place** 체크(낱개 다운로드만 — **팩 다운로드에는 In Place 옵션이 없으므로** 가져온 뒤 `retarget_strip_drift`로 제자리 처리). Y Bot 메시 자체는 한 번만 **With Skin**·T-Pose로 받음. 파일 이름은 Mixamo 동작 이름 그대로(`MX_<CamelCase>`로 가져옴).

**가져오기·리타깃(에디터를 켠 채 MCP, PIE·Simulate가 꺼져 있어야 함):** `RetargetTools.retarget_import_source_mesh`(최초 1회) → `retarget_import_anims(FBX 목록, 팩)` → `retarget_ensure_rigs`(최초 1회, 있으면 그대로) → `retarget_run(시퀀스 목록, 팩)` → 이동이 든 동작은 `retarget_strip_drift`. 리타기터는 `RTG_Mixamo_Mannequin` 하나(손바닥뼈 체인은 매핑 안 함, Root Motion 오퍼레이션 끔, `Pin IK Bones`로 IK 본이 발·손을 따름). 쓰는 시퀀스는 `MX_<무기>_<동작>`으로 이름을 바꾼 뒤 리타깃하면 `AS_<무기>_<동작>`이 된다.

| 에셋 | 원본 FBX | Mixamo 동작 | 비고 |
|---|---|---|---|
| `Content/Character/Mixamo/SKM_YBot`(+`_Skeleton`), `IK_Mixamo` | `SourceArt/Mixamo/Y Bot.fbx` | Y Bot(With Skin) | 리타깃 원본 메시. 2026-11-09 MC4.2b |
| `Content/Character/Mannequin/Rigs/IK_Mannequin`, `RTG_Mixamo_Mannequin` | — | — | 대상 `SKM_Manny_Simple`, 결과 스켈레톤 `SK_Mannequin` |
| `Content/Character/Mixamo/Anims/Bow/MX_StandingDrawArrow` → `Content/Character/Mannequin/Anims/Bow/AS_Bow_StandingDrawArrow` | `SourceArt/Mixamo/Standing Draw Arrow.fbx` | Standing Draw Arrow | MC4.2b 시범(사용자 확인). MC4.2d 활 스킬 조준 당김 후보 |
| `Mixamo/Anims/OneHandSword/MX_OneHandSword_Idle` → `Mannequin/Anims/OneHandSword/AS_OneHandSword_Idle` | `SourceArt/Mixamo/Pro Sword and Shield Pack/sword and shield idle (4).fbx` | Sword And Shield Idle | MC4.2c 대기 |
| `MX_OneHandSword_Walk` → `AS_OneHandSword_Walk` | `SourceArt/Mixamo/Pro Sword and Shield Pack/sword and shield walk.fbx` | Sword And Shield Walk(앞) | 제자리 처리, BS 138 |
| `MX_OneHandSword_Run` → `AS_OneHandSword_Run` | `SourceArt/Mixamo/Pro Sword and Shield Pack/sword and shield run.fbx` | Sword And Shield Run(앞) | 제자리 처리, BS 500 ×1.31 |
| `MX_OneHandSword_Attack` → `AS_OneHandSword_Attack` | `SourceArt/Mixamo/Pro Sword and Shield Pack/sword and shield slash.fbx` | Downward Slash | 2001 기본 공격 |
| `MX_OneHandSword_Skill1` → `AS_OneHandSword_Skill1` | `SourceArt/Mixamo/Pro Sword and Shield Pack/sword and shield attack.fbx` | Sword And Shield Attack(도약 내려찍기) | 2002, 제자리 처리(사용자 교체 — 처음 Power Slash `slash (4)`) |
| `MX_Common_Hit` → `Mannequin/Anims/Hit/AS_Common_Hit` | `SourceArt/Mixamo/Pro Sword and Shield Pack/sword and shield impact (3).fbx` | Sword And Shield Impact(머리) | 공용 피격(BodyTable `HitMontage`) |
