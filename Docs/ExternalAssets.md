# External Assets (git에 포함되지 않음)

용량/라이선스 문제로 아래 에셋은 저장소에 올리지 않습니다. clone 후 직접 설치하세요. 엔진 버전: **Unreal Engine 5.6**

현재 `UEClient/JM/Content`의 에셋은 다른 프로젝트에서 필요한 것만 이주한 상태입니다. 원본 팩을 전부 다시 받은 뒤 작업을 이어갈 예정이므로, 받은 팩은 아래 표에 기록해 두세요.

| 폴더 | 이름 | 링크 | 용도 | 설치 방법 |
|---|---|---|---|---|
| ~~`Content/ParagonGreystone`~~ | Paragon: Greystone | (Fab 링크 기입) | **2026-11-12 삭제** — 쓰던 FX 의존은 `Content/FX/Dep/Greystone`(git 포함). 설치 불필요 | — |
| ~~`Content/ParagonSparrow`~~ | Paragon: Sparrow | (Fab 링크 기입) | **2026-11-12 삭제** — 쓰던 FX 의존은 `Content/FX/Dep/Sparrow`(git 포함). 설치 불필요 | — |
| `Content/ParagonRampage` | Paragon: Rampage | (Fab 링크 기입) | 몬스터(메시·스켈레톤 등). FX가 쓰던 의존은 2026-11-12 `Content/FX/Dep/Rampage`로 옮겨 팩에서 빠짐 — 새로 받으면 그 에셋이 팩 쪽에도 다시 생기지만 참조하지 않음 | Fab 라이브러리 → Add to Project → `JM` |
| `Content/ParagonProps` | Paragon: Props | (Fab 링크 기입) | 환경 소품. **2026-11-21 DG4부터 다시 필요** — 던전 맵 `Continent01_Dungeon`과 대륙의 던전 입구 문틀이 `Monolith/Ruins/Meshes`의 벽·기둥·잔해·원형 바닥을 **이 폴더 경로 그대로** 참조합니다(Fab 에셋은 공개 저장소에 올릴 수 없어 Move해 커밋하지 않음, D-227 — 이 팩이 없으면 던전의 Ruins 장식이 보이지 않고 충돌·내비메시는 그대로). **새 맵 `Continent01`은 이 폴더를 쓰지 않음** — 쓰는 바위 메시·재질은 2026-11-18 `Content/World/Dep/ParagonProps`로 옮김(git 포함, 텍스처 2K로 축소 — D-191·D-192). 옛 맵 `ParagonSample`과 던전 제작(DG4 `Monolith/Ruins` 예정)에 필요. 새로 받으면 옮긴 에셋이 팩 쪽에도 다시 생기지만 `Continent01`은 참조하지 않음 | 위와 동일 |
| `Content/KiteDemo` | Kite Demo | (Fab 링크 기입) | 자연물(나무·바위·풀·지면 타일). **새 맵 `Continent01`은 이 폴더를 쓰지 않음** — 쓰는 에셋은 2026-11-18 `Content/World/Dep/KiteDemo`로 옮김(git 포함, 텍스처 2K로 축소 — D-191·D-192). 옛 맵 `ParagonSample`에만 필요 | 위와 동일 |
| `Content/LevelPrototyping`, `Content/StarterContent` | UE 템플릿 기본 에셋 | - | 프로토타이핑용 | 새 프로젝트 템플릿에서 가져오기 |
| `Content/CoreC` | Stylized Handpainted Modular Character Starter Kit: Male & Female + Outfit (CoreCharacters, 무료) | https://fab.com/s/485d4e7206ec | **플레이어 베이스 바디(남/여)**, 2026-11-04 MC2.1b | Fab 내 라이브러리 → Add to Project → `JM`. 몸 부위는 키트 메시를 그대로 사용(`jm_kit/kit_profiles.json`). **재설치 뒤 소켓 추가 필요**(2026-11-12 FX 궤적용, 폴더가 git 제외): `SKM_CoreC_M_Base_Body_Combined`·`SKM_CoreC_F_Base_Body_Combined`에 `hand_r` 자식 소켓 `FX_Weapon_Bottom`·`FX_Weapon_Top`(회전 0, 배율 1) — **2026-11-22 D-247 소켓 부착 기준으로 다시 계산(남녀 같음)**: (-9.611, 5.198, 1.949)·(-39.772, -2.779, 69.161). 값은 `WeaponSocket_R`(남녀 같은 변환, D-249) 기준 날 22%·칼끝 `(0, 0, 13.3)`·`(0, 0, 87.4)`(MCP `fx_weapon_socket_points` — 결과가 소켓 로컬이므로 `WeaponSocket_R` 변환을 곱해 `hand_r` 로컬로 바꿈), 추가는 `SkeletalMeshTools.add_socket` → `set_socket_transform` → 저장. (옛 값 M (-5.264, 6.422, 11.429)·(-8.211, 44.988, 74.633)은 정규화 전 검 기준이라 칼끝과 약 57cm 어긋났음) |
| (Test) `Content/Polyphoria` | (에셋 이름 기입) | (Fab 링크 기입) | 모듈러 검토용, 병합 여부 미정 | 위와 동일 |
| (Test) `Content/Pirate`, `Adventures_woman` | (에셋 이름 기입) | (Fab 링크 기입) | 모듈러 캐릭터 검토용, 캐릭터 베이스 결정 후 폐기 여부 확정 | 위와 동일 |

