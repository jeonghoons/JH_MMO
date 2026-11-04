# HANDOFF: 모듈형 장비 시스템 — VARCO 3D + UE 5.6 + uemcp 프로토타입 결과

> 이 문서는 **다른 Claude 세션(프로토타입을 만든 세션)이 JH_MMO의 Claude에게 넘기는 인수인계서**입니다.
> 읽는 쪽(JH_MMO의 Claude)이 할 일: ① 이 결과가 **이 프로젝트에 적용 가능한지 판단**하고, ② 가능하다면 **구현 계획**을 세우는 것입니다. 이 문서는 구현 지시가 아니라 **근거 자료 + 계획 재료**입니다.
> 작성일: 2026-11-03 · 엔진: UE 5.6.1 · 브랜치: 프로토타입은 `test/varco-equipment`(커밋 없음) · 이 문서는 커밋하지 않은 상태로 `Docs/`에 있습니다.

## 0. 30초 요약

| 질문 | 답 |
|---|---|
| 일반 MMO식 모듈형 장비(슬롯별 파츠 교체, 남녀 체형, 몸 숨김, 런타임 교체)를 UE 5.6에서 만들 수 있나? | **프로토타입 수준에서 가능**함을 확인했다. 매니퀸 스켈레톤 하나에 Leader Pose로 파츠를 얹고, 장비가 덮는 몸 부위를 숨긴다. |
| 외부 DCC(Blender) 없이 되나? | **된다.** UE의 Geometry Script(스킨 웨이트 전이)로 VARCO가 만든 정적 메시를 매니퀸 스켈레톤에 스키닝했다. |
| VARCO 3D로 장비를 만들 수 있나? | 정적 메시(무기·투구)는 바로 쓸 만하다. 몸에 맞는 의상은 **자동 맞춤 + 사람이 정리**하는 전제로 쓸 만하다. |
| 그대로 프로덕션에 쓸 수 있나? | **아니다.** 아래 "한계"의 폴리 수·메시 찢김·성능·무기 애니메이션·라이선스가 남아 있다. |
| 이 프로젝트에 지금 바로 적용할지? | 이 문서 6장(적용 판단)을 보고 **JH_MMO 쪽에서 판단**. 기존 결정(2026-10-17 Paragon 고정, 10-03 외부 DCC 금지)과 **충돌**하므로 사용자 결정과 `DECISIONS.md` 새 항목이 먼저다. |

**프로토타입이 보여 준 가장 중요한 사실 3가지**
1. 파츠 교체형 모듈형의 최대 불확실성이던 "AI가 만든 의상을 매니퀸에 입히는 것"이 **UE 단독으로 풀린다**(스킨 웨이트 전이).
2. "피부가 옷을 뚫는다"는 문제는 **몸을 부위별로 나눠 장비가 덮는 부위를 숨기는 것**으로 해결된다(일반 MMO와 같은 방식).
3. 에디터를 못 보는 Claude도 **SceneCapture2D로 렌더링해 PNG로 저장 → Read 도구로 직접 확인**할 수 있다. ROADMAP의 "Claude가 못 보는 것: 에디터의 애니메이션·이펙트 모양"은 이 방법으로 상당 부분 해소된다(부록 A).

## 1. 사용자 제약·승인 (이 프로토타입이 지킨 규칙)

이 값은 사용자가 이 세션에서 직접 정한 것입니다. 다음 세션에서 같은 권한이 있다고 가정하지 마세요.

- 모든 작업은 **테스트이고, 끝나면 되돌린다.** 통과해도 우선 되돌려야 한다.
- 레벨은 서버 없이 쓰는 **테스트 레벨**(`/Game/Test_VARCO/L_Test`)에서만 한다.
- 파일·폴더는 **깔끔하게 지울 수 있게** 한 폴더에 모은다. **삭제는 사용자가 "해 달라"고 할 때만** 한다.
- VARCO 포인트는 **5,000까지는 확인 없이 사용 가능**(이번에 1,750 사용). 그 이상은 확인 필요.
- 외부 DCC(Blender 등)는 "VARCO로 제작만" **테스트 동안만 허용**한다고 했다(실제로는 Blender를 쓰지 않았다).
- 서버 권위 규칙·DataTable 규칙은 "테스트 동안만" 적용하지 않았다(= 프로토타입은 서버·CSV와 연결되어 있지 않다).
- **사고 기록**: 사용자가 도구 실행을 거부했는데도 삭제 스크립트(`delete_directory`)가 실제로 실행되어 `Test_VARCO`와 복사한 매니퀸이 지워진 적이 있다. 이후 복구했다. 교훈: 파괴적 명령은 **사용자가 명시적으로 요청한 뒤에만** 보내고, 거부 직후에는 상태를 먼저 확인한다.

## 2. 이 프로젝트와의 관계 (먼저 읽을 기존 문서)

JH_MMO의 기존 방침과 이 프로토타입이 어떻게 맞물리는지:

| 기존 결정 | 내용 | 이 프로토타입과의 관계 |
|---|---|---|
| 2026-10-16/17 | Test 프로젝트의 모듈러 캐릭터는 한계로 폐기. 캐릭터 베이스를 **Paragon 영웅**으로 확정, 무기 공용화(교차 리타게팅, 활/방패 분리)는 하지 않음. 당시 검증 2("활·방패 분리")가 막힘 | 이 프로토타입은 **그 결정을 뒤집는 방향**이다. 단, 이번 방식은 "스켈레톤 하나 + 파츠 교체"이며 교차 리타게팅이 필요 없다. 활/방패 분리 문제는 **무기를 통째로 소켓에 붙이는 방식**이라 이번에는 해당 없다(활은 시험하지 않았다) |
| 2026-10-17 | 무기 아이템 = 스탯 + **머티리얼 교체만**, 별도 메시/본 숨김/소켓 장착/의류/모듈러 없음 | 프로토타입은 **정반대**(별도 메시, 본 소켓 장착, 의류, 몸 숨김). 새 방향으로 가면 이 항목을 대체하는 새 `DECISIONS.md` 항목이 필요 |
| 2026-10-03 | Blender 등 **외부 DCC 없이** UE 툴만으로 가공 | 프로토타입은 이 제약을 **지켰다**(Geometry Script). 다만 양산 품질 정리(구멍·웨이트)는 DCC가 훨씬 쉬우니 재검토 대상 |
| 2026-10-17 | 스킬은 영웅에 종속(`SkillTable.HeroId`), 영웅별 AnimBP | 매니퀸 공용 스켈레톤으로 가면 스킬이 **영웅이 아니라 무기 타입**에 종속되는 구조가 자연스럽다. 이는 SkillTable·ABP·몽타주·노티파이 전체에 영향을 준다(가장 큰 재작업) |
| 2026-10-30 | UE 에디터 MCP로 uemcp 0.1.0 채택, C-7에서 대체 검토 중 | 이번 프로토타입은 **uemcp의 `ue_python`만으로** 전부 수행했다. C-7 결정 자료로 5장 "uemcp 사용 메모" 참고 |
| `CLAUDE.md` 규칙 | `Content/` 에셋 작업은 사용자가 따라 할 수 있게 모든 단계를 대화로 안내, 문서에 없는 결정은 먼저 묻고 `DECISIONS.md`에 기록 | 이 프로토타입은 사용자 요청으로 테스트 폴더에서 자동화했다. **프로젝트에 적용할 때는 이 규칙을 다시 지킬 것** |
| 서버 권위 | 장비 상태는 서버가 결정하고 `ObjectInfo`/`SC_*`로 전달, 클라이언트는 반영만 | 프로토타입에는 서버가 없다. 적용 시 장비 ID → 외형 조립은 클라이언트 반영 로직이어야 한다(7장) |

현재 ROADMAP 위치는 **P. 성능 개선(P1)** 이다. 이 프로토타입은 **트랙 B(콘텐츠/클라이언트)** 성격이며 트랙 A와 일정이 겹치지 않게 조정해야 한다.

## 3. 무엇을 검증했나 (테스트 결과)

### 3-1. 파이프라인 / 구조 검증

| # | 시험 | 결과 | 근거 |
|---|---|---|---|
| 1 | uemcp ↔ UE 5.6.1 에디터 연결, `ue_python`으로 전부 제어 | 통과 | 에디터가 완전히 뜬 뒤에야 검색됨(로딩 중에는 `connected:false`) |
| 2 | VARCO MCP로 이미지 → 3D 생성, FBX 다운로드 | 통과 | 7개 에셋(검·투구·흉갑·상의·바지·장화·장갑) |
| 3 | 정적 메시(검, 투구)를 본에 부착 | 통과 | 달리기·점프에서도 본을 따라감 |
| 4 | 의상(흉갑·상의·바지·장화·장갑)을 매니퀸 스켈레톤으로 스키닝 | 통과 | `GeometryScript_BoneWeights.transfer_bone_weights_from_mesh` |
| 5 | 매니퀸 애니메이션 구동(Idle/Walk/Jog/Jump/Attack) | 통과 | Leader Pose, Simulate에서 확인 |
| 6 | 몸 숨김(부위 분할 + 장비별 숨김 규칙) | 통과 | 숨김 없는 대조군은 어깨·무릎에서 피부가 뚫리고, 적용 쪽은 깨끗함 |
| 7 | 남녀 체형(Manny/Quinn) | 통과 | 같은 의상을 자동 맞춤으로 두 체형에 적용 |
| 8 | 런타임 장비 교체(달리는 중 상의→흉갑, 장화 해제) | 통과 | Simulate에서 파이썬으로 교체, 애니메이션 끊김 없음, 몸 숨김 규칙대로 갱신 |
| 9 | 맨몸·부분 착용 조합(맨몸 / 상의만 / 바지+장화+장갑만) | 통과 | 몸 부위 사이 이음새 없음 |
| 10 | 레벨 재열기(다른 맵 갔다가 복귀) 후 상태 유지 | 통과 | 리더 숨김, 애니메이션, 리더 포즈 연결, 정적 파츠 유지 |

