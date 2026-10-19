# Common/DataTable

서버와 UE 클라이언트가 공용으로 읽는 데이터 테이블의 **단일 진실 공급원**입니다. 규약 전체는 [Docs/DESIGN_DATA.md](../../Docs/DESIGN_DATA.md)를 참고하세요.

- `<Table>.csv`: 서버/UE가 읽는 **기준 파일** (UTF-8, 쉼표 구분)
- `<Table>.xlsx`: 편집용. CSV와 같은 이름
- 편집은 xlsx 또는 UE DataTable에서 하고, **끝나면 항상 CSV와 일치**시킵니다.
  - xlsx 편집: CSV UTF-8로 저장 → UE에서 DataTable Reimport
  - DataTable 편집: Export as CSV로 이 폴더에 덮어쓰기 → xlsx도 갱신
- 첫 컬럼 `Name` = 정수 ID(1001부터), 컬럼명은 Row 구조체 프로퍼티명(PascalCase)과 동일
