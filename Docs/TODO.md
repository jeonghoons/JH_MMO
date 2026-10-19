# TODO / 로드맵

프로젝트의 할 일과 진행 상황을 기록합니다. CLAUDE.md는 정적인 아키텍처 문서이므로, 자주 바뀌는 진행 상황은 여기에 기록합니다.

## 진행 중

## 예정

(진행 순서와 체크박스는 [ROADMAP.md](ROADMAP.md)를 따릅니다. 아래는 설계 문서와의 연결용 메모입니다.)

- 플레이어 영웅을 Greystone/Sparrow(Paragon) 고정으로 사용, 영웅별 AnimBP — [DESIGN_CHARACTER_EQUIPMENT.md](DESIGN_CHARACTER_EQUIPMENT.md), [DESIGN_ANIMATION.md](DESIGN_ANIMATION.md)
- 무기 아이템 = 스탯 + 머티리얼 교체 (서버가 스탯 계산) — [DESIGN_CHARACTER_EQUIPMENT.md](DESIGN_CHARACTER_EQUIPMENT.md)
- 스킬 시스템을 영웅 종속 구조 + 타겟팅 타입(Targeted/NonTargeted)으로 재구성 — [DESIGN_COMBAT_SKILL.md](DESIGN_COMBAT_SKILL.md) (마이그레이션 순서 미정)

## 백로그

2026-10-20 클라이언트 검토에서 나온 항목 (M0.5 이후, M2/설계 작업에 연결):
- 서버 권위 이동: 내 캐릭터 위치·이동 상태를 클라이언트가 계산해 전송하고 서버 응답은 무시함(`JMObjectManager.cpp:88`, `JMMyPlayer.cpp:86-101`). 서버 보정 경로 필요 — [DESIGN_CONTROL_CAMERA.md](DESIGN_CONTROL_CAMERA.md)
- 원격 데드레커닝 시간 상한(`JMCharacterBase.cpp:55-58`), `SyncTransformToInfo`의 매 프레임 `ObjectInfo.position` 덮어쓰기 정리
- 하드코딩 → 데이터: 서버 주소/포트, 인게임 맵 이름, PlayerType→영웅 매핑(`JMPlayer.cpp:91-106`), 캡슐/보간/카메라/이동 임계값, UI 위젯 이름·크기 (`MovementTable`/`HeroTable`/`UITable`)
- 몬스터 데이터: `template_id` 기반 메시/AnimBP 로드, Rampage `DT_CharacterAsset` 행
- 채팅 기능 복구(송신 주석 처리, `SC_CHAT` 핸들러 없음), 로그인 실패 응답 필드, 로그아웃/`SC_CMOVE_OBJECT` 핸들러
- 렌더 설정(레이트레이싱 등) MMO 클라이언트 기준 재검토, 안드로이드 토큰 저장소 노출 확인
- `ProtobufCore/Include`(686개, 테스트·타 언어 생성기 포함)를 헤더만 남기고 `ThirdParty`로 정리
