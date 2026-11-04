# 아트·장비 컨셉 (Phase MC)

목표 기준만 기록합니다. 근거와 결정 이력은 [DECISIONS.md](DECISIONS.md) 2026-11-04 항목, 프로토타입 결과는 [HANDOFF_MODULAR_EQUIPMENT_VARCO.md](HANDOFF_MODULAR_EQUIPMENT_VARCO.md)에 있습니다. 이 문서의 값은 모두 확정값이며, 바꿀 때는 DECISIONS에 새 항목을 추가합니다.

## 1. 스타일
- **반실사 판타지 — 스타일라이즈드와 실사의 중간**(2026-11-04 MC2.1b에서 고사실을 수정), 서양 중세 판타지 세계관. 기준은 베이스 바디 키트(Fab CoreCharacters)의 얼굴·피부다. 특징:
  - 인체 비율과 조명 반응은 실사(PBR), 텍스처는 손으로 다듬은 듯한(painterly) 질감 — 만화 윤곽선·셀 셰이딩·망점은 쓰지 않는다.
  - 장식은 또렷하게: 금속 세공, 자수, 천·가죽·금속의 겹침, 술·끈. 형태가 읽히는 정도로 단순화한다.
  - 몸에 맞는 우아한 실루엣, 미형 얼굴.
- **디자인은 자체 제작**: 참고 게임의 이름을 프롬프트에 넣지 않고, 실제 갑옷 세트·캐릭터·로고·문양을 따라 만들지 않는다. 위 시각적 특징만 기준으로 삼는다.
- 비율은 UE 매니퀸(Manny/Quinn) 체형 그대로. 머리를 키우거나 팔다리를 늘리지 않는다(리타게팅 없이 매니퀸 모션을 쓰기 위해). 몬스터 Rampage(Paragon)와 같은 화면에 서도 어색하지 않아야 한다.
- 화면 기준: 기본 카메라(붐 4.5m, 피치 −15°, FOV 90°)에서 **10m 거리(약 110px)에서 티어가 구분**되고, 50m(약 33px, 시야 한계)에서는 실루엣만 보이면 된다(수치는 DECISIONS 2026-10-31 A1.3b 측정값).

## 2. 장비 티어 — 외형은 재질로, 등급은 데이터로
| 티어 | 1차 제작 | 재질·실루엣 | 주조색 |
|---|---|---|---|
| 천 (Cloth) | O | 여행자·수도사풍. 리넨·양모를 겹쳐 입고 자수 테두리, 가죽 벨트·끈. 몸에 붙는 실루엣, 금속은 버클 정도 | 생지 베이지·흙갈색, 포인트 짙은 청색 |
| 가죽 (Leather) | 후속 | 두꺼운 가죽·스터드, 어깨·무릎 덧댐, 실루엣 약간 넓어짐 | 적갈색·암갈색 |
| 판금 (Plate) | O | 세공 문양을 새긴 강철 판금, 금 트림, 가죽 끈. 각진 실루엣, 어깨·팔꿈치·무릎 보호대 | 회색 강철 + 금색 트림 |

- 1차 범위 = **천·판금 2세트 × 6슬롯**(머리/상의/하의/장갑/장화/무기), 세트마다 **한손검·활**을 포함 → 세트당 7개, 합계 14개.
- 티어끼리는 **실루엣이 먼저 다르고**(천은 몸에 붙음, 판금은 각지고 넓음) 색은 그다음이다. 같은 세트의 6부위는 주조색·트림을 맞춰 한 벌로 보이게 한다.
- 등급(Common/Uncommon/Rare)은 `ItemTable` 데이터에만 있고 **외형은 바꾸지 않는다**. 아이콘 테두리 색으로만 구분: Common 회색 `#9D9D9D`, Uncommon 녹색 `#1EFF00`, Rare 청색 `#0070DD`.
- 맨손(무기 없음)은 `ItemTable` 행이 없고 외형 변화도 없다.

## 3. 슬롯별 제작 기준
예산은 DECISIONS 2026-11-04 "모듈형 범위·예산"과 MC2.1b 수정. 장비 합계 약 2.5만 + 몸(얼굴·눈·이·헤어) LOD0 남 약 3.7만 / 여 약 4.9만, **LOD 3단계(100·50·25%)**로 멀리 있는 캐릭터는 가볍게. 텍스처 2048.
**장식 디테일은 지오메트리가 아니라 노멀·러프니스 맵으로 표현**한다(장비 예산 유지). Remesh 후 디테일이 얼마나 남는지는 MC2.2 첫 아이템에서 확인한다.

| 슬롯 | 메시 종류 | 삼각형 예산 | 가리는 몸 부위 | 제작 메모 |
|---|---|---|---|---|
| 몸(얼굴·헤어 포함) | 스켈레탈(부위 분할) | 키트 원본 유지(LOD0 남 3.7만 / 여 4.9만) | — | head(+눈·이)/hair/torso/arms/hands/legs/feet. Fab CoreCharacters 키트를 `body_parts`로 분할(MC2.1b) |
| 머리 | 정적, `head` 본 | 4천 | hair | 천 = 두건·후드, 판금 = 투구. 정면 방향 확인(프로토타입에서 뒤를 봄) |
| 상의 | 스켈레탈 | 6천 | torso (+arms는 아이템별 데이터) | A-포즈 팔 각도(약 45°)로 생성. 칼라가 목 경계를 덮을 것 |
| 하의 | 스켈레탈 | 5천 | legs | 밑단이 장화 윗부분과 겹칠 것(발목 틈 방지) |
| 장갑 | 스켈레탈 | 2천 | hands | 손목 경계를 소매와 함께 덮을 것 |
| 장화 | 스켈레탈 | 3천 | feet | 발목 위까지 올라올 것 |
| 무기 | 정적, 손/등 소켓 | 5천 | — | 한손검·활. 비전투 시 등 소켓. 쥐는 각도·등 위치는 소켓 오프셋 데이터 |