### 3-2. 한계·미검증 (정직한 목록)

| 항목 | 상태 | 비고 |
|---|---|---|
| **폴리 수** | 문제 | 캐릭터 1명이 **보이는 것만 약 19.4만 삼각형**(남성 튜닉 풀세트). 매니퀸 'Simple' 몸 메시가 몸 전체 약 9.2만(손 부위만 2.5만), 의상 2.2~3.6만씩, 투구·검 각 3만. MMO 예산을 크게 넘는다. VARCO `Remesh`(0포인트, 1천~10만 폴리)와 자체 저폴리 베이스 바디가 필요 — **Remesh 노드는 이번에 시험하지 않았다** |
| **의상 찢김** | 문제 | AI 메시는 칼라·밑단에 작은 구멍이 있다. `fill_all_mesh_holes`는 소매·목·밑단의 **정상적인 열린 가장자리까지 막으므로 쓰지 않았다**. 양산에서는 DCC 정리 또는 홀 크기 기준 선택 메움이 필요 |
| **런타임 병합** | 미검증 | 파츠가 많으면 캐릭터 1명이 컴포넌트 13개(리더 1 + 몸 6 + 슬롯 4 + 정적 2). `USkeletalMergingLibrary`는 **Python에 노출되지 않아** 시험하지 못했다(C++/BP 필요). 다수 플레이어 환경에서는 병합이 사실상 필수 |
| **성능 측정** | 미검증 | 캐릭터 수십 명 동시 프레임 시간을 재지 않았다(에디터가 애니메이션을 틱하지 않아 측정이 부정확) |
| **무기 타입별 애니메이션** | 미검증 | 맨손 애니메이션만 시험. 소총·권총 모션은 템플릿에 있으나 한손검·두손검·활은 **없음**(별도 확보/제작 필요) |
| **장갑 정밀도** | 부분 | 한 쌍을 좌우로 나눠 손 본에 맞췄고 대체로 맞지만 정밀하지 않다 |
| **검 쥐는 각도** | 부분 | 대략 맞춤(소켓 오프셋 데이터화 필요) |
| **상의+흉갑 겹쳐 입기** | 미검증 | 현재 한 슬롯(top)에 하나만. 레이어 슬롯 설계 필요 |
| **서버·패킷·DB·DataTable 연동** | 미구현 | 프로토타입은 Python 조립 코드뿐. 게임 코드는 C++/BP로 새로 짜야 한다 |
| **VARCO 약관(상업적 사용, 출처 표기)** | 미확인 | 생성 옵션에 `CC_BY_4_0`/`PRIVATE`가 있고 이번엔 `PRIVATE` 사용. **약관은 읽지 않았다** |
| **Epic Mannequin 에셋 사용 조건** | 미확인 | 엔진 설치본 `Templates`의 매니퀸을 복사해 썼다. 프로젝트에 포함해 배포 가능한지 확인 필요 |
| **일반 `Play`(PIE) 동작** | 미검증 | 프로젝트 게임 모드는 서버 로그인이 필요해서 Simulate로만 확인했다 |

### 3-3. 측정값

| 항목 | 값 |
|---|---|
| VARCO 비용 | 에셋 1개(이미지 + 텍스처 3D, PBR·2048) 약 **250포인트** (이미지 20 + 3D 약 230). 합계 7개 = 1,750 |
| VARCO 시간 | 3D 생성 약 4~5분(동시에 3~6개 병렬) |
| 삼각형(원본) | 몸(M): 머리 13,280 / 몸통 15,446 / 팔 13,904 / 손 25,456 / 다리 12,608 / 발 11,484 (합 약 92k). 몸(F): 14,640 / 13,978 / 8,228 / 25,420 / 13,734 / 11,280. 의상: 상의 33,232, 흉갑 22,160, 바지 35,764, 장화 21,878, 장갑 30,000. 정적: 투구 30,000, 검 30,000 |
| 스키닝 처리 시간 | 의상 1개당 수 초(몸 정점 4.6만 분류 약 0.3초) |
| FBX 크기 | 에셋당 9~15MB(텍스처 포함) |

## 4. 검증된 아키텍처 (프로토타입 구조)

```
캐릭터 1명
├─ Leader (SkeletalMeshComponent) ─ 전체 몸 메시 + 애니메이션 재생. 화면에는 보이지 않음(visible=false, hidden_in_game=true)
│                                   * 반드시 visibility_based_anim_tick_option = AlwaysTickPoseAndRefreshBones
├─ Body_head / torso / arms / hands / legs / feet ─ 몸을 부위별로 나눈 스켈레탈 메시(Leader Pose 따라감)
├─ Slot_top / pants / boots / gloves ─ 장비 스켈레탈 메시(몸에 스키닝된 의상). Leader Pose 따라감
├─ Slot_head(투구) ─ 정적 메시, head 본에 부착 (상대 변환은 데이터)
└─ Slot_weapon(검) ─ 정적 메시, hand_r 본에 부착 (상대 변환은 데이터)
```

**핵심 규칙**
- **정적 메시 vs 스켈레탈 메시**: 단단한 물체(무기·투구)는 정적 메시를 본에 부착(가볍고 단순). 몸에 밀착해 **구부러져야 하는 것**(상의·바지·장화·장갑)만 스켈레탈 메시.
- **몸 숨김 = 일반 MMO 방식**: 장비가 덮는 몸 부위를 숨긴다. "몸 전체를 두고 옷을 겹치는 방식"은 피부가 옷을 뚫어서(실제로 대조군에서 확인) 쓰지 않는다. 옷을 안 입은 상태는 "기본 맨몸 파츠"가 대신한다.
  - **근거의 범위**: "일반 MMO가 이렇게 한다"는 설명은 이 세션의 **일반 지식**이며, 특정 게임의 구현을 이번에 조사·검증한 것이 아니다. 이 프로젝트에 적용할 때는 이 방식이 "피부 뚫림을 없앤다"는 **시험 결과**(3장)를 근거로 삼고, 업계 사례 비교가 필요하면 별도로 조사할 것.
- **아이템마다 가리는 부위가 다르다(데이터)**: 상의(`tunic`)는 몸통+팔, 흉갑(`plate`)은 몸통만(팔이 드러남), 바지는 다리, 장화는 발, 장갑은 손.
- **경계는 옷이 가려야 한다**: 목·손목·발목에서 몸 부위 경계가 보이지 않게 칼라·소매 끝·밑단·장갑·장화가 덮어야 한다. 처음 시험에서 목이 비어 보였고(목 정점이 몸통 부위로 분류됨), 발목과 바지 밑단 사이도 비었다 → 5장 해결 참고.
- **데이터 구조(프로토타입은 Python dict, 실제로는 `Common/DataTable/*.csv`로)**:

```
ITEMS = {
  "tunic":  {slot:"top",    mesh:"SKM_Top_{b}",   hides:["torso","arms"]},
  "plate":  {slot:"top",    mesh:"SKM_Plate_{b}", hides:["torso"]},
  "pants":  {slot:"pants",  mesh:"SKM_Pants_{b}", hides:["legs"]},
  "boots":  {slot:"boots",  mesh:"SKM_Boots_{b}", hides:["feet"]},
  "gloves": {slot:"gloves", mesh:"SKM_Gloves_{b}",hides:["hands"]},
  "helmet": {slot:"head",   static:..., bone:"head",   scale:0.42, rel_loc/rel_rot:...},
  "sword":  {slot:"weapon", static:..., bone:"hand_r", ...},
}   # {b} = 체형 M(Manny) / F(Quinn)
```

- **런타임 교체 함수**: `equip_item(캐릭터, 체형, 슬롯, 아이템ID|None)` = 슬롯 컴포넌트의 메시 교체 → Leader Pose 재연결 → 현재 장착 목록으로 몸 부위 가시성 재계산. (부록 F)

## 5. 문제 → 해결 로그 (가장 가치 있는 부분)

"증상 / 원인 / 해결 / 남은 한계" 순서입니다. **같은 함정을 다시 밟지 않도록** 읽어 두세요.

### 5-1. 에셋 생성·임포트 (VARCO + FBX)