> 팩을 다시 받을 때는 기존 이주본과 경로가 겹치므로, 덮어쓰기 전에 `Content/Character/*`(ABP, DT, BP)와 `DT_Appearance`/`DT_Equip`은 git으로 커밋된 상태인지 확인하세요.

## CC0 텍스처 (git 포함, 2026-11-21 DG4)

던전 바닥·벽 재질(`Content/World/Dungeon/Materials/`, 부모 `M_Dungeon_Surface`)이 쓰는 텍스처입니다. 모두 [Poly Haven](https://polyhaven.com)의 CC0(저작권 표시·재배포 제한 없음, https://polyhaven.com/license)이고 2K JPG(색·DirectX 노멀·ARM)를 받아 `world_step dg_surface`로 가져왔습니다.

| 에셋 | Poly Haven 이름 | 제작자 | 쓰는 곳 |
|---|---|---|---|
| `T_FloorSlate_BaseColor/Normal/ARM`, `MI_Dungeon_FloorSlate` | [Stone Tiles 02](https://polyhaven.com/a/stone_tiles_02) | Charlotte Baglioni | 던전 바닥 |
| `T_WallBlock_*`, `MI_Dungeon_WallBlock` | [Medieval Blocks 03](https://polyhaven.com/a/medieval_blocks_03) | Rob Tuytel | 던전 벽 |
| `T_WallBrick_*`, `MI_Dungeon_WallBrick` | [Stone Brick Wall 001](https://polyhaven.com/a/stone_brick_wall_001) | Dimitrios Savva, Rico Cilliers | 예비(지금 배치에는 안 씀) |

## VARCO 맵 소품 (git 포함)

`Content/World/Props/<이름>/` — VARCO 3D로 만든 소품(컨셉·멀티뷰 이미지는 `SourceArt/Concept/`, git 제외). L2d 마을 11종에 더해 2026-11-21 DG4 던전 6종: `WallTorch`, `Brazier`, `Sarcophagus`, `BonePile`, `WeaponRack`, `GuardianStatue`.

`Content/FX/Proj/LightArrow/` — 2026-11-23 SK2(D-278) 3003 빛의 화살 `SM_Proj_LightArrow`(VARCO 3D, 자체 디자인 — 에메랄드 수정 화살). 컨셉 2장 중 왼쪽 → 멀티뷰 → 3D(30,000 → LOD0 6,000), 원본 FBX `SourceArt/SK2/LightArrow.fbx`(git 제외)·컨셉 `SourceArt/Concept/SK2/`. `jm_kit.skill_fx` `si_arrow_import`가 긴 축 +X·길이 3m·굵기 80cm(비균등)·가운데 원점으로 굽고 03등급 발광 부모(`M_Equip_PBR_Glow`)의 `MI_Proj_LightArrow`(GlowColor 에메랄드, MetalMask 0)를 입힘. VARCO 약 350포인트(컨셉 100·멀티뷰 50·3D 200).

## 직접 추가한 이미지 (원본 PNG는 git 제외, 가져온 텍스처 에셋은 git 포함)

| 에셋 | 원본 | 출처 | 비고 |
|---|---|---|---|
| `Content/UI/Icons/Skill/T_Icon_Skill_Sword_1` | `UEClient/JM/SourceArt/Icons/Original/SwordSkill.png` | VARCO 생성(사용자 제작) | 2026-10-28. 그려진 체크무늬 배경을 투명 처리·256px 축소한 `SourceArt/Icons/T_Icon_Skill_Sword_1.png`를 가져옴. 2026-11-10 `Character/Greystone/Icons/T_Greystone_Icon_Skill1`에서 이름·위치 변경(Greystone과 무관) → 2026-11-23 SK2에서 `T_Icon_Skill_<ID>`로 바뀌어 지금은 쓰지 않음(사용자 제작이라 지우지 않음) |
| `Content/UI/Icons/Skill/T_Icon_Skill_Bow_1` | `UEClient/JM/SourceArt/Icons/Original/ArrowSkill.png` | 위와 같음 | 위와 같음(원본 `T_Icon_Skill_Bow_1.png`, 옛 경로 `Character/Sparrow/Icons/T_Sparrow_Icon_Skill1`) → 2026-11-23 SK2에서 `T_Icon_Skill_<ID>`로 바뀌어 지금은 쓰지 않음(사용자 제작이라 지우지 않음) |
| `Content/UI/Icons/Item/T_Icon_Potion_Hp` | `UEClient/JM/SourceArt/Icons/T_Icon_Potion_Hp.png` | Claude가 Python(PIL)으로 그린 임시 그림 | 2026-11-16 A3.2. HP 물약(ItemTable 8001) 임시 아이콘, 256px 투명 배경. 콘텐츠 동결(D-147) 중이라 임시 허용 — 영상 준비 때 교체 판단 → 2026-11-23 SK2에서 `T_Icon_Item_8001`로 바뀌어 지금은 쓰지 않음 |
| `Content/UI/Icons/Skill/T_Icon_Skill_{2002,2003,3002,3003,4002,4003}`, `Content/UI/Icons/Item/T_Icon_Item_{8001~8004,8051~8055}` | `SourceArt/UI/Icons/{Skill,Item}/*.png`(256px), VARCO 원본 1024px는 `UEClient/JM/SourceArt/Concept/SK2/icons/`(git 제외) | VARCO `GenerateImage`(자체 디자인, 흐름 JH_MMO, 15장 750포인트) | 2026-11-23 SK2. 스킬은 타격 순간 장면, 아이템은 흰 배경을 투명 처리(테두리에서 채우기 + 1px 줄임·흐림)하고 여백 8%로 가운데 맞춤. 사용자 시트 승인. 가져오기 `world_step` `ui_import_texture`(compress ui) |
| `Content/Slate/Cursors/Cursor_{Default,Hand,Talk}{,@1.5x}.png`(에셋이 아닌 PNG 파일 — 하드웨어 커서) | `SourceArt/Cursors/draw_cursors.py` | Claude가 Python(PIL)으로 그림(2026-11-22 UI2, 사용자 승인) | 32·48px, 핫스팟 화살표 끝. 설정은 `DefaultEngine.ini` `HardwareCursors` |
| `Content/UI/Frame/T_UI_{Window,TitleBar,Button,Button_Hover,Slot}` | `SourceArt/UI/out/*.png`(생성 스크립트 `SourceArt/UI/draw_ui_frame.py`) | Claude가 Python(PIL)으로 그림(2026-11-22 UI3, 사용자 승인 — `SourceArt/UI/mock.png`) | 창 9-slice·버튼·아이템 칸 |

## 글꼴 (git 포함, 2026-11-22 UI1 — D-270)

| 에셋 | 원본 | 출처 | 비고 |
|---|---|---|---|
| `Content/UI/Fonts/FF_Pretendard_{Regular,Medium,Bold}`, `F_Body` | `SourceArt/Fonts/Pretendard/` | [Pretendard 1.3.9](https://github.com/orioncactus/pretendard) — SIL OFL 1.1(`LICENSE.txt` 함께) | 본문·숫자 |
| `Content/UI/Fonts/FF_NotoSerifKR_{SemiBold,Bold}`, `F_Title` | `SourceArt/Fonts/NotoSerifKR/` | [Noto Serif KR](https://github.com/notofonts/noto-cjk) SubsetOTF — SIL OFL 1.1(`LICENSE.txt` 함께) | 창 제목·NPC 이름표 |

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
| `MX_Bow_Idle`·`MX_Bow_Walk`·`MX_Bow_Run` → `AS_Bow_*` | `SourceArt/Mixamo/Pro Longbow Pack/standing idle 01.fbx`·`standing walk forward.fbx`·`standing run forward.fbx` | Standing Idle 01 / Walk Forward / Run Forward | MC4.2d-2 활 든 이동(`BS_Bow_Locomotion` 0·106·500 ×1.49), 걷기·달리기 제자리 처리 |
| `MX_Bow_AimIdle`·`MX_Bow_AimWalkFwd/Bwd/Left/Right` → `AS_Bow_*` | `standing aim overdraw.fbx`·`standing aim walk forward/back/left/right.fbx` | Standing Aim Overdraw / Aim Walk 4방향 | 조준 이동(`BS_Bow_AimWalk`, 속도 200에 ×1.46~2.3), 제자리 처리 |
| `MX_Bow_Attack` + `MX_Bow_Skill1` → `AM_Bow_Attack` | `standing draw arrow.fbx` + `standing aim recoil.fbx` | Standing Draw Arrow + Standing Aim Recoil | 3001 기본 공격(당기기 1.033초 + 놓기, 타격 1.213) |
| `MX_Bow_Skill1` → `AM_Bow_Skill1` | `standing aim recoil.fbx` | Standing Aim Recoil | 3002 확정 발사(타격 0.18) |
| `MX_Bow_Sheathe` → `AM_Bow_Sheathe` | `standing disarm bow.fbx` | Standing Disarm Bow | 활 넣기(등 소켓 전환 0.45초, 왼손 궤적 측정) |
| `Mixamo/Anims/TwoHandSword/MX_GreatSwordIdle`·`Walk`·`Run2` → `Mannequin/Anims/TwoHandSword/AS_TwoHandSword_GreatSword*` | `SourceArt/Mixamo/Great Sword Pack/great sword idle.fbx`·`great sword walk.fbx`·`great sword run (2).fbx` | Great Sword Idle / Walk(앞) / Run(앞) | WT1(2026-11-22) 양손검 이동 `BS_TwoHandSword_Locomotion` 0·101·500 ×1.25, 걷기·달리기 제자리 처리. 팩 파일 이름의 번호는 순서가 섞여 있음 — `walk (2)`·`run`은 뒷걸음질(골반 이동으로 구분) |
| `MX_GreatSwordSlash` → `AM_TwoHandSword_Attack` | `great sword slash.fbx` | Great Sword Downward Slash | 4001 기본 공격(1.267초, 타격 0.65·휘두르기 끝 0.85 — 초안, 서버 값은 2일차 `ExportSkillTiming`) |
| `MX_GreatSwordHighSpinAttack` → `AM_TwoHandSword_Skill1` | `great sword high spin attack.fbx` | Great Sword High Spin Attack | 4002 스킬1(1.867초, 타격 1.0·끝 1.4), 제자리 처리(감속 이동이라 중간에 조금 밀림) |
| `MX_DrawAGreatSword1`·`2` → `AM_TwoHandSword_Sheathe`·`OneHandSword/AM_Sword_Sheathe` | `draw a great sword 1.fbx`·`draw a great sword 2.fbx` | Draw A Great Sword 1·2 | 넣기 = 2 → 1을 거꾸로(구간 배율 -1, `AnimAssetBuilder::SetMontageSegmentPlayRates`), 등 소켓 전환 0.8초 — 한손검도 같은 몽타주(D-254) |
| `MX_Common_Hit` → `Mannequin/Anims/Hit/AS_Common_Hit` | `SourceArt/Mixamo/Pro Sword and Shield Pack/sword and shield impact (3).fbx` | Sword And Shield Impact(머리) | 공용 피격(BodyTable `HitMontage`) |
| `AS_OneHandSword_SwordAndShieldAttack3` → `AM_OneHandSword_Skill1`(새로 만듦) / `AS_OneHandSword_Skill1` → `AM_OneHandSword_Skill2` / `AS_TwoHandSword_GreatSwordJumpAttack` → `AM_TwoHandSword_Skill2` | 위 표의 시퀀스 | — | 2026-11-23 SK2(D-278): 2002 스킬1 앞 부채꼴(타격 0.78·끝 1.0, 궤적 0.6~1.05), 2003 스킬2 = 옛 도약 내려찍기(타격 1.25·끝 1.45 — 옛 `AM_OneHandSword_Skill1`은 지우고 같은 경로에 Attack3로 다시 만듦), 4003 스킬2 점프 내려찍기(타격 1.12·끝 1.45, 궤적 0.85~1.3). 시각은 손 본 앞 거리(`anim_measure_bone_reach`)로 정하고 `ExportSkillTiming.bat`으로 SkillTable에 반영 |
| `MX_SwordAndShield{Attack2,Attack3,Attack4,Slash2,Slash3,Slash4,Slash5,Kick,Casting,Casting2,PowerUp}` → `Mannequin/Anims/OneHandSword/AS_OneHandSword_SwordAndShield…` | `Pro Sword and Shield Pack/sword and shield attack (2)~(4)·slash (2)~(5)·kick·casting·casting (2)·power up.fbx` | 같은 이름 | 2026-11-23 스킬2 동작 비교용 공격 동작 전부(쓰는 것은 몽타주가 정해지면 표에 따로). 이동 든 것(attack (2)·(3), slash (2)·(4))은 제자리 처리. 기존 `AS_OneHandSword_Attack` = slash, `AS_OneHandSword_Skill1` = attack |
| `MX_GreatSword{Attack,Kick,Kick2,JumpAttack,SlideAttack,Casting,PowerUp}`·`MX_SpellCast` → `Mannequin/Anims/TwoHandSword/AS_TwoHandSword_…` | `Great Sword Pack/great sword attack·kick·kick (2)·jump attack·slide attack·casting·power up·spell cast.fbx` | 같은 이름 | 위와 같음. jump attack·slide attack은 제자리 처리. Slash2~5는 WT1 때 이미 가져옴. 기존 `AS_TwoHandSword_GreatSwordSlash` = 기본 공격, `…HighSpinAttack` = 스킬1 |
| `MX_Standing{MeleeKick,MeleePunch,DodgeBackward,DiveForward}` → `Mannequin/Anims/Bow/AS_Bow_Standing…` | `Pro Longbow Pack/standing melee kick·melee punch·dodge backward·dive forward.fbx` | 같은 이름 | 위와 같음. 구르기 2개는 제자리 처리. 기존 `AS_Bow_Attack` = draw arrow, `AS_Bow_Skill1` = aim recoil, `AS_Bow_AimIdle` = aim overdraw. 활 "위로 쏘기"는 팩에 없음 |

2026-11-23 SK2 정리(사용자 결정): 위 세 줄의 공격 동작 중 스킬에 쓰지 않는 21개(`AS_`·`MX_` 짝 42개 — 목록은 `jm_kit/skill_fx.py` `UNUSED_ANIMS`)는 `Mannequin/Anims/_Unused/<무기>/`·`Mixamo/Anims/_Unused/<무기>/`로 옮기고 `.gitignore`로 커밋에서 뺐습니다(로컬 비교용, 없으면 FBX에서 다시 가져옴). 쓰는 것: 한손검 Attack3(2002), 양손검 JumpAttack(4003). WT1 때 커밋한 `GreatSwordSlash2~5`는 그대로 둡니다.

