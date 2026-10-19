# ROADMAP

Test 프로젝트(`UEProjects/Test`) 병합 작업의 진행 순서입니다. 설계는 `DESIGN_*.md`, 확정된 결정은 [DECISIONS.md](DECISIONS.md)에 둡니다. 기존 `TODO.md`의 예정 항목은 아래 단계와 연결됩니다.
**한 번에 하나의 체크박스만 진행하고, 완료하면 즉시 `[x]`로 갱신합니다.**

현재 위치: **Phase M0 (병합 준비)**

## Phase M0 – 병합 준비
- [x] Test의 서버 자리(Seam) 동작을 서버 권위 동작으로 전환한다는 방침을 `DESIGN_COMBAT_SKILL.md`에 기록
- [x] Test의 작업 절차 규칙·문서 구조·데이터 규약(`Common/DataTable`)을 이 저장소에 이식
- [x] **원본 에셋 팩 재다운로드** (사용자 작업) — 완료 후 `ExternalAssets.md` 표 채움
- [ ] 이식된 `MovementTable.csv`를 UE DataTable로 임포트할 구조(Row 구조체, `UDataManager`) 설계

## Phase M0.5 – 클라이언트 정리 (2026-10-20 검토 결과, 상세 항목은 아래 체크박스 단위로 진행)
- [ ] 0. `.gitignore`: `Content/Paragon*/`, `StarterContent/` 추가 완료 (2026-10-20). 이미 추적 중인 Paragon* 435개·StarterContent 2개는 `git rm -r --cached`로 추적만 해제 (대상 확인 후 실행)
- [ ] 1. 네트워크 안정화: 패킷 크기 검증, 수신 스트림 어긋남, `Disconnect` 스레드 종료 대기, `Deinitialize`, 재접속 정리, 핸들러 반환값·경계 처리
- [ ] 2. `JMPlayer.cpp` 몽타주 로드 버그·조기 return 수정, 죽은 코드·미사용 include 삭제
- [ ] 3. 명명/배치: `NpcCharaceter` 오타·위치, `CharacterAssetData.h` 위치, CP949 주석 UTF-8 변환 (BP 참조는 에디터에서 안내)
- [ ] 4. Content 정리(에디터 수동): WBP 중복 통일, 깨진 참조(`ABP_Base`, `BP_Player`, `DT_Appearance/Equip`) 제거, 잔재 에셋·`Character/` 루트 정리, Config(기본 맵·레거시 입력)
- [ ] 5. 레이어 분리: 송신 파사드, 핸들러 보일러플레이트, UIManager 일원화 (M2 `UDataManager`와 함께)
- 이월: 서버 권위 이동·하드코딩 이전 → `Docs/TODO.md` 기록. `ParagonSample` 맵·`_Core/Materials·Environment`는 `NetworkManager.cpp:64`가 인게임 레벨로 하드코딩해 사용 중이므로 자체 제작 맵으로 교체한 뒤 제외·삭제

## Phase M1 – 에디터 검증 (JH_MMO에서 진행, 에셋 재다운로드 이후)
- [x] 1. 기본 무기 숨김: 본 스케일 0으로 무기만 사라지는지 (Greystone, Sparrow) — 통과 (현재는 숨길 필요 없음)
- [x] 2. 무기 분리 — 폐기 (활·방패 분리가 막힘, 영웅 고유 무기만 쓰기로 함)
- [x] 3. 교차 리타게팅 — 폐기 (같은 이유)
- [x] **캐릭터 베이스 결정: Paragon 영웅 + 고유 무기** (`DECISIONS.md` 2026-10-17)

## Phase M2 – 데이터 파이프라인
- [ ] Row 구조체 + `UDataManager` 이식, `MovementTable` 임포트/조회
- [ ] 서버 CSV 로더 (`Common/DataTable/*.csv`를 읽어 `ServerData.h` 하드코딩 대체), 이동 수치 연동
- [ ] `UJMAssetDataManager`의 Mock DB 교체 여부 결정

## Phase M3 – 캐릭터/무기/애니메이션
- [ ] HeroTable (Greystone, Sparrow): 영웅 → 메시/AnimBP/스탯, 영웅 선택 흐름
- [ ] 영웅별 AnimBP 연결 ([DESIGN_ANIMATION.md](DESIGN_ANIMATION.md), AnimLayer/무기 교체 없음)
- [ ] WeaponItemTable + 무기 머티리얼 교체 + 장착 CS_/SC_ 패킷, 서버 스탯 계산 ([DESIGN_CHARACTER_EQUIPMENT.md](DESIGN_CHARACTER_EQUIPMENT.md))
- [x] 모듈러 코드/에셋 삭제 (클라이언트 코드, CoreC, DT_Appearance/DT_Equip). 남은 일: 에디터 컴파일 확인, `BP_Player`/`BP_MyPlayer`/`ABP_Base` 정리, `appearance_items` proto 제거
- [ ] 사용 중인 Paragon 에셋을 새 폴더로 정리해 커밋 (재다운로드 파일은 커밋하지 않음)

