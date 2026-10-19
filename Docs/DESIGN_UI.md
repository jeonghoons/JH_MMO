# DESIGN_UI

## 확정 사항
- UI는 데이터 기반(UITable 등)으로 구성하되 검은사막/붉은사막 UI를 참고하며 모작하지 않습니다.
- 위젯 관리는 기존 `JMUIManager`(위젯 스택 매니저)를 사용합니다.

## 대상
- HUD: HP바, 스킬바(숫자키 1~5 슬롯, 현재 무기의 스킬 표시), 쿨타임
- 인벤토리/장비창, 몬스터 체력바 (기존 `HpBarWidget` 확장)

## 미정
- UITable 컬럼과 데이터화 범위
- 마우스 커서 모드 전환 키 ([DESIGN_CONTROL_CAMERA.md](DESIGN_CONTROL_CAMERA.md))