| 증상 | 원인 | 해결 | 남은 한계 |
|---|---|---|---|
| 모든 VARCO 모델이 비슷한 크기(바운드 ±50 cm, 약 100 cm 높이)로 나옴 | VARCO가 정규화해서 내보냄(실제 크기 정보 없음) | 아이템마다 **크기를 직접 정해 스케일**(투구 0.42, 검 1.0, 의상은 몸 부위 상자 대비 비율) | 정규화 때문에 "원래 크기" 힌트가 없다 |
| FBX 임포트가 **아무 에러 없이 실패**(에셋이 안 생김) | Windows 경로 260자 제한(스크래치 경로가 길었음) | FBX를 **짧은 경로**(`UEClient/JM/Saved/VDL/`)로 옮겨 임포트 | 임포트 실패 시 `imported_object_paths`를 반드시 확인 |
| 투구가 얼굴 뒤쪽을 향함 | VARCO 모델의 정면 방향이 매니퀸 정면과 반대 | 180° 회전. **정면·측면 캡처로 반드시 확인** | 아이템 정면 방향 규약이 없다 → 파이프라인에 방향 검수 단계 필요 |
| 의상 소매가 팔 안쪽에 묻힘 | VARCO가 만든 소매 각도 ≠ 매니퀸 A-포즈 팔 각도 | 프롬프트에 "A-pose, sleeves 45도" 지시 + 스케일 키우기(가로 1.2배) | 생성 결과 품질에 좌우됨. 한 번에 안 맞을 수 있음 |
| 장갑이 한 쌍으로 한 메시 | 이미지→3D 특성 | 메시를 **x 부호로 좌/우 분할 → 각 손 본 위치·방향으로 변환 → 합침**(부록 E) | 정밀도 낮음 |
| 생성 프롬프트 | — | 효과 좋은 표현: `ghost mannequin style, hollow invisible body, no person, isolated clothing item only, no background, plain white background` + 정면(front view) | 포즈 제어가 약함 |

### 5-2. Geometry Script (스키닝)

| 증상 | 원인 | 해결 |
|---|---|---|
| 스킨 웨이트 전이로 만든 흉갑이 **몸 안에 묻혀 안 보임** | 처음 크기가 몸통보다 작음 | 비균일 스케일(너비 0.75 / 두께 0.95 / 높이 0.6)과 중심 이동. **크기·위치는 수동**이었으나 아래 "자동 맞춤"으로 일반화 |
| 크기 맞춤을 아이템마다 수작업 | — | **몸 부위 경계 상자 대비 비율로 보정**: Manny에서 수작업 값을 `calibrate()`로 비율화 → `apply_profile()`을 Quinn에 적용(부록 D). Manny 값을 정확히 재현했고 Quinn 값은 비율로 자동 계산됨 |
| 상의·바지 일부가 **통째로 사라져 찢어져 보임** | `remove_small_components` 기본값이 **부피 임계값**을 써서 열린 껍질(옷)은 부피≈0이라 통째로 삭제 | `min_volume = -1e12`, `min_area = 0`, `min_triangle_count = 50`으로 지정(부록 D). **기본값으로 호출하지 말 것** |
| 구멍을 메우고 싶음 | `fill_all_mesh_holes`는 정상 열린 가장자리(소매·목·밑단)까지 막음 | **쓰지 않음**. 필요하면 홀 둘레 길이 기준으로 작은 구멍만 선택 메움을 직접 구현해야 함 |
| 재료가 투명하게 보이는 면 | 얇은 껍질, 뒷면 컬링 | 머티리얼 `two_sided = True`로 설정(임포트된 `Material`에 대해) |
| 에셋을 같은 경로에 다시 만들면 실패/충돌 | 이미 존재 | `does_asset_exist` → `delete_asset` 후 생성. **레벨 액터가 그 메시를 쓰고 있으면 먼저 `set_skeletal_mesh_asset(None)`으로 참조 해제**(안 하면 위험) |
| 새로 만든 스켈레탈 메시의 머티리얼이 비어 있음 | `create_new_skeletal_mesh_asset_from_mesh`는 머티리얼을 지정하지 않음 | 생성 후 `set_editor_property("materials", [SkeletalMaterial(...)])`, 저장(`save_loaded_asset`) |

### 5-3. 몸 숨김 / 몸 분할

| 증상 | 원인 | 해결 |
|---|---|---|
| 달릴 때 몸(피부)이 옷을 뚫고 나옴 | 몸 전체 + 옷 겹침 방식 | **몸을 6부위로 분할**(head/torso/arms/hands/legs/feet), 장비 착용 시 해당 부위 `set_visibility(False)` (각 정점의 **가장 큰 본 가중치** 본 이름으로 분류, 삼각형은 다수결) |
| 분할 메시의 정점 bbox가 이상하게 큼 | `delete_triangles`는 **고립 정점을 남김**(렌더되지 않지만 정점 목록에 남음) | 부위 메시의 정점 위치로 bbox를 계산하지 말고, 에셋의 `get_bounds()` 또는 삼각형 기준으로 계산 |
| **목이 비어 보임**(남녀 모두) | 목 정점이 `spine_05`에 지배되어 몸통 부위로 분류 → 상의 착용 시 함께 숨김 | `neck_01.z - 10` 위이고 `abs(x) < 10`인 몸통 정점은 **머리 부위로 재분류** (부록 B) |
| 바지 밑단과 신발 사이가 빔 | 종아리 정점은 `legs`(숨김), 신발은 `feet` | 종아리 중 **z < 32 cm는 `feet` 부위로 포함**(`ANKLE_Z`), 손목 11 cm 안의 팔뚝은 `hands`로(`WRIST_R`) |
| 흉갑을 입으면 팔이 맨살 | 흉갑의 `hides`가 `["torso"]`뿐 | **의도된 동작**(아이템별 숨김 규칙). 문서/데이터에 명시할 것 |

### 5-4. 에디터 자동화 (uemcp / `ue_python`)

| 증상 | 원인 | 해결 |
|---|---|---|
| `ue_screenshot`이 20초 후 실패 | 에디터 뷰포트가 숨겨져 있거나 포커스 없음 | **SceneCapture2D + `RenderingLibrary.export_render_target`로 PNG 저장**, `Read`로 이미지 확인 (부록 A) |
| 에디터에서 애니메이션이 갱신되지 않음(포즈 값이 안 바뀜) | 에디터가 컴포넌트 애니메이션을 틱하지 않음 | **Simulate 모드**(`ue_play`)에서 확인. 시뮬레이션 액터는 `UnrealEditorSubsystem.get_game_world()` + `GameplayStatics.get_all_actors_of_class`로 접근. 캡처 액터는 **시뮬레이션 시작 전에 레벨에 미리 배치**(`T_CapG`) |
| `ue_stop_play`가 에러 | 도구가 UE 5.6에 없는 함수 호출(기존 `DECISIONS.md` 10-30 기록과 같음) | `LevelEditorSubsystem.editor_request_end_play()` |
| 장비 상대 변환 값이 엉뚱하게 적용됨 | `component.relative_location` 등 **Vector/Rotator는 살아 있는 참조**. 원본 액터를 삭제하면 값이 깨짐 | 값을 **복사(스칼라로 저장)한 뒤** 원본을 삭제 |
| 헬퍼 코드 재사용 | `ue_python`은 호출마다 새로 실행 | `exec(open(path).read())`로 파일에서 불러옴(부록 파일들) |
| 화면에 주황색 경고("…competing to be the single one used for forward shading…") | 방향성 광원 2개 모두 대기 광원 | 보조 광원의 `atmosphere_sun_light=False`, 주 광원 `forward_shading_priority=1` |
| 거부된 삭제가 실행됨 | 1장 사고 기록 참고 | 삭제 명령은 사용자 확인 후에만, 먼저 상태 점검 |
| 파일 복사 시 "Device or resource busy" | 에디터가 로드 중인 .uasset | 같은 내용이면 무시 가능(복사 전후 `cmp`) |
| uemcp 연결이 처음엔 `connected:false` | 에디터가 아직 로딩 중 | 에디터가 완전히 뜬 뒤 `ue_status`로 인스턴스 확인 |
| 게임 시작 전 `Content/Characters` 폴더를 못 씀 | `.gitignore`가 `UEClient/JM/Content/Characters/`를 제외(UE 템플릿 잔재 규칙) | 프로토타입은 그 경로에 매니퀸을 복사했다. **프로젝트에 적용하면 git 추적되는 경로로 옮겨야 한다**(예: `Content/Character/Mannequin/`) |

## 6. 적용 판단 가이드 (JH_MMO의 Claude가 판단할 것)

먼저 **적용할지**를 판단하세요. 아래를 사용자에게 확인하고 `DECISIONS.md`에 남긴 뒤에야 구현 계획으로 넘어갑니다.

### 6-1. 방향 선택지

| 선택지 | 내용 | 비용 | 위험 |
|---|---|---|---|
| **A. 완전 전환** | 플레이어를 매니퀸 스켈레톤 + 모듈형으로 교체. Paragon 영웅은 플레이어에서 제거 | 매우 큼(애니메이션·스킬·타격 노티파이 전체 재작업) | Greystone/Sparrow의 전투 구현(A2.2d/e)이 모두 영향받음. 무기별 모션 세트 확보 필요 |
| **B. 병행** | 몬스터·시연은 기존 Paragon, 플레이어는 매니퀸 모듈형(AnimBP 체계 2개) | 큼 | 두 체계를 모두 유지 |
| **C. 유지(적용 안 함)** | 현재 로드맵 유지, 이 문서는 참고 자료로 보관 | 없음 | 프로젝트가 "일반 MMO식 장비 교체"를 못 보여 줌 |

이 프로젝트의 목표는 "서버·클라이언트 구조 시연 포트폴리오 MMORPG"(`DECISIONS.md` 2026-10-20)다. **장비 외형 교체는 서버 구조 시연에 필수는 아니지만**, A3(아이템/인벤토리/거래)와 시각적으로 연결되는 가치가 크다. 반대로 **P1~P4(성능), A2.3~A2.5**가 먼저 남아 있다. 사용자의 최종 선택이 필요한 사안이다.

### 6-2. 먼저 사용자에게 확인할 질문