## Phase M4 – 전투/스킬 (서버 권위)
- [ ] SkillTable(영웅 종속, `HeroId`) + 스킬 시전 CS_/SC_ 패킷 ([DESIGN_COMBAT_SKILL.md](DESIGN_COMBAT_SKILL.md))
- [ ] AnimNotify는 시전·연출 타이밍만, 판정은 서버

## 수동 작업 기록 (에디터에서 사용자가 직접)
상세 절차는 기록하지 않고 대화창으로 안내합니다. 여기에는 항목별 한 줄 요약과 상태만 남깁니다.

- [x] 검증 1: 기본 무기 본 스케일 0 숨김 (통과, 현재는 사용하지 않음)
- [x] Greystone, Sparrow: 몽타주 Duplicate(`AM_`), AnimBP·피격·사망 Rename, `DT_CharacterAsset` 경로 갱신 (2026-10-19 완료)
- [ ] Rampage: 사용 에셋 확인(`BP_Npc`) 후 같은 방식으로 처리
- [ ] Paragon 에셋 최종 Move: 캐릭터 작업 종료 시 (아래 중단점 4번 참고)

## ▶ 중단점 (2026-10-17 종료 시점) — "에셋 정리 이어서 진행"이라고 하면 여기서 재개
**목표:** 재다운로드한 Paragon 전체 팩은 커밋하지 않고(`.gitignore`, 완성 후 제거), 실제 쓰는 에셋과 의존 에셋만 `Content/Character/<영웅이름>/`(Greystone, Sparrow, Rampage)로 **Move**해 커밋한다. 원본은 복원·백업하지 않으며 Move 후 Fix Up Redirectors를 실행한다 (`DECISIONS.md` 2026-10-17).

**확인된 기준 에셋 (`DT_CharacterAsset`)**
- Greystone: 메시 `/Game/ParagonGreystone/Characters/Heroes/Greystone/Meshes/Greystone`, 공격 몽타주 `.../Greystone/Animations/Attack_PrimaryA_Montage`, AnimBP·피격·사망은 이미 `Content/Character/Greystone/`
- Sparrow: 메시 `/Game/ParagonSparrow/Characters/Heroes/Sparrow/Meshes/Sparrow`, 공격 몽타주 `.../Sparrow/Animations/Primary_Fire_Med_Montage`, AnimBP·피격·사망은 이미 `Content/Character/Sparrow/`
- Rampage(몬스터): 사용 에셋 미확인 (`Content/Character/BP_Npc` 확인 필요)

**재개 시 순서 (한 단계씩 사용자에게 묻고 진행)**
> 2026-10-19 방침 변경 (`DECISIONS.md`): 팩은 캐릭터 작업 종료까지 유지. 고칠 에셋(몽타주·AnimBP·피격·사망)은 `Content/Character/<영웅>/`에 **Duplicate**, 무거운 에셋(메시·스켈레톤·머티리얼·텍스처·시퀀스)은 캐릭터 작업 종료 시 한 번에 **Move**.

1. Step 2 — Greystone: 공격 몽타주 등 고칠 에셋을 `Content/Character/Greystone/`에 Duplicate(네이밍 규칙 적용), `DT_CharacterAsset` 경로를 복사본으로 변경. 네이밍 규칙(SK_/ABP_/AM_/M_/T_)을 먼저 `DECISIONS.md`에 확정 (사용자 결과 대기 중)
2. (완료 2026-10-19: Greystone·Sparrow Step 2) 활/화살 `FX/Meshes/Heroes/Sparrow/Abilities/SM_Sparrow_Arrow`, 검 `FX/Meshes/Heroes/Greystone/SM_Greystone_Blade_01` 사용 여부는 **미정** — 스킬 작업(M4) 때 결정, 쓰기로 하면 최종 Move 대상에 포함
3. Rampage 사용 에셋 확인 후 처리
4. 캐릭터 작업 종료 시: 영웅 폴더와 `Characters/Global`을 Move → Fix Up Redirectors → `DT_CharacterAsset` 확인 → Save All → `Paragon*` 삭제 → PIE 확인 (Reference Viewer로 의존 확인)
5. 영웅 하나가 끝날 때마다 `Content/Character/**`와 소스·문서만 체크포인트 커밋
6. 정리 후 `git status`로 남은 변경(재다운로드 파일)을 확인하고, 커밋 범위를 사용자와 정해 `Content/Character/**`와 소스/문서만 커밋. 원본 `Paragon*` 폴더의 재다운로드 변경분은 커밋하지 않음(복원 또는 제외 방식은 그때 결정)

**그 외 남은 일**
- 에디터 컴파일 확인(모듈러 코드 삭제 후), `BP_Player`/`BP_MyPlayer`/`ABP_Base`가 쓰이는지 확인 후 정리
- `appearance_items` proto 제거(`GanPackets.bat` 필요)
- 이후 M2: Row 구조체/`UDataManager` 이식, 서버 CSV 로더
