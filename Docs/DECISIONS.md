# DECISIONS

결정 로그입니다. 번복할 때는 기존 항목을 지우지 말고 새 항목을 추가합니다. Test 프로젝트의 로그 중 이 저장소에서도 유효한 것만 옮겼습니다.

| 날짜 | 결정 | 이유 |
|---|---|---|
| 2026-10-16 | Test 프로젝트는 종료하고 JH_MMO로 병합한다. 구조의 기준은 JH_MMO | 모듈러 캐릭터의 한계 |
| 2026-10-16 | Test에서 "나중에 서버가 할 일"로 설계된 동작은 모두 JH_MMO의 서버 권위 동작(CS_ → 서버 검증/계산 → SC_)으로 전환. `IGameBackend`/`ULocalGameBackend`는 병합하지 않음 | 사용자 확정. 상세는 DESIGN_COMBAT_SKILL.md |
| 2026-10-16 | 데이터 허브는 Test 방식(CSV 허브 + 정수 ID 규약)을 따르되 위치는 `Common/DataTable/`. 서버도 이 CSV를 읽어 관리한다 | 사용자 결정 + 기존 빈 폴더 `Common/DataTable` 활용 |
| 2026-10-16 | 에디터 검증 3가지는 Test가 아니라 JH_MMO에서 진행한다. 캐릭터 베이스(Paragon 영웅 vs 매니퀸)는 검증 결과로 결정(미정). 원본 에셋 팩을 전부 재다운로드한 뒤 시작 | 사용자 결정 |
| 2026-10-17 | **간편화**: 캐릭터 베이스는 Paragon 영웅으로 확정, 각 영웅은 자기 원래 무기만 사용. 무기 공용화(교차 리타게팅, 활/방패 분리, 외부 에셋 구매)는 하지 않음. 영웅별 AnimBP 사용. 10-16의 "캐릭터 베이스 미정"과 DESIGN_CHARACTER_EQUIPMENT(매니퀸 통일)/DESIGN_ANIMATION(AnimLayer) 방침을 이 항목이 대체 | 사용자 결정. 검증 1은 통과, 2는 활·방패 분리가 막혔고 3은 불필요해짐 |
| 2026-10-17 | 플레이어 영웅은 Greystone(전사), Sparrow(궁수) 2명으로 시작, MMO 기본 기능 구현 후 확장. Rampage는 몬스터용 | 사용자 결정 |
| 2026-10-17 | 스킬은 무기가 아니라 **영웅에 종속**(`SkillTable.HeroId`). 무기별 AnimLayer 설계 폐기, 영웅별 AnimBP 사용 | 사용자 결정. 무기가 영웅에 고정됨 |
| 2026-10-17 | 무기 아이템 = 스탯 보너스 + **무기 머티리얼 슬롯 교체**만. 별도 메시/본 숨김/소켓 장착/의류 장비/모듈러 없음. 서버가 스탯 계산 | 사용자 결정 |
| 2026-10-17 | 모듈러 관련 코드와 에셋을 삭제한다(클라이언트 컴포넌트, CoreC, DT_Appearance/DT_Equip). 재다운로드한 Paragon 에셋은 사용하지 않고, 실제 사용하는 에셋과 의존 에셋만 새 폴더로 모아 커밋한다 | 사용자 결정 |
| 2026-10-17 | 사용 중인 Paragon 에셋과 의존 에셋은 `Content/Character/<영웅이름>/`(Greystone, Sparrow, Rampage)으로 모아 관리한다 | 사용자 결정 |
| 2026-10-17 | 다운로드한 Paragon 원본 폴더(`Content/Paragon*`)는 프로젝트 완성 전까지 그대로 두고(완성 후 제거, 깃 커밋 X), 쓰는 에셋과 의존 에셋만 `Content/Character/<영웅>/`에 독립 복사본으로 관리·커밋한다. 방법: 사용 에셋 Move → Fix Up Redirectors → Vault에서 팩 재설치로 원본 복원. 10-17의 "새 폴더로 모아 커밋"을 구체화 | 사용자 결정. Migrate는 같은 프로젝트로 불가, Duplicate는 참조가 원본에 남음 |
| 2026-10-17 | 위 Vault 재설치 방식을 번복: 쓰는 에셋과 의존 에셋만 `Content/Character/<영웅>/`으로 **Move**하고 원본은 복원·백업하지 않는다. `Content/Paragon*`에는 안 쓰는 에셋만 남고 `.gitignore` 처리, 완성 후 제거. Move 후 Fix Up Redirectors 필수. 이미 Move한 에셋이 다시 필요하면 Vault에서 팩을 받는다 | 사용자 결정. 가장 단순하고 별도 백업·재설치 단계가 없음 |
| 2026-10-19 | 에셋 정리 방식 변경: 캐릭터 관련 작업이 끝날 때까지 `Content/Paragon*` 팩을 그대로 두고(깃 커밋 X), **고칠 에셋(Montage, AnimBP, 피격·사망 등)은 `Content/Character/<영웅>/`에 Duplicate**해 새 이름으로 관리한다. 안 고칠 무거운 에셋(메시, 스켈레톤, 머티리얼, 텍스처, 시퀀스)은 팩을 참조하다가 캐릭터 작업 종료 시 영웅 폴더와 `Characters/Global`을 **한 번에 Move**하고, 그 뒤 `Paragon*`를 삭제한다. Move 후 Fix Up Redirectors와 `DT_CharacterAsset` 경로 확인 필수. 영웅 하나가 끝날 때마다 `Content/Character/**`와 소스·문서만 체크포인트 커밋. 10-17의 "사용 에셋 Move 우선" 순서를 대체 | 사용자 결정. 필요한 에셋 범위를 아직 확정할 수 없고 몽타주·DT 경로·네이밍 변경이 잦음 |
| 2026-10-19 | 에셋 네이밍 규칙 확정: 스켈레탈 메시 `SK_`, AnimBP `ABP_`, 몽타주 `AM_`, 머티리얼 `M_`, 머티리얼 인스턴스 `MI_`, 텍스처 `T_`, DataTable `DT_`. 영웅 이름을 포함한다(예: `SK_Greystone`, `ABP_Greystone`, `AM_Greystone_AttackPrimary`, `MI_Greystone_Sword`, `T_Greystone_Body_D`) | 사용자 확정. 나중의 일괄 이름 변경 비용을 줄이기 위함 |
| 2026-10-17 | Greystone 검과 방패는 같은 머티리얼 슬롯 3번을 공유 | 사용자 확인 |
| 2026-10-16 | 작업 절차 규칙(체크박스 단위 진행, 에디터 수동 작업은 대화창으로 단계별 안내)을 Test에서 이식 | 사용자 지정 |
| 2026-10-01 | 스탯/장비/스킬/이동/UI는 DataTable 기반, 코드에 수치 하드코딩 금지 (Test 결정 유지) | 현업식 데이터 기반 구조 |
| 2026-10-01 | 콤보 공격 없음, 스킬은 숫자키 1~5 + 무기별 스킬 목록, 맨손은 공격/스킬 불가(데이터로 표현) (Test 결정 유지, 쿼터뷰 조준 설계와 충돌 시 재확인) | 사용자 지정 |
| 2026-10-03 | 무기별 애니메이션은 Animation Layer Interface로 링크. 공격 타이밍은 C++ 타이머가 아니라 AnimNotify | 기존 방침 유지 |
| 2026-10-03 | 에셋은 Blender 등 외부 DCC 없이 UE 툴(IK Retargeter 등)만으로 가공 | 사용자 지정 |