1. 방향 A / B / C 중 무엇인가? (사용자는 "완전히 일반적인 MMO 스타일을 원한다"고 말했다. A 또는 B에 가깝다.)
2. **VARCO 약관**(상업적 사용, 출처 표기)과 **Mannequin 사용 조건**을 확인했는가?
3. **외부 DCC(Blender) 허용 범위**: 정리(구멍·웨이트)를 DCC로 할지, 계속 UE 도구만 쓸지.
4. **슬롯 목록**(머리, 상의, 장갑, 하의, 장화, 무기, 어깨·망토 등)과 **체형**(남/여만? 종족?).
5. **무기 타입**(한손검/두손검/활/총 …)과 **애니메이션 세트** 확보 방법.
6. **폴리 예산**과 **LOD**, 다수 플레이어 성능 목표(동접 시연은 서버 부하 중심인가, 클라이언트 렌더링도 보여 줄 것인가).
7. 장비 상태를 어떻게 서버가 전달하는가(`ObjectInfo` 확장 vs 별도 패킷).

### 6-3. 적용 가능성 체크리스트 (현재 저장소 기준)

| 항목 | 현황 | 판단 |
|---|---|---|
| `JM.uproject`에 필요한 플러그인 | `GeometryScripting`, `PythonScriptPlugin`, `ModelingToolsEditorMode`, `StructUtils` **이미 켜짐**. `bRemoteExecution=True` | 에셋 생성 자동화(에디터 전용)는 바로 가능 |
| 에디터 MCP | `.mcp.json`에 uemcp 0.1.0 | 이번 작업에 충분했음(C-7 참고) |
| 런타임 코드 | 기존 `ModularAppearanceComponent`/`ModularEquipmentComponent`는 10-17에 삭제 | 새 컴포넌트를 **새로 설계**해야 함(이름 충돌 주의) |
| 서버 | DB `Player_Equipped`/`equip_items`는 남아 있음, `ObjectInfo.appearance_items`는 `reserved`(번호) | 프로토콜 변경이 필요할 수 있음(`.proto` → `GanPackets.bat`) |
| DataTable 규약 | `Common/DataTable/*.csv` + `DT_*` + `*Row.h` | `ItemAppearanceTable` 등 새 테이블 설계(7장) |
| 아이템 설계 문서 | `DESIGN_ITEMS_TRADE.md`(A3.1)가 아직 **없다** | 장비 외형 설계는 이 문서와 **함께** 작성하는 것이 자연스럽다 |

## 7. 구현 계획 초안 (적용하기로 결정된 경우, 리포지토리 규칙 형식)

> 한 번에 하나의 체크박스만 진행하고, 큰 항목은 차례가 되면 코드를 읽고 하위 단계로 다시 계획하세요. 모든 콘텐츠 단계는 **오류 테스트 + 부하 테스트**(검증 게이트)를 통과해야 완료입니다.

### 사전(코드 이전)
- [ ] M-0 사용자 결정 3건을 `DECISIONS.md`에 **새 항목**으로 추가(이전 항목은 지우지 않음): ① 모듈형 장비 방향(A/B/C), ② 매니퀸 공용 스켈레톤 + 무기 타입별 애니메이션, ③ 외부 DCC 허용 범위. (10-17 "의류/모듈러 없음"과 10-03 "DCC 없음"을 대체/수정하는 항목)
- [ ] M-1 라이선스 확인(VARCO 약관, Epic Mannequin) — 결과를 `DECISIONS.md`에 기록.
- [ ] M-2 `Docs/DESIGN_CHARACTER_EQUIPMENT.md`를 새 방향으로 **개정**(현재 "의류·모듈러 없음"이 적혀 있음). A3.1 `DESIGN_ITEMS_TRADE.md`와 슬롯·ID 체계를 맞춤.

### 에셋 파이프라인(에디터 전용, 서버 영향 없음)
- [ ] M-3 몸 부위 분할 도구를 저장소로 이식(부록 B). 베이스 바디 결정(매니퀸 Simple은 약 9.2만 폴리 → **자체 저폴리 베이스 바디 필요**). 결과물 경로·네이밍은 `DECISIONS.md` 2026-10-19 규칙(`SK_`, `M_`, `T_` …)에 맞춤.
- [ ] M-4 의상 스키닝·자동 맞춤 도구 이식(부록 C, D). 아이템별 `calibrate` 프로파일을 데이터로 저장.
- [ ] M-5 VARCO 생성 → Remesh(폴리 목표) → 임포트 → 맞춤 → 스키닝 **체크리스트화**(8장). 비용·시간 기록.
- [ ] M-6 의상 품질 기준 정의(구멍 허용치, 폴리, 텍스처 크기)와 정리 방식(UE 도구 vs DCC).

### 런타임(C++/BP, 서버 연동)
- [ ] M-7 클라이언트 `UEquipmentComponent`(가칭): 슬롯별 `USkeletalMeshComponent`/정적 컴포넌트, Leader Pose 설정, `EquipItem(Slot, ItemId)`, 몸 부위 가시성 재계산. 프로토타입 로직(부록 F)을 C++로 옮김. `AJMCharacterBase`의 기본 `Mesh`를 **숨김 Leader**로 쓰는 구조를 검토.
- [ ] M-8 DataTable: `ItemAppearanceTable`(클라이언트 전용 열) — 열 초안: `Name`(ItemId), `Slot`, `MeshM`, `MeshF`(소프트 경로), `HidesBodyParts`(예: `torso|arms`), `AttachBone`, `RelLocX/Y/Z`, `RelRot…`, `Scale`. 서버는 장착 가능 슬롯·스탯만 필요(외형 열 무시, `DESIGN_DATA.md` 규약).
- [ ] M-9 프로토콜: 캐릭터 외형 = (체형, 슬롯별 ItemId). `ObjectInfo` 확장 또는 `SC_EQUIP_CHANGED`. **서버 권위**: 장착 검증·저장은 서버, 클라이언트는 반영만. `.proto` 수정 시 `GanPackets.bat` 필수.
- [ ] M-10 몸 부위 숨김 규칙을 데이터로 구동하고, 맨몸/부분 착용 조합 전부 테스트(10가지 이상 조합).
- [ ] M-11 애니메이션: 공용 ABP + 무기 타입별 레이어(Animation Layer Interface). 무기 타입별 모션 세트 확보 후 시험. 스킬 타격 노티파이 체계(기존 AnimNotify 방침) 유지.
- [ ] M-12 성능: 컴포넌트 수·드로우콜 측정, 런타임 병합(`USkeletalMergingLibrary`, C++) 또는 LOD/파츠 폴리 예산으로 조정. 동접 시연 시 클라이언트 비용 기록.

### 검증 게이트(모든 M 단계 공통)
- 오류 테스트: 존재하지 않는 ItemId, 슬롯 불일치, 체형 불일치, 중복 장착, 접속 중 교체, 중도 접속 해제.
- 부하 테스트: 장비 변경 패킷 폭주(봇 `--profile` 추가 검토), 시야 안 플레이어 N명의 외형 동시 조립.
- 시각 검증: **SceneCapture 렌더**(부록 A)로 정면/측면/후면 × (Idle, Walk, Jog, Jump, Attack) 캡처를 `Docs/TESTING.md`에 남긴다.

## 8. 다음에 새로 시작할 때 놓치면 안 되는 것 (체크리스트)

**시작 전**
- [ ] 사용자 승인 범위 확인(포인트 상한, 삭제 금지, 되돌리기 방침, DCC 허용 여부).
- [ ] `DECISIONS.md` 새 항목, 라이선스 확인 완료 여부.
- [ ] 에디터를 완전히 띄운 뒤 `ue_status`로 인스턴스 확인. 프로젝트 레벨 말고 **테스트 레벨**에서 작업.
- [ ] 작업 브랜치를 새로 만들고 시작(프로토타입은 `test/varco-equipment`).

**에셋 만들 때**
- [ ] FBX는 **짧은 경로**로 복사해 임포트, `imported_object_paths` 길이 확인.
- [ ] VARCO 모델은 **크기 정규화**되어 나오므로 아이템마다 스케일을 정한다.
- [ ] **정면 방향 확인**(투구가 뒤를 보고 있었음). 정면/측면/후면 캡처.
- [ ] 머티리얼 `two_sided`, `remove_small_components` 옵션(부피 임계값!), 구멍 메움은 쓰지 않는다.
- [ ] 의상은 **매니퀸 A-포즈 팔 각도**에 맞춰 생성(프롬프트에 포즈 명시).
- [ ] 모델 폴리 수를 **Remesh로 줄이는 단계**를 넣는다(미검증).

**몸 숨김**
- [ ] 목·손목·발목 경계 규칙(`neck_01 - 10`, `ANKLE_Z`, `WRIST_R`)을 체형마다 확인한다(Quinn은 Manny와 값이 다름).
- [ ] 아이템별 `hides` 데이터를 반드시 명시(흉갑은 몸통만).
- [ ] 대조군(몸 숨김 없음)을 항상 하나 두고 비교하되 **줄에 이름을 붙여 혼동을 막는다**(이번에 사용자가 대조군을 보고 "피부가 뚫린다"고 오해했다).

**에디터 자동화**
- [ ] 애니메이션 확인은 **Simulate**(`ue_play`)에서, 시뮬레이션 액터는 `get_game_world()`로 접근, 캡처 액터는 시작 전에 배치.
- [ ] `ue_stop_play`는 쓰지 말고 `editor_request_end_play()`.
- [ ] 컴포넌트의 Vector/Rotator는 복사해서 쓴다(원본 파괴 후 깨짐).
- [ ] 메시를 교체하기 전에 액터의 참조를 해제한 뒤 에셋을 삭제/재생성한다.
- [ ] 레벨을 저장하고 **다른 맵을 열었다 돌아와서** 상태 유지 확인.
- [ ] 방향성 광원은 하나만(두 개면 경고).

