# Common/DataTable

서버와 UE 클라이언트가 공용으로 읽는 데이터 테이블의 **단일 진실 공급원**입니다. 규약 전체는 [Docs/DESIGN_DATA.md](../../Docs/DESIGN_DATA.md)를 참고하세요.

- `<Table>.csv`: 서버/UE가 읽는 **기준 파일** (UTF-8, 쉼표 구분)
- `<Table>.xlsx`: 편집용. CSV와 같은 이름
- 편집은 xlsx 또는 UE DataTable에서 하고, **끝나면 항상 CSV와 일치**시킵니다.
  - xlsx 편집: CSV UTF-8로 저장 → UE에서 DataTable Reimport
  - DataTable 편집: Export as CSV로 이 폴더에 덮어쓰기 → xlsx도 갱신
- 첫 컬럼 `Name` = 정수 ID(1001부터), 컬럼명은 Row 구조체 프로퍼티명(PascalCase)과 동일
- `SkillTable.csv`의 `AnimMs`(행동 시간 = 몽타주 1회 길이)와 `HitDelayMs`는 `Montage` 열이 있는 행에 한해 몽타주에서 뽑습니다(`HitDelayMs`는 **Skill Hit** 노티파이 시각, 노티파이가 없으면 기존 값 유지). `Montage`가 빈 행(몬스터 기본 공격)은 두 값을 직접 관리합니다. 기본 공격(`Slot` 0)은 `CooldownMs` 0이고 행동 시간·타격 시점이 `StatTable.AttackSpeed`로 나뉩니다(`Docs/DECISIONS.md` 2026-10-28). 몽타주를 고친 뒤 에디터를 닫고 `ExportSkillTiming.bat`을 실행하면 CSV와 `DT_SkillTable`이 함께 갱신되고, 서버를 재시작하면 판정 시점에 반영됩니다.