- 어떤 부위를 숨기는지는 아이템마다 데이터로 둔다(예: 천 상의 = torso+arms, 판금 흉갑 = torso). 아이템별 값은 MC 데이터 단계에서 정한다.
- 단단한 것(머리·무기)은 정적 메시, 구부러지는 의상만 스켈레탈. 겹쳐 입기·어깨/망토는 후속.

## 4. 머티리얼·텍스처
- PBR 금속성/거칠기 워크플로, 텍스처 2048. **아이템 하나 = 머티리얼 1개**(드로우콜).
- 피부는 체형별 `MI_Skin_M` / `MI_Skin_F`. 피부 톤 선택 기능은 범위 밖.
- 의상 머티리얼은 프로토타입처럼 `two_sided`를 켜고(안쪽 면), 구멍 메움(`fill_all_mesh_holes`)은 쓰지 않는다(정상적인 소매·목 구멍까지 막힘).

## 5. 아이콘
- 최종 메시를 UE **SceneCapture**로 같은 각도·조명·투명 배경으로 촬영. 테두리(등급 색)는 UI에서 그린다.
- 규격: **512×512 PNG**, 원본은 `SourceArt/Icons/`, 임포트 이름 `T_Icon_<Item>`. 각도는 슬롯별 고정 — 무기 대각선 45°, 방어구 정면 3/4.

## 6. 이펙트
- 스킬 이펙트는 Paragon 이펙트를 `Content/FX/`로 **Duplicate**해 재사용하고, 부족하면 Fab 무료 에셋. 원본 팩 파일은 직접 고치지 않는다(DECISIONS 2026-10-19 방침).

## 7. 제작 파이프라인 (VARCO 기준)
1. 컨셉 이미지: VARCO `GenerateImage`. 원본은 `SourceArt/Concept/`에 보관.
2. 3D: `Generate3D`. 폴리 감소는 맞춤 도구가 UE `apply_simplify_to_triangle_count`로 슬롯 예산까지 줄인다(MC2.1 검증, 노멀 맵으로 겉모습 유지). VARCO `Remesh`는 첫 아이템에서 품질만 비교.
3. 임포트: FBX를 짧은 경로로 복사해 가져오고, 크기 정규화·정면 방향을 아이템마다 확인.
4. 맞춤·스키닝: `Content/Python/jm_modular/garment_fit.py`(MC2.1) — 슬롯별 허용 본, 관통 보정, 양면 머티리얼. 큰 어깨 보호대는 몸통과 한 메시로 만들면 팔을 들 때 늘어나므로 작게 디자인한다.
5. 시각 검증: SceneCapture 시트(정면/측면/후면 × Idle·Walk·Attack)를 사용자가 승인하고 `TESTING.md`에 기록.

**프롬프트 공통 규칙** (프로토타입 부록 G에서 효과가 확인된 표현 + 스타일):
- 공통 끝말: `game asset, semi-realistic stylized fantasy MMORPG style, painterly hand-crafted PBR textures, clear ornate details, layered cloth leather and metal, isolated object, no background, plain white background` (MC2.2 첫 아이템에서 키트 바디와 나란히 보고 조정)
- 참고 게임 이름이나 실제 아이템 이름은 넣지 않는다.
- 의상: `ghost mannequin style with hollow invisible body, no person`, 상의는 `sleeves extended downward at about 45 degrees from the body in an A-pose`
- 정적 물체: 무기는 `side view, blade pointing up, no hand`, 투구·두건은 `front three-quarter view, no head`
- 세트 재질 문구(같은 세트는 고정):
  - 천: `layered undyed linen and wool with embroidered trim and leather belts and straps, earthy beige and brown with dark blue accents`
  - 판금: `polished steel plate with engraved filigree patterns, gold trim and leather straps`
- `Generate3D`: `topology` tri, `tPose` 0, `usePbrTexture` 1, `textureSize` 2048, `license` PRIVATE.

**비용 추정** (프로토타입 측정 약 250포인트/개, Remesh 0포인트): 1차 14개 ≈ 3,500포인트 + 재생성 여유. VARCO 포인트는 단계마다 예상 비용을 알리고 승인 후 사용.

## 8. 열린 항목 (다음 단계에서 결정)
- 프로토타입 의상(VARCO, 고사실 프롬프트)은 시험용일 뿐이고, 장비는 반실사 스타일로 새로 생성한다(MC2.2).
- 여성 헤어 1.66만 삼각형 — LOD0 부담이 크면 헤어만 단순화 검토.
- Remesh 결과가 예산 안에서 장식 디테일을 유지하지 못하면 폴리 예산 또는 생성 설정을 재검토(MC2.2 첫 아이템).