**정리(되돌리기)**
- [ ] 삭제는 **사용자 요청 후**에만. 11장 정리 절차를 따른다.

## 9. 미래 작업 후보 (검증되지 않은 아이디어)

- VARCO `Remesh` / `AI_UNWRAP` / `PBR` 노드로 폴리·UV·머티리얼 품질을 올리는 자동화(0포인트 항목).
- Geometry Script로 **오프라인 아웃핏 베이크**(여러 파츠를 하나의 스켈레탈 메시로 합침, `append_mesh_with_materials`). 런타임 병합의 대안(캐시용).
- 몸을 안쪽으로 **줄이는 모프 타깃**/마스크로 몸 부위 분할의 이음새를 없애기.
- Epic **Mutable(Customizable Object)**, **Chaos Outfit Asset**(5.5+ 실험 기능)과 비교.
- 무기 타입별 모션: Lyra/Game Animation Sample 등 외부 세트 평가.
- 체형 변환: Manny→Quinn 외에 비율 기반 자동 맞춤이 다른 체형(어린이·대형)에도 통하는지.
- 다수 캐릭터 동시 성능 측정(PIE에서 `stat unit`, `stat gpu`).

## 10. 산출물 위치 (프로토타입)

모두 `UEClient/JM/` 기준입니다. **삭제하지 않았고, 사용자가 요청하면 11장 절차로 정리합니다.**

| 위치 | 내용 |
|---|---|
| `Content/Test_VARCO/` | 테스트 레벨 `L_Test`, 임포트한 정적 메시(`Sword`, `Helmet`, `Armor`, `Top`, `Pants`, `Boots`, `Gloves`), 스키닝 결과 `Items/`, 몸 부위 `Body/`(옛 분할)와 `Body2/Manny`·`Body2/Quinn`(현재) |
| `Content/Characters/Mannequins/` | 엔진 템플릿에서 복사한 매니퀸·애니메이션(약 126MB, **git 무시 경로**) |
| `Saved/VDL/` | 내려받은 FBX, 스크린샷 PNG, 헬퍼 스크립트(`shot.py`, `shot_g.py`, `body_parts*.py`, `fit.py`, `skin_item.py`, `modular.py`, `gloves.py`, `make_arena.py`, `profiles.json`) — **git 무시, 정리 시 삭제됨. 코드는 부록에 사본이 있다** |
| 레벨 `L_Test` 줄 | `Chars/`(옛 6명, y -375~375), `Modular/`(남녀 6명, y 600~1500), 추가 조합 4명(y 1650~2100). 캡처 액터 `T_CapG`, 조명 `T_Sun`/`T_SunFill`/`T_Sky`, 바닥 `T_Floor` |
| git 브랜치 | `test/varco-equipment`(커밋 없음, 원래 `Jeonghoon`) |
| VARCO 플로우 | `JH_MMO`(flowId `fb0fad37-d136-4ac6-b85c-006e9a98f533`), 아래 노드 ID 표 참고 |
| 이 문서 | `Docs/HANDOFF_MODULAR_EQUIPMENT_VARCO.md`(커밋 안 함, 브랜치를 바꿔도 미추적 파일로 남음) |

**VARCO 노드 ID와 3D 작업 ID**(재다운로드/정리용. 플로우를 지우면 결과도 사라질 수 있음)

| 아이템 | TextInput | GenerateImage | Generate3D | 3D taskId |
|---|---|---|---|---|
| 검 | `9f9f33d1-7c87-4e46-af18-df61d65456a6` | `dd5ac033-b327-4401-9c52-09636061aad8` | `dcb298a0-bb13-4d4d-b28d-63d01e67fb79` | `8a1320d7-5043-4657-b6a6-50f5704ef973` |
| 투구 | `e6084ee1-871b-45f0-ae33-31b82a0da48f` | `796d175b-038e-411c-8655-45da918eca37` | `e6a90d49-41dc-413a-9d1d-329dc8fec61a` | `342a3a9b-4306-4d16-9e64-56a68831af97` |
| 흉갑 | `7c224320-e3f8-4541-8e90-6f45b9641798` | `9cedf0b0-fac4-4902-bb4f-caf83d117e6d` | `7a0f88fe-e5f4-4cc7-b603-dfe8363ca059` | `e7a4ce04-d552-43c4-823c-d191a6dfc9de` |
| 상의 | `a5de9449-e116-401d-8b54-b73b7a34636e` | `cdeab5eb-68db-4d1d-9918-85ebc8e8bca9` | `46584cf8-bbe7-45c6-8e95-b01fbedf6cf6` | `e6adf74e-14af-44ac-b944-58e6a54332a9` |
| 바지 | `df08cc32-1b1a-4a8b-9f68-e5753176c0cb` | `8ab7dee5-cba0-4916-91ac-405d5f8d82fb` | `cf9186a9-7d04-4fb8-a5af-d793c41e41d8` | `3a75b9dc-7c14-4f42-a86f-0cd3dfda2c3b` |
| 장화 | `340d07d7-e545-4621-be24-3272163cde99` | `defd494c-3481-4e15-8c25-4321f29abede` | `4e9a60b2-7091-4f81-867e-b774687a2c93` | `e92603c1-7b14-43f8-9841-de1e6804d0b5` |
| 장갑 | `0d287071-b558-4875-b80c-dac0a2f1fd75` | `c4f108b8-6b8c-495b-8bf0-a27066500d37` | `8d8220c6-4ad9-42e7-a5c5-3ab29ea9ef04` | `17e4b536-ba70-414b-9e0e-2fa587b434ff` |

플로우의 기존 노드(사용자가 만든 템플릿: 고양이 프롬프트 등)는 건드리지 않았다.

## 11. 정리(삭제) 절차 — 사용자가 요청한 뒤에만

1. 에디터에서 **다른 맵**(예: `/Game/Maps/DevMap`)을 연다(`L_Test`가 열린 채로는 지울 수 없음).
2. 테스트 액터가 에셋을 참조하고 있지 않은지 확인한 뒤 `EditorAssetLibrary.delete_directory("/Game/Test_VARCO")`.
3. `Content/Characters/Mannequins` 복사본 삭제. 에디터가 잠근 `.uasset`이 있으면 에디터에서 삭제하거나 재시작 후 삭제. 빈 폴더 `Content/Characters/`도 제거(원래 없던 폴더).
4. `Saved/VDL/` 삭제(git 무시 경로라 `git status`에는 안 나옴).
5. `git status`로 변경 없음 확인 → 원래 브랜치(`Jeonghoon`)로 복귀 → `git branch -d test/varco-equipment`.
6. VARCO 플로우에서 위 노드 ID들을 삭제(결과물 재사용이 필요 없을 때만; 되돌릴 수 없음).
7. 임시 스크립트(`%TEMP%/uedisc*.py`)와 스크래치 폴더 정리.
8. **이 문서를 남길지 지울지는 사용자 결정**(미추적 파일이라 브랜치를 지워도 남음).

## 12. uemcp 사용 메모 (C-7 결정 자료)

- 이번 프로토타입은 `ue_python`(임의 Python)만으로 거의 모든 작업을 수행했다. 전용 도구(`ue_spawn_actor` 등)는 거의 쓰지 않았다.
- 한계: 스크린샷 도구가 뷰포트 가시성에 의존(→ SceneCapture로 대체), `ue_stop_play` 버그(→ 우회), `ue_play`는 Simulate 전용, 에디터가 애니메이션을 틱하지 않음(→ Simulate), 호출 사이에 Python 상태가 유지되지 않음(→ 파일 `exec`).
- 강점: Geometry Script·AssetImportTask·EditorAssetLibrary까지 **에디터 Python 전체**를 쓸 수 있어 에셋 파이프라인 자동화에 충분했다.
- 따라서 C-7에서 EngineWire 같은 C++ 플러그인으로 갈아타더라도 **에셋 자동화 용도로는 uemcp(`ue_python`)가 이미 충분**하다는 근거가 된다. 다만 BP 그래프·UMG 편집은 시험하지 않았다.

---

# 부록: 프로토타입 코드 (원본 그대로)

모두 `UEClient/JM/Saved/VDL/`에서 `exec(open(경로).read())`로 불러 쓰도록 만들었다. 경로는 환경에 맞게 바꿀 것. 에디터 안에서 `ue_python`으로 실행한다.

## A. SceneCapture 스크린샷 (`shot.py`, 에디터 월드) / `shot_g.py` (Simulate 월드)
`shot(name, cam_loc, target, fov, w, h)`는 PNG를 저장하고 경로를 반환 → `Read` 도구로 확인.

`shot.py`

```python
import unreal

OUT_DIR = "C:/Users/kjhkjh/KJH/JH_MMO/UEClient/JM/Saved/VDL"


def shot(name, cam_loc, target, fov=35.0, w=900, h=900):
    """SceneCapture2D로 현재 레벨을 PNG로 저장 (뷰포트가 가려져 있어도 동작)."""
    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    world = unreal.EditorLevelLibrary.get_editor_world()
    for a in eas.get_all_level_actors():
        if a.get_actor_label() == "T_Cap":
            eas.destroy_actor(a)
    cam_loc = unreal.Vector(*cam_loc)
    target = unreal.Vector(*target)
    rot = unreal.MathLibrary.find_look_at_rotation(cam_loc, target)
    cap = eas.spawn_actor_from_class(unreal.SceneCapture2D, cam_loc, rot)
    cap.set_actor_label("T_Cap")
    comp = cap.capture_component2d
    rt = unreal.RenderingLibrary.create_render_target2d(world, w, h, unreal.TextureRenderTargetFormat.RTF_RGBA8)
    comp.texture_target = rt
    comp.fov_angle = fov
    comp.capture_source = unreal.SceneCaptureSource.SCS_FINAL_COLOR_LDR
    comp.capture_every_frame = False
    comp.capture_scene()
    unreal.RenderingLibrary.export_render_target(world, rt, OUT_DIR, name + ".png")
    eas.destroy_actor(cap)
    return f"{OUT_DIR}/{name}.png"
```

