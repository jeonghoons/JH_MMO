# DESIGN_CONTROL_CAMERA

Test 프로젝트에서 확정한 조작/카메라 방침입니다. 쿼터뷰 조준 입력은 [DESIGN_COMBAT_SKILL.md](DESIGN_COMBAT_SKILL.md)와 함께 봐야 하며, 두 문서가 충돌하면 병합 시 사용자에게 확인합니다.

## 확정 사항 (Test)
- 검은사막식 조작/카메라를 참고합니다 (모작 아님). 입력은 Enhanced Input.
- 이동 입력은 **WASD뿐**이며 달리기 전용 키가 없습니다. 속도 기반 블렌드 스페이스로 Idle → Walk → Run을 자동 블렌딩합니다(자동 달리기).
- WASD는 카메라 기준 이동, 마우스 이동으로 카메라가 캐릭터 중심으로 회전합니다.
- 이동/카메라 수치는 MovementTable에서 읽습니다.
- 스킬은 숫자키 1~5, 콤보 공격 없음.

## 서버 권위 관련
- ~~이동은 서버가 위치를 계산하고 `ObjectInfo`로 반영합니다.~~ → 아래 "서버 권위 이동(S1)"으로 구체화(2026-10-30). 클라이언트의 자동 달리기 블렌드 스페이스 입력(Speed/Direction)은 서버가 내려준 값 기준으로 계산합니다.

## 서버 권위 이동 (S1, 2026-10-30 구현)
방식은 **클라이언트 예측 + 서버 검증·보정**입니다(`DECISIONS.md`). 입력을 보내고 서버가 매 틱 위치를 계산하는 방식은 쓰지 않습니다.

1. **내 캐릭터(클라이언트)**: CharacterMovement로 바로 움직이고(예측), 움직이는 동안 `MoveSendIntervalMs`마다, 입력이 바뀌거나 완전히 멈춘 순간에 한 번 `CS_MOVE`(위치·yaw·속도·상태 + `correction_seq`)를 보냅니다. 정지 중에는 보내지 않습니다. 서버가 `ServerMinMoveIntervalMs`보다 잦은 패킷을 버리므로 그 안의 전송은 미뤘다가 보냅니다. 이동 속도 `MaxWalkSpeed`는 서버 스탯 `move_speed`.
2. **서버 검증**(`Room::PlayerMove`, 순서대로): ① `correction_seq`가 현재 보정 순번과 다르면 조용히 버림(보정 전에 보낸 이동) ② 너무 잦으면 버림 ③ NaN/Inf → 보정 ④ **속도**: 토큰 버킷 — 예산 += 경과 × `move_speed` × `ServerSpeedTolerance`(상한 `ServerMoveBudgetMs` 분량), 2D 이동 거리 > 예산 + `ServerMoveSlackCm`이면 보정 ⑤ **지형**: 내비메시 레이캐스트(경계 밖 `ServerNavToleranceCm`까지 허용, 수평 거리) 실패 → 보정 ⑥ 통과하면 속도 벡터를 허용 속도로 자르고 저장·AOI 방송.
3. **보정**: 서버는 순번을 +1하고 서버 위치를 본인에게 `SC_MOVE_OBJECT`(`correction_seq`)로 보냅니다. 클라이언트(`AJMMyPlayer::ApplyServerCorrection`)는 그 위치로 텔레포트하고 이후 이동에 새 순번을 싣습니다. 순번 덕분에 이미 날아가던 옛 이동이 연쇄로 보정되지 않습니다.
4. **원격 캐릭터**: 서버 위치로 `RemoteInterpSpeed` 보간, `RemoteSnapDistCm`보다 멀면 순간이동. 원격 **플레이어**는 마지막 패킷 이후 `RemoteExtrapolateMaxMs`까지만 속도로 외삽하고 멈춥니다(멈춤 패킷이 빠져도 미끄러지지 않게). 몬스터는 서버가 방향이 바뀔 때·멈출 때만 보내는 방식이라 상한을 두지 않습니다. 원격 캐릭터의 `ObjectInfo.position`은 서버 값을 유지합니다(`SyncTransformToInfo`는 내 캐릭터만).
5. **맵 데이터**: 서버 내비메시는 UE에서 캐릭터가 실제로 부딪히는 메시(충돌 켜짐 + Pawn Block)로만 만듭니다(`MapExportCommandlet` → `NavGenerator`). 충돌 없는 풀·덤불이 장애물로 들어가면 UE에서는 지나갈 수 있는 곳에서 서버가 이동을 거부해 끌려갑니다. 지오메트리는 Detour 좌표(UE (x,y,z) → (y,z,-x))와 뒤집은 정점 순서로 저장해야 NavGenerator가 바르게 읽습니다.
7. **서버 오브젝트 생성**: 서버 z는 바닥 높이라 캡슐이 지형과 겹치므로 클라이언트는 `AdjustIfPossibleButAlwaysSpawn`으로 항상 생성합니다(`UJMObjectManager::HandleSpawn`). 기본값이면 몬스터·다른 플레이어가 보이지 않을 수 있습니다.
6. **관측**: 서버 `[metrics]`의 `move(total ok/speed/nav/bad/stale/rate)` 누적 집계.

알려진 한계: z(높이)는 클라이언트 값을 그대로 씁니다(내비메시 근처인지만 확인). 봇(LoadTest)은 직선 이동만 해서 벽으로 둘러싸인 PlayerStart(궁수 계정)에서는 몬스터에게 가지 못합니다 — 길 찾기는 하지 않습니다(사용자 결정).

## 미정
- 현재 JH_MMO 클라이언트의 카메라(쿼터뷰)와 Test의 검은사막식 마우스 회전 카메라 중 무엇을 쓸지
- 마우스 커서 모드 전환 키, 줌 범위, 점프/회피 도입 여부
