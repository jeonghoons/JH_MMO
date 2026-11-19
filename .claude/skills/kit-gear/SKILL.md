---
name: kit-gear
description: 장비·의상 메시를 캐릭터에 맞추고 검사·촬영할 때 사용. jm_kit의 preview(테스트 레벨 조립), skeleton_check, garment(의상 맞춤·정적 부착), icon, body_mask, item_table, posed(자세별 측정) 사용법.
---

# jm_kit 장비 도구 사용법

MCP `KitTools`(프로젝트 툴셋 `jm_mcp`)로 부른다. 메시 생성 요령은 `varco-gear` 스킬, 확인용 레벨 정리 규칙은 `pie-check` 스킬.

- `jm_kit` 기본: `kit_profiles.json`(체형별 리더·키트 부위·기본 의상, 슬롯별 숨김 부위), `preview`(테스트 레벨 `Dev/MC/L_KitTest`에 조립 `spawn`, SceneCapture `shot`/Simulate 중 `shot_game`, `look_at`). 예: `import importlib, jm_kit.preview as pv; importlib.reload(pv); pv.setup_level(); pv.spawn("KM", "M", (0, 0, 0), outfit="kit", anim=pv.ANIM["jog"])`
- `jm_kit.skeleton_check` — 장비 메시 뼈대 검사(MC1.3): `check(메시, "M", slot="top")`(기준 = 키트 의상 또는 리더, 다르면 `ok: False`와 이유), `self_test()`. 장비를 만들면 반드시 통과시킵니다.
- `jm_kit.garment` — 의상 맞춤(MC2.1): `import_fbx`(짧은 경로에서), `build(정적 메시, "M", "top", 출력 경로)` = 키트 의상 복제 후 형상·가중치·머티리얼만 교체(뼈대·바인드·PA 상속, 결과에 `skeleton_ok`·`limbs_ok`), `build_gloves`(틀 없음 — 오른손 맞춤·좌우 반전·몸 메시 복제), `build_gloves_kit_hand`(키트 손 형상 + `glove_paint` 무늬, ORM 포함 — 판금은 `M_Equip_PBR`, `fit.clean_cuff`면 손목 가장자리를 팔뚝 축으로 펴서 톱니 제거), 방패는 정적 부착 `fit: "shield"`(`hand_l`, `face_dir`·`face`·`up`으로 앞면·위 축, 손바닥에서 `gap_cm` — MC4.2f), 투구는 정적 부착 `fit: "helm"`(머리 상자 균일 배율·`yaw`, `attach.target_tris`면 가져올 때 `reduce_static_lods`로 LOD 3단계 축소 — 고폴리 생성용), `fit.clear_cm`(몸과 최소 거리 — 삼각형이 큰 판금 상의 사이로 몸이 비치는 것 방지), `static_attachment`(두건·무기 본 부착값), `build_item(키)`·`import_source`(items.json 기준), `measure`. `preview.spawn`의 outfit 값에 items.json 키(예 `"Cloth_Sword"`)를 쓰면 체형별 메시·정적 부착을 알아서 처리합니다.
- `jm_kit.icon` — 장비 아이콘(MC2.4): `shoot(키 목록)` = items.json 아이템을 같은 조명·각도(방어구 정면 3/4, 무기 45° 대각선, 장갑 오른손 손등)로 투명 배경 촬영 → `SourceArt/Icons/T_Icon_<Item>.png`(512²) + 아이템 폴더 `T_Icon_<Item>`, 확인 시트 `Saved/MCShots/icons_sheet.png`. 아이템별 각도는 items.json `icon`(`yaw`·`pitch`·`margin`). MCP 도구 인자로 선택 목록(`list[str] | None`)은 변환이 실패하므로 쉼표 문자열로 받습니다.
- `jm_kit.body_mask` — 몸 마스킹(2026-11-05): `build_mask("M"/"F")`(키트 몸 UV에 영역·거리 마스크 래스터화 → `SourceArt/Masks` PNG → `Character/Body/Masks`), `build_material()`(`M_Body_Masked`·`MI_Body_<M|F>`), `apply(컴포넌트, 체형, 파라미터)`. `preview.spawn`이 입은 슬롯의 마스크를 리더(= 보이는 몸, 키트 `Body_Combined`)의 몸 섹션에 자동 적용합니다.
- `jm_kit.item_table` — 데이터 테이블 생성(MC4.1): `export(write=True)` = `ItemTable.csv` 외형 열(`AttachSocket`·`AttachM`·`AttachF`·`Mask`·`HideHair`)과 `BodyTable.csv`를 `items.json`·`kit_profiles.json`에서 계산해 씀(MCP `item_table_export`). 이 열은 손으로 고치지 않고, 쓴 뒤 `reimport_csv`로 DT를 다시 채웁니다. 메시가 없는 행(`Slot` `None` — 소비 아이템)은 건너뜁니다(결과 `skipped`).
- `jm_kit.posed` — 자세별 측정·촬영(MC4.2e, MCP `posed_run`·`posed_grip_search`): 동작 프레임(`POSES` — 활·검 대기·당김·스킬 등)으로 캐릭터 자세를 잡고(loadout `shield`면 검 자세에 방패도 붙여 앞·뒤 촬영) 장갑·무기 관통(맨손 기준값 포함), 상의 어깨 뭉침(어깨 관절 반경 삼각형 넓이 비·위치·몸 안 비율)을 재고 상체·어깨·손을 촬영(`Saved/MCShots/MC42e_*`). 키트 상의 경로를 넣으면 기준값. `grip_search`는 손잡이 수직 평면에서 쥐는 위치(`items.json` `attach.offset`, 손 본 로컬)를 찾는다. 자세는 C++ `UAnimAssetBuilder::PoseComponentAtTime`(에디터 월드는 틱하지 않음) — 눈·이·헤어 같은 Leader Pose 팔로워는 에디터 월드에서 따라오지 않아 숨긴다.