`shot_g.py`

```python
import unreal

OUT_DIR = "C:/Users/kjhkjh/KJH/JH_MMO/UEClient/JM/Saved/VDL"


def game_actor(label):
    gw = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
    for a in unreal.GameplayStatics.get_all_actors_of_class(gw, unreal.Actor):
        if a.get_actor_label() == label:
            return a
    return None


def shot_g(name, cam_loc, target, fov=35.0, w=900, h=900):
    """Simulate 중인 게임 월드를 미리 배치한 T_CapG(SceneCapture2D)로 PNG 저장."""
    gw = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
    cap = game_actor("T_CapG")
    cam_loc = unreal.Vector(*cam_loc)
    rot = unreal.MathLibrary.find_look_at_rotation(cam_loc, unreal.Vector(*target))
    cap.set_actor_location_and_rotation(cam_loc, rot, False, False)
    comp = cap.capture_component2d
    rt = unreal.RenderingLibrary.create_render_target2d(gw, w, h, unreal.TextureRenderTargetFormat.RTF_RGBA8)
    comp.texture_target = rt
    comp.fov_angle = fov
    comp.capture_source = unreal.SceneCaptureSource.SCS_FINAL_COLOR_LDR
    comp.capture_every_frame = False
    comp.capture_scene()
    unreal.RenderingLibrary.export_render_target(gw, rt, OUT_DIR, name + ".png")
    return f"{OUT_DIR}/{name}.png"
```

## B. 몸 부위 분할 (`body_parts2.py`)
`build_parts2(메시경로, 출력폴더, 태그)` → `SKM_<태그>_Head|Torso|Arms|Hands|Legs|Feet`. 목·손목·발목 규칙 포함.

`body_parts2.py`

```python
import unreal, time

A = unreal.GeometryScript_AssetUtils
B = unreal.GeometryScript_BoneWeights
N = unreal.GeometryScript_NewAssetUtils
Q = unreal.GeometryScript_MeshQueries
E = unreal.GeometryScript_MeshEdits
L = unreal.GeometryScript_List

SKELETON = "/Game/Characters/Mannequins/Meshes/SK_Mannequin.SK_Mannequin"
GROUPS = ["head", "torso", "arms", "hands", "legs", "feet"]
ANKLE_Z = 32.0      # 이 높이 아래의 종아리는 '발' 부위에 포함 (바지 밑단과 신발 사이 틈 방지)
WRIST_R = 11.0      # 손목 관절에서 이 거리 안의 팔뚝은 '손' 부위에 포함
NECK_HALF_W = 10.0  # 목 기둥 반폭(cm). 이보다 바깥(어깨)은 몸통 부위 유지


def base_group(bone):
    b = bone.lower()
    if b.startswith(("head", "neck")):
        return "head"
    if b.startswith(("hand", "thumb", "index", "middle", "ring", "pinky")):
        return "hands"
    if b.startswith(("upperarm", "lowerarm")):
        return "arms"
    if b.startswith(("foot", "ball")):
        return "feet"
    if b.startswith(("thigh", "calf", "pelvis")):
        return "legs"
    return "torso"


def _copy(path):
    sm = unreal.load_asset(path)
    dm = unreal.DynamicMesh()
    dm, _ = A.copy_mesh_from_skeletal_mesh(sm, dm, unreal.GeometryScriptCopyMeshFromAssetOptions(), unreal.GeometryScriptMeshReadLOD())
    return sm, dm


def build_parts2(mesh_path, out_dir, tag):
    sm, dm = _copy(mesh_path)
    dm, infos = B.get_all_bones_info(dm)
    names = {i.index: str(i.name) for i in infos}
    wrist = {str(i.name): i.world_transform.translation for i in infos if str(i.name) in ("hand_l", "hand_r")}
    neck_z = [i.world_transform.translation.z for i in infos if str(i.name) == "neck_01"][0] - 10.0

    dm, vids, _ = Q.get_all_vertex_i_ds(dm)
    vgroup = {}
    for v in L.convert_index_list_to_array(vids):
        dm, bw, ok = B.get_largest_vertex_bone_weight(dm, v)
        bone = names.get(bw.bone_index, "root") if ok else "root"
        g = base_group(bone)
        if g == "torso":
            p, _ = Q.get_vertex_position(dm, v)
            if p.z > neck_z and abs(p.x) < NECK_HALF_W:   # 목 기둥은 머리 부위로 (상의를 입어도 목은 보이게)
                g = "head"
        if g in ("legs", "arms"):
            p, _ = Q.get_vertex_position(dm, v)
            if g == "legs" and bone.lower().startswith("calf") and p.z < ANKLE_Z:
                g = "feet"
            elif g == "arms" and bone.lower().startswith("lowerarm"):
                side = "hand_l" if bone.lower().endswith("_l") else "hand_r"
                w = wrist[side]
                if ((p.x - w.x) ** 2 + (p.y - w.y) ** 2 + (p.z - w.z) ** 2) ** 0.5 < WRIST_R:
                    g = "hands"
        vgroup[v] = g

    dm, tri_list, _ = Q.get_all_triangle_indices(dm, False)
    tgroup = {}
    for tid, tri in enumerate(L.convert_triangle_list_to_array(tri_list)):
        if tri.x < 0:
            continue
        gs = [vgroup[tri.x], vgroup[tri.y], vgroup[tri.z]]
        tgroup[tid] = max(set(gs), key=lambda g: (gs.count(g), g == gs[0]))

    skeleton = unreal.load_asset(SKELETON)
    unreal.EditorAssetLibrary.make_directory(out_dir)
    made = {}
    for g in GROUPS:
        _, part = _copy(mesh_path)
        kill = [t for t, gg in tgroup.items() if gg != g]
        part, _n = E.delete_triangles_from_mesh(part, L.convert_array_to_index_list(kill, unreal.GeometryScriptIndexType.TRIANGLE), False)
        path = f"{out_dir}/SKM_{tag}_{g.capitalize()}"
        if unreal.EditorAssetLibrary.does_asset_exist(path):
            unreal.EditorAssetLibrary.delete_asset(path)
        sk, outcome = N.create_new_skeletal_mesh_asset_from_mesh(part, skeleton, path, unreal.GeometryScriptCreateNewSkeletalMeshAssetOptions())
        sk.set_editor_property("materials", [unreal.SkeletalMaterial(material_interface=s.material_interface, material_slot_name=s.material_slot_name) for s in sm.materials])
        unreal.EditorAssetLibrary.save_loaded_asset(sk)
        made[g] = (path, sum(1 for x in tgroup.values() if x == g))
    return made
```

## C·D. 자동 맞춤 + 스키닝 (`fit.py`)
`calibrate`(Manny 수작업 값을 비율로) → `apply_profile`(다른 체형에 적용) → `skin_item2`(스키닝). `remove_small_components` 옵션 주의.

`fit.py`

