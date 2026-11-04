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
