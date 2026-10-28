# TODO / 로드맵

프로젝트의 할 일과 진행 상황을 기록합니다. CLAUDE.md는 정적인 아키텍처 문서이므로, 자주 바뀌는 진행 상황은 여기에 기록합니다.

## 진행 중

## 예정

(진행 순서와 체크박스는 [ROADMAP.md](ROADMAP.md)를 따릅니다. 2026-10-20부터 트랙 A(서버/포트폴리오: 몬스터·전투 → 아이템 → 거래/경매장 → 동접 테스트)와 트랙 B(기존 M0~M4)를 병행합니다. 아래는 설계 문서와의 연결용 메모입니다.)

- 플레이어 영웅을 Greystone/Sparrow(Paragon) 고정으로 사용, 영웅별 AnimBP — [DESIGN_CHARACTER_EQUIPMENT.md](DESIGN_CHARACTER_EQUIPMENT.md), [DESIGN_ANIMATION.md](DESIGN_ANIMATION.md)
- 무기 아이템 = 스탯 + 머티리얼 교체 (서버가 스탯 계산) — [DESIGN_CHARACTER_EQUIPMENT.md](DESIGN_CHARACTER_EQUIPMENT.md)
- 스킬 시스템을 영웅 종속 구조 + 타겟팅 타입(Targeted/NonTargeted)으로 재구성 — [DESIGN_COMBAT_SKILL.md](DESIGN_COMBAT_SKILL.md) (마이그레이션 순서 미정)

## 백로그

2026-10-20 클라이언트 검토에서 나온 항목 (M0.5 이후, M2/설계 작업에 연결):
- 서버 권위 이동: 내 캐릭터 위치·이동 상태를 클라이언트가 계산해 전송하고 서버 응답은 무시함(`JMObjectManager.cpp:88`, `JMMyPlayer.cpp:86-101`). 서버 보정 경로 필요 — [DESIGN_CONTROL_CAMERA.md](DESIGN_CONTROL_CAMERA.md)
- 원격 데드레커닝 시간 상한(`JMCharacterBase.cpp:55-58`), `SyncTransformToInfo`의 매 프레임 `ObjectInfo.position` 덮어쓰기 정리
- 하드코딩 → 데이터: 서버 주소/포트, 인게임 맵 이름, PlayerType→영웅 매핑(`JMPlayer.cpp:91-106`), 캡슐/보간/카메라/이동 임계값, UI 위젯 이름·크기 (`MovementTable`/`HeroTable`/`UITable`)
- 몬스터 데이터: `template_id` 기반 메시/AnimBP 로드, Rampage `DT_CharacterAsset` 행
- 채팅 기능 복구(송신 주석 처리 — `ChatWidget.cpp`의 TODO, `UNetworkManager::SendChat` 없음, 클라이언트 `SC_CHAT` 핸들러 없음. 2026-10-27 PIE에서 입력 후 메시지가 쌓이지 않음 확인). 로그인 실패 응답은 A0.7에서 처리
- 인벤토리 키 미연결: `AJMPlayerController::OnInventoryKeyPressed`를 호출하는 입력이 `BP_PControler`에 처음부터 없음(위젯 `WBP_Inventory`는 등록됨). A6.1 인벤토리 UI 때 `IA_Inventory` 입력 액션으로 연결, 로그아웃/`SC_CMOVE_OBJECT` 핸들러
- 렌더 설정(레이트레이싱 등) MMO 클라이언트 기준 재검토, 안드로이드 토큰 저장소 노출 확인
- `ProtobufCore/Include`(686개, 테스트·타 언어 생성기 포함)를 헤더만 남기고 `ThirdParty`로 정리

2026-10-28 QA에서 나온 항목 (A0.7에서 처리하지 않은 것, 근거는 `TESTING.md`):
- 플레이어 ID 충돌: 플레이어 id = 세션 번호(접속마다 +1), 몬스터 id는 100001부터 → 서버 재시작 없이 누적 접속 10만 회를 넘으면 겹친다(`Listener.h:27`, `Room.h:77-81`). 오브젝트 id 발급 일원화 필요
- 내 캐릭터 이동 보정 무시(`JMObjectManager.cpp:93`): 서버가 거부한 이동의 보정 패킷을 적용하지 않아 다른 클라이언트와 위치가 어긋날 수 있음 — 서버 권위 이동(⑤)에서 함께 처리
- 입장 시 내 캐릭터 `SC_ADD_OBJECT`를 두 번 보냄(`Room.cpp` `PlayerEnterRoom`). 클라이언트가 중복을 무시해 증상 없음
- 유휴 상태에서도 100ms 틱이 평균 약 10ms(최대 20~30ms) 늦음: Windows 타이머 해상도 + 다음 틱을 실행 시각 기준으로 예약. `timeBeginPeriod(1)` 또는 예정 시각 기준 예약 검토
- 빈 방이 삭제되지 않음(`RoomManager::Remove(room)` 비어 있음): 부하 테스트 후 방 50개·몬스터 450마리가 남음(메모리 46MB, CPU 영향 미미)
- 죽은 몬스터가 같은 피해 처리 안에서 `HIT`/`TRACE` 상태로 바뀌어 이동 패킷을 한 번 더 보냄(`Monster::OnDamaged`)
- DB: 50자를 넘는 ID/비밀번호의 `MultiByteToWideChar` 버퍼 처리(종료 문자 보장 없음, `DatabaseWorker.cpp`), `CP_ACP` 변환(인코딩 불일치), 평문 비밀번호, `is_dummy` 우회 — A5.1·보안 단계
- 서버 빌드 경고: `ServerService.cpp` BOM 없음(C4819, DataTable 로드 실패 메시지가 깨짐), 형 변환 경고 4건
- 클라이언트 성능: 정지 중에도 이동 패킷 초당 5회 전송, 스킬 몽타주·이펙트 첫 사용 시 동기 로딩, 송신 스레드 1ms 폴링
- 서버 핫패스 `cout`(입장·퇴장·세션 소멸·투사체 피해 로그) — A1.2 로거에서 처리
- 사용하지 않는 에셋: `AM_Greystone_Skill1`(402가 `AM_Greystone_Skill2`로 교체됨)