```python
import unreal

A = unreal.GeometryScript_AssetUtils
B = unreal.GeometryScript_BoneWeights
N = unreal.GeometryScript_NewAssetUtils
T = unreal.GeometryScript_MeshTransforms
R = unreal.GeometryScript_MeshRepair

SKELETON = "/Game/Characters/Mannequins/Meshes/SK_Mannequin.SK_Mannequin"
BODY = {
    "M": {"mesh": "/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple.SKM_Manny_Simple", "parts": "/Game/Test_VARCO/Body2/Manny", "tag": "M"},
    "F": {"mesh": "/Game/Characters/Mannequins/Meshes/SKM_Quinn_Simple.SKM_Quinn_Simple", "parts": "/Game/Test_VARCO/Body2/Quinn", "tag": "F"},
}


def region_box(body, groups, z_clip=None):
    """몸 부위 메시들의 기준 포즈 경계 상자 합집합 -> (min, max)."""
    b = BODY[body]
    lo = [1e9] * 3
    hi = [-1e9] * 3
    for g in groups:
        sm = unreal.load_asset(f"{b['parts']}/SKM_{b['tag']}_{g.capitalize()}")
        bb = sm.get_bounds()
        o, e = bb.origin, bb.box_extent
        for i, (oc, ec) in enumerate(zip((o.x, o.y, o.z), (e.x, e.y, e.z))):
            lo[i] = min(lo[i], oc - ec)
            hi[i] = max(hi[i], oc + ec)
    if z_clip:
        lo[2], hi[2] = z_clip
    return lo, hi


def raw_size(static_path):
    bb = unreal.load_asset(static_path).get_bounds()
    return [bb.box_extent.x * 2, bb.box_extent.y * 2, bb.box_extent.z * 2]


def calibrate(static_path, body, groups, manual_scale, manual_center, z_clip=None):
    """Manny에서 수작업으로 맞춘 값을 '몸 부위 상자 대비 비율'로 바꿔 저장."""
    lo, hi = region_box(body, groups, z_clip)
    size = [h - l for l, h in zip(lo, hi)]
    ctr = [(h + l) / 2 for l, h in zip(lo, hi)]
    raw = raw_size(static_path)
    return {"ratio": [raw[i] * manual_scale[i] / size[i] for i in range(3)],
            "rel_off": [(manual_center[i] - ctr[i]) / size[i] for i in range(3)],
            "groups": groups, "z_clip": z_clip}


def apply_profile(static_path, body, prof):
    lo, hi = region_box(body, prof["groups"], prof["z_clip"])
    size = [h - l for l, h in zip(lo, hi)]
    ctr = [(h + l) / 2 for l, h in zip(lo, hi)]
    raw = raw_size(static_path)
    scale = [prof["ratio"][i] * size[i] / raw[i] for i in range(3)]
    center = [ctr[i] + prof["rel_off"][i] * size[i] for i in range(3)]
    return tuple(scale), tuple(center)


def make_two_sided(material_path):
    m = unreal.load_asset(material_path)
    if m and isinstance(m, unreal.Material):
        m.set_editor_property("two_sided", True)
        unreal.EditorAssetLibrary.save_loaded_asset(m)


def skin_item2(static_path, out_path, body, scale, center, material, yaw=0.0):
    b = BODY[body]
    body_sm = unreal.load_asset(b["mesh"])
    skeleton = unreal.load_asset(SKELETON)
    bodymesh = unreal.DynamicMesh()
    bodymesh, _ = A.copy_mesh_from_skeletal_mesh(body_sm, bodymesh, unreal.GeometryScriptCopyMeshFromAssetOptions(), unreal.GeometryScriptMeshReadLOD())
    item = unreal.DynamicMesh()
    item, _ = A.copy_mesh_from_static_mesh(unreal.load_asset(static_path), item, unreal.GeometryScriptCopyMeshFromAssetOptions(), unreal.GeometryScriptMeshReadLOD())

    # 정리: 떠다니는 작은 조각 제거
    ro = unreal.GeometryScriptRemoveSmallComponentOptions()
    ro.min_triangle_count = 50
    ro.min_volume = -1.0e12   # 열린 껍질(옷)은 부피가 0 이하라 기본값이면 통째로 지워짐
    ro.min_area = 0.0
    item = R.remove_small_components(item, ro)

    item = T.scale_mesh(item, unreal.Vector(*scale), unreal.Vector(0, 0, 0), True)
    item = T.rotate_mesh(item, unreal.Rotator(roll=0, pitch=0, yaw=yaw), unreal.Vector(0, 0, 0))
    item = T.translate_mesh(item, unreal.Vector(*center))

    opts = unreal.GeometryScriptTransferBoneWeightsOptions()
    opts.transfer_method = unreal.TransferBoneWeightsMethod.CLOSEST_POINT_ON_SURFACE
    opts.output_target_mesh_bones = unreal.OutputTargetMeshBones.SOURCE_BONES
    item = B.transfer_bone_weights_from_mesh(bodymesh, item, opts)

    if unreal.EditorAssetLibrary.does_asset_exist(out_path):
        unreal.EditorAssetLibrary.delete_asset(out_path)
    sk, outcome = N.create_new_skeletal_mesh_asset_from_mesh(item, skeleton, out_path, unreal.GeometryScriptCreateNewSkeletalMeshAssetOptions())
    mat = unreal.load_asset(material)
    sk.set_editor_property("materials", [unreal.SkeletalMaterial(material_interface=mat, material_slot_name="material_0")])
    unreal.EditorAssetLibrary.save_loaded_asset(sk)
    return sk, outcome
```

## E. 장갑 분할·맞춤 (`gloves.py`)

`gloves.py`

```python
import unreal, math

A = unreal.GeometryScript_AssetUtils
B = unreal.GeometryScript_BoneWeights
N = unreal.GeometryScript_NewAssetUtils
T = unreal.GeometryScript_MeshTransforms
E = unreal.GeometryScript_MeshEdits
Q = unreal.GeometryScript_MeshQueries
L = unreal.GeometryScript_List
SKELETON = "/Game/Characters/Mannequins/Meshes/SK_Mannequin.SK_Mannequin"
BODY_MESH = {"M": "/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple.SKM_Manny_Simple",
             "F": "/Game/Characters/Mannequins/Meshes/SKM_Quinn_Simple.SKM_Quinn_Simple"}
GLOVES = "/Game/Test_VARCO/Gloves/gloves.gloves"
GM = "/Game/Test_VARCO/Gloves/material_0.material_0"


def _copy_static(path):
    dm = unreal.DynamicMesh()
    dm, _ = A.copy_mesh_from_static_mesh(unreal.load_asset(path), dm, unreal.GeometryScriptCopyMeshFromAssetOptions(), unreal.GeometryScriptMeshReadLOD())
    return dm


def make_gloves(body, out_path, phi_right, flip=False, scale=0.2, reach=10.0, y_off=0.0, z_off=0.0):
    """한 쌍 장갑 메시를 좌/우로 나눠 각 손 본 위치·방향에 맞추고 매니퀸 가중치를 옮긴다."""
    bodydm = unreal.DynamicMesh()
    bodydm, _ = A.copy_mesh_from_skeletal_mesh(unreal.load_asset(BODY_MESH[body]), bodydm, unreal.GeometryScriptCopyMeshFromAssetOptions(), unreal.GeometryScriptMeshReadLOD())
    bodydm, infos = B.get_all_bones_info(bodydm)
    bone = {str(i.name): i.world_transform.translation for i in infos}

    src = _copy_static(GLOVES)
    src, vids, _ = Q.get_all_vertex_i_ds(src)
    pos = {}
    for v in L.convert_index_list_to_array(vids):
        p, _ = Q.get_vertex_position(src, v)
        pos[v] = (p.x, p.y, p.z)
    src, tri_list, _ = Q.get_all_triangle_indices(src, False)
    tris = L.convert_triangle_list_to_array(tri_list)

    result = unreal.DynamicMesh()
    for side, sign, bname in [("right", -1, "hand_r"), ("left", 1, "hand_l")]:
        half = _copy_static(GLOVES)
        kill, keep_pts = [], []
        for tid, t in enumerate(tris):
            if t.x < 0:
                continue
            cx = (pos[t.x][0] + pos[t.y][0] + pos[t.z][0]) / 3.0
            if cx * sign > 0:
                keep_pts += [pos[t.x], pos[t.y], pos[t.z]]
            else:
                kill.append(tid)
        half, _n = E.delete_triangles_from_mesh(half, L.convert_array_to_index_list(kill, unreal.GeometryScriptIndexType.TRIANGLE), False)
        c = [sum(p[i] for p in keep_pts) / len(keep_pts) for i in range(3)]
        half = T.translate_mesh(half, unreal.Vector(-c[0], -c[1], -c[2]))
        half = T.scale_mesh(half, unreal.Vector(scale, scale, scale), unreal.Vector(0, 0, 0), True)
        phi = phi_right + (180.0 if flip else 0.0)
        phi = phi if side == "right" else -phi            # 좌우 대칭
        rot = unreal.Rotator(roll=0, pitch=phi, yaw=0)
        half = T.rotate_mesh(half, rot, unreal.Vector(0, 0, 0))
        d = unreal.MathLibrary.quat_rotate_vector(rot.quaternion(), unreal.Vector(0, 0, 1))
        hb = bone[bname]
        target = unreal.Vector(hb.x + d.x * reach, hb.y + d.y * reach + y_off, hb.z + d.z * reach + z_off)
        half = T.translate_mesh(half, target)
        result = E.append_mesh(result, half, unreal.Transform())

    opts = unreal.GeometryScriptTransferBoneWeightsOptions()
    opts.transfer_method = unreal.TransferBoneWeightsMethod.CLOSEST_POINT_ON_SURFACE
    opts.output_target_mesh_bones = unreal.OutputTargetMeshBones.SOURCE_BONES
    result = B.transfer_bone_weights_from_mesh(bodydm, result, opts)
    if unreal.EditorAssetLibrary.does_asset_exist(out_path):
        unreal.EditorAssetLibrary.delete_asset(out_path)
    sk, outcome = N.create_new_skeletal_mesh_asset_from_mesh(result, unreal.load_asset(SKELETON), out_path, unreal.GeometryScriptCreateNewSkeletalMeshAssetOptions())
    mat = unreal.load_asset(GM)
    sk.set_editor_property("materials", [unreal.SkeletalMaterial(material_interface=mat, material_slot_name="material_0")])
    unreal.EditorAssetLibrary.save_loaded_asset(sk)
    return sk, outcome
```

## F. 모듈형 조립·런타임 교체 (`modular.py`)
`ITEMS`(= 이후 CSV 테이블), `spawn_modular`, `equip_item`, `refresh_body_hide`.

`modular.py`

```python
import unreal

# ---- 데이터 (실제 프로젝트에서는 Common/DataTable/*.csv 로 옮길 내용) ----
ITEM_DIR = "/Game/Test_VARCO/Items"
BODY_DIR = {"M": ("/Game/Test_VARCO/Body2/Manny", "M", "/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple.SKM_Manny_Simple"),
            "F": ("/Game/Test_VARCO/Body2/Quinn", "F", "/Game/Characters/Mannequins/Meshes/SKM_Quinn_Simple.SKM_Quinn_Simple")}
GROUPS = ["head", "torso", "arms", "hands", "legs", "feet"]
ANIM = "/Game/Characters/Mannequins/Anims/Unarmed"

# 아이템: slot, 스켈레탈 메시(체형별) 또는 스태틱(본 부착), 가리는 몸 부위
ITEMS = {
    "tunic":  {"slot": "top",   "mesh": "SKM_Top_{b}",   "hides": ["torso", "arms"]},
    "plate":  {"slot": "top",   "mesh": "SKM_Plate_{b}", "hides": ["torso"]},
    "pants":  {"slot": "pants", "mesh": "SKM_Pants_{b}", "hides": ["legs"]},
    "boots":  {"slot": "boots", "mesh": "SKM_Boots_{b}", "hides": ["feet"]},
    "gloves": {"slot": "gloves", "mesh": "SKM_Gloves_{b}", "hides": ["hands"]},
    "helmet": {"slot": "head",  "static": "/Game/Test_VARCO/Helmet/helmet.helmet", "bone": "head", "scale": 0.42,
               "rel_loc": (6.476472, 1.664176, -0.534626), "rel_rot": (-7.630433, 78.175181, -91.303879), "hides": []},
    "sword":  {"slot": "weapon", "static": "/Game/Test_VARCO/Sword/sword.sword", "bone": "hand_r", "scale": 1.0,
               "rel_loc": (0, 0, 0), "rel_rot": (0, 0, 0), "hides": []},
}
SKEL_SLOTS = ["top", "pants", "boots", "gloves"]
STATIC_SLOTS = ["head", "weapon"]

eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
KW = unreal.AttachmentRule.KEEP_WORLD
SNAP = unreal.AttachmentRule.SNAP_TO_TARGET


def _world_actors():
    ues = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
    gw = ues.get_game_world()
    if gw:
        return list(unreal.GameplayStatics.get_all_actors_of_class(gw, unreal.Actor))
    return list(eas.get_all_level_actors())


def find(label):
    for a in _world_actors():
        if a.get_actor_label() == label:
            return a
    return None


def _spawn(cls, loc, rot, label, folder):
    a = eas.spawn_actor_from_class(cls, loc, rot)
    a.set_actor_label(label)
    a.set_folder_path(folder)
    return a


def spawn_modular(name, loc, body, equip, anim_name, anim_enabled=True):
    """슬롯 구조의 모듈형 캐릭터 1명 생성. equip = {slot: item_id}."""
    folder = f"Modular/{name}"
    pos, rot = unreal.Vector(*loc), unreal.Rotator(roll=0, pitch=0, yaw=90)
    bdir, tag, full = BODY_DIR[body]

    leader = _spawn(unreal.SkeletalMeshActor, pos, rot, f"{name}_Leader", folder)
    lc = leader.skeletal_mesh_component
    lc.set_skeletal_mesh_asset(unreal.load_asset(full))
    lc.set_editor_property("visibility_based_anim_tick_option", unreal.VisibilityBasedAnimTickOption.ALWAYS_TICK_POSE_AND_REFRESH_BONES)
    lc.set_animation_mode(unreal.AnimationMode.ANIMATION_SINGLE_NODE)
    data = unreal.SingleAnimationPlayData()
    data.set_editor_property("anim_to_play", unreal.load_asset(f"{ANIM}/{anim_name}"))
    data.set_editor_property("saved_looping", True)
    data.set_editor_property("saved_playing", anim_enabled)
    data.set_editor_property("saved_play_rate", 1.0)
    lc.set_editor_property("animation_data", data)
    lc.set_editor_property("hidden_in_game", True)
    lc.set_visibility(False, False)

    def follower(label, mesh_path):
        a = _spawn(unreal.SkeletalMeshActor, pos, rot, f"{name}_{label}", folder)
        c = a.skeletal_mesh_component
        if mesh_path:
            c.set_skeletal_mesh_asset(unreal.load_asset(mesh_path))
        c.set_leader_pose_component(lc)
        a.attach_to_actor(leader, "", KW, KW, KW, False)
        return a

    for g in GROUPS:
        follower(f"Body_{g}", f"{bdir}/SKM_{tag}_{g.capitalize()}")
    for s in SKEL_SLOTS:
        follower(f"Slot_{s}", None)
    for s in STATIC_SLOTS:
        a = _spawn(unreal.StaticMeshActor, pos, unreal.Rotator(), f"{name}_Slot_{s}", folder)
        a.static_mesh_component.set_mobility(unreal.ComponentMobility.MOVABLE)
        a.attach_to_component(lc, "head" if s == "head" else "hand_r", SNAP, SNAP, SNAP, False)

    char = {"name": name, "body": body}
    for slot, item in equip.items():
        equip_item(name, body, slot, item)
    return char


def _set_comp_visible(actor, visible):
    for c in actor.get_components_by_class(unreal.SkinnedMeshComponent):
        c.set_visibility(visible, False)


def refresh_body_hide(name, equipped_items):
    hidden = set()
    for it in equipped_items:
        if it:
            hidden.update(ITEMS[it]["hides"])
    for g in GROUPS:
        a = find(f"{name}_Body_{g}")
        if a:
            _set_comp_visible(a, g not in hidden)


def equip_item(name, body, slot, item_id):
    """슬롯의 아이템을 교체 (item_id=None 이면 해제). 몸 부위 숨김도 규칙대로 갱신."""
    bdir, tag, _ = BODY_DIR[body]
    if slot in SKEL_SLOTS:
        a = find(f"{name}_Slot_{slot}")
        c = a.skeletal_mesh_component
        if item_id is None:
            c.set_skeletal_mesh_asset(None)
        else:
            c.set_skeletal_mesh_asset(unreal.load_asset(f"{ITEM_DIR}/" + ITEMS[item_id]["mesh"].format(b=body)))
            lc = find(f"{name}_Leader").skeletal_mesh_component
            c.set_leader_pose_component(lc)
    else:
        a = find(f"{name}_Slot_{slot}")
        c = a.static_mesh_component
        if item_id is None:
            c.set_static_mesh(None)
        else:
            it = ITEMS[item_id]
            c.set_static_mesh(unreal.load_asset(it["static"]))
            rl, rr = it["rel_loc"], it["rel_rot"]
            c.set_relative_location_and_rotation(unreal.Vector(*rl), unreal.Rotator(roll=rr[2], pitch=rr[0], yaw=rr[1]), False, False)
            c.set_relative_scale3d(unreal.Vector(it["scale"], it["scale"], it["scale"]))
    # 현재 장착 목록 기준으로 몸 숨김 재계산
    state_key = f"{name}"
    STATE.setdefault(state_key, {})[slot] = item_id
    refresh_body_hide(name, list(STATE[state_key].values()))


STATE = {}
```

## G. VARCO 프롬프트 (효과가 좋았던 것)

- 검: `Single one-handed medieval fantasy longsword, game asset, steel blade with ornate crossguard and leather-wrapped grip, centered, side view, blade pointing up, isolated object, no hand, no background, plain white background`
- 투구: `Single fantasy knight helmet with visor, game asset, steel with gold trim, front three-quarter view, isolated object, no head, no character, no background, plain white background`
- 흉갑: `Fantasy plate chest armor cuirass with shoulder pauldrons, game asset, steel with gold trim, front view, symmetrical, shaped for a human torso, isolated armor piece only, no body, no character, no background, plain white background`
- 상의: `Fantasy long-sleeve tunic shirt with leather bracers, complete garment, front view, sleeves extended downward at about 45 degrees from the body in an A-pose, ghost mannequin style with hollow invisible body, no person, no head, no hands, isolated clothing item only, no background, plain white background`
- 바지: `Fantasy long trousers pants with leather knee straps, complete garment, front view, legs slightly apart, ghost mannequin style with hollow invisible body, no person, no feet, no shoes, isolated clothing item only, no background, plain white background`
- 장화: `Pair of fantasy leather knee-high boots with metal buckles, complete pair standing side by side, front view, ghost mannequin style with hollow invisible legs, no person, no feet, no pants, isolated footwear only, no background, plain white background`
- 장갑: `Pair of fantasy leather gauntlet gloves with metal knuckle plates, complete pair, fingers extended flat, palms facing down, left and right gloves side by side, front view, hollow invisible hands, no person, no hands, isolated gloves only, no background, plain white background`
- Generate3D 설정: `polygonCount` 30000~40000, `topology` tri, `tPose` 0(리깅 안 함), `usePbrTexture` 1, `textureSize` 2048, `generateTexture` 1, `license` PRIVATE(자동). 이미지 모델은 기본값(`gpt-image-2.5-flare`).

## H. VARCO MCP 연결 방법 (이번에 확인)

- 공식 안내: https://3d.varco.ai/blog/ko/2026-07-14-connect-varco-3d-mcp-to-claude-app
- Claude 앱 → Connectors → Custom connector 추가: 이름 `VARCO 3D`, URL `https://3d.varco.ai/api/mcp`, 브라우저에서 Authorize(OAuth). Claude Code에서도 원격 MCP로 사용 가능하다(이번 세션에서 도구가 로드됨).
- 노출 도구: 노드 카탈로그/생성/연결/삭제/이동, `run_flow`, `get_flow_graph`, `get_output_downloads`(obj/fbx/glb/stl), `upload_image`, `get_account_and_pricing`, 애니메이션 카탈로그 등. 계정 등급은 CREATOR였다.
- 노드: `TextInput`, `ImageInput`, `3DInput`, `Rigged3DInput`, `AIAssistant`, `GenerateImage`, `EditImage`, `Generate3D`, `Texture3D`, `Remesh`, `PBR`, `Rig`(humanoid/humanoid-fingers/quadruped), `Animate`, `Annotation`.
