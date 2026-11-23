---
name: varco-gear
description: VARCO 3D로 장비(방어구·무기·방패·투구)나 맵 소품(마을·던전 소품) 메시를 생성할 때 사용. 단일 이미지 3D 생성 요령, 3D 생성 전 멀티뷰 확인, 포인트 비용, UE 반입 전 확인 절차.
---

# VARCO 장비 생성 요령

MC2.3 판금 세트(2026-11-06)에서 얻은 요령이다. 투구는 4번 실패했다(누적 약 2,250 + 1,000포인트). 성공 조건은 **사슬 제거 + 눈높이 30° 회전 입력(초록 단색 배경) + 3만 폴리 생성 → UE `reduce_static_lods`로 4천**이었다.

## 플로우 (D-279, 2026-11-23)

플로우 하나에 노드가 120개를 넘어 나눴다. 기준과 화풍 규칙은 `Docs/DESIGN_ART_CONCEPT.md` §7-1~7-3에 있다.

| 플로우 | flowId | 쓰임 |
|---|---|---|
| `JH_MMO_Style` | `f2468d14-3c35-4dbf-a6de-1da4860da5d9` | 기준 이미지 9장 + 문구 메모. 생성 노드 없음 |
| `JH_MMO_Equip_00` | `afdfa486-22d8-4c18-8a07-c1cde781706b` | 00 천 |
| `JH_MMO_Equip_01` | `cbf27b78-95ca-4b3b-8cd9-9fe5b395740e` | 01 가죽 |
| `JH_MMO_Equip_02` | `cced49d3-f7aa-4eb2-a1f2-761defd9ee6c` | 02 강철 |
| `JH_MMO_Equip_03` | `734624d4-daf4-4c46-a889-dee7a24d1081` | 03 근위 |
| `JH_MMO_Env_Village` | `d8d1b742-2f04-4cc2-a878-7ad52df1a3c3` | 마을·정거장·성벽 소품 |
| `JH_MMO_Env_Dungeon` | `0d842431-6c9c-4a2a-8dfc-9fc0540cb7e9` | 던전 소품 |
| `JH_MMO_NPC` | `68d1c4bb-1494-4d11-a3b8-c91408483ee6` | 마을 NPC·몬스터(사람형 의상 줄 + 동물 줄) |
| `JH_MMO_Archive` | `fb0fad37-d136-4ac6-b85c-006e9a98f533` | 옛 `JH_MMO`. 보관 전용, 새 노드 금지 |

- **모든 VARCO 호출에 `flowId`를 넣는다.** 생략하면 브라우저에서 마지막으로 연 플로우에 작업이 들어간다.
- 기록(`TESTING.md` 등)은 `플로우 이름/노드ID`로 적는다.
- 각 플로우 위쪽에 템플릿 한 줄이 있다: `TextInput` → `GenerateImage`(기준 이미지 참조) → `EditImage`(멀티뷰) → `Generate3D`. 새 에셋은 이 줄을 복제해서 쓴다.
- 기준 이미지는 출력 URL(`https://3d.varco.ai/api/objects/<해시>.png`)을 `ImageInput`에 그대로 넣는다. 다른 플로우의 URL도 다시 올리지 않고 쓸 수 있다.
- 이미 만든 모델은 다시 만들지 않는다. 새 플로우는 새로 만드는 에셋에만 쓴다.

## 화풍 수치 검사 (D-279)

- 멀티뷰 확인 다음, 3D 생성 전에 돌린다: `python -I -X utf8 .claude/skills/varco-gear/scripts/lineup.py <시트.png> --stats <새 컨셉> <같은 묶음 승인 컨셉들>`
  - 수치만 보려면 출력 파일 자리에 `-`를 넣는다.
- 통과 조건: 같은 묶음 평균과 채도 차이 ±0.08 이내 + 눈으로 색·톤 / 질감·화풍 / 시대감 각 17/20 이상. 세계 기준 채도는 0.36이다.
- 2026-11-23 사례: 01 가죽은 컨셉 채도 0.53으로 불합격이었다. 이미 만든 9개는 다시 생성하지 않았다. 대신 `M_Equip_PBR`의 `Desat`을 0.4로 넣어 아이콘 채도를 0.69에서 0.54로 낮췄다(`WorldTools.world_step` `eq_color {prefix "01_", desat 0.4}`, 0포인트).
- UE 아이콘 수치는 조명이 달라 컨셉 기준값과 비교하지 않는다. 아이콘끼리 비교한다.
- 아이콘을 다시 찍을 때(`kit-gear` `icon`):
  - 에디터를 새로 켰거나 부모 머티리얼을 다시 컴파일했으면, 첫 촬영이 비거나 회색 또는 흐리게 나온다.
  - 먼저 `world_step` `eq_icon_warm {keys}`(텍스처 완전 로드)를 하고 다음 호출에서 `icon_shoot`을 한다. 끝나면 `eq_icon_warm_clear`를 한다.
  - 촬영 전에 백그라운드 절전(`bThrottleCPUWhenNotForeground`)을 끄고, 끝나면 되돌린다.

## 입력 이미지

- 로컬 이미지 업로드는 base64를 도구 인자에 통째로 실어야 해서 1MB급 PNG는 불가 → VARCO 안에서 `EditImage`로 기존 노드 이미지를 편집한다(배경 교체 등). 기본 모델은 장당 약 50포인트.
- 위에서 내려다본 시점, 촘촘한 디테일(사슬), 낮은 폴리 설정은 형상이 무너진다. 눈높이, 단순한 형상, 단색 배경을 쓴다.

## 3D 생성 전 멀티뷰 확인 (필수, 2026-11-18 사용자 지시)

컨셉 이미지 한 장만 보고 3D를 만들지 않는다. **멀티뷰 시트를 뽑아 어느 방향에서 봐도 컨셉에 맞는 물체인지 확인한 뒤** `Generate3D`를 돌린다. 3D가 비용의 대부분이라(아래 요금) 장당 50포인트로 미리 거른다.

1. 컨셉: `TextInput` → `GenerateImage`(물체 하나, 눈높이 3/4, 단색 배경).
2. 멀티뷰: 컨셉 이미지 → `EditImage`, 지시문 예 — `Turnaround reference sheet of this exact same object on a plain white background: front view, left side view, back view, and three-quarter top view in a 2x2 grid. Keep the same design, materials and proportions. No text, no labels.` (전용 멀티뷰 노드는 노드 목록에 없다 — 2026-11-18 확인.)
3. 확인(시트를 내려받아 직접 본다):
   - 컨셉에 맞는가 — 프로젝트 기준은 반실사 서양 중세 판타지(`Docs/DESIGN_ART_CONCEPT.md` §1). 현대 사물(공원 벤치, 플라스틱, 볼트·용접 자국, 캠핑용품, 글자·로고)이 보이면 탈락.
   - 뒷면·옆면이 말이 되는가(앞만 그럴듯하고 뒤가 비어 있거나 다른 물체가 되지 않는가).
   - 3D로 만들기 쉬운 형상인가 — 얇은 끈·사슬·촘촘한 격자, 공중에 뜬 조각, 여러 물체가 흩어진 구성은 무너진다.
   - 쓸 자리에 맞는 비율인가(예: 목책 한 칸은 가로로 길고 양 끝이 이어 붙일 수 있게 평평한가).
4. 탈락이면 프롬프트를 고쳐 컨셉부터 다시(50포인트). 통과한 것만 `Generate3D`.
5. 멀티뷰 시트는 확인용이다. **`Generate3D`에는 물체가 하나만 있는 컨셉 이미지를 넣는다**(시트를 넣으면 물체 네 개가 한 덩어리로 만들어진다).
6. 컨셉·멀티뷰 이미지는 `SourceArt/Concept/`에 보관하고, 노드 ID·판정·포인트를 `TESTING.md`에 적는다.

요금(2026-11-18, CREATOR 등급): `GenerateImage`·`EditImage`는 **장당 50포인트**(`get_account_and_pricing`에는 20으로 나오지만 기억된 모델 `gpt-image-2.5-flare`로 9장을 돌리면 `run_flow`가 450을 돌려줌 — 모델에 따라 다름), `Generate3D`(텍스처 포함) 200포인트, `Remesh`·`PBR` 0포인트. 실제 차감 예상치는 `run_flow` 응답의 `points`로 확인하고, 사용자에게 알린 예산을 넘기 전에 다시 묻는다.

도구 사용 메모(2026-11-18): 플로우 `JH_MMO`는 노드가 120개가 넘어 `get_flow_graph` 결과가 18만 자다 → 상태 확인은 노드별 `get_output_downloads(nodeId)`로 한다. 이미지 주소는 `https://3d.varco.ai` + 응답의 `/api/objects/<해시>.png`이고 로그인 없이 내려받아진다. `run_flow`는 반드시 `scope`에 돌릴 노드 ID를 넣는다(비우면 옛 노드까지 전부 돈다). 프롬프트 맨 앞에 구분용 이름(`L2d 01 Palisade.`)을 넣어도 이미지에 글자로 나오지 않았다.

## 맵 소품 (L2d 마을, 2026-11-18)

- **얇고 긴 물체는 정면 이미지로 3D를 만들지 않는다.** 목책 한 칸을 정면에 가까운 컨셉으로 넣었더니 사방이 막힌 "네모난 우리"가 나왔다(200포인트 낭비). 멀티뷰는 통과했어도 `Generate3D`에 넣는 이미지가 두께를 보여 주지 않으면 이렇게 된다 → `EditImage`로 "40° 돌려 위에서 살짝 내려다본, 통나무 한 줄 두께의 얇은 벽"으로 바꾼 이미지를 넣어 해결. 벽·울타리·간판·문짝 같은 판 모양은 처음부터 3/4 각도로 만든다.
- 통과한 예: 문틀, 가판대, 깃발(가는 장대도 유지됨), 모닥불, 상자·통 더미, 차양, 수레, 천막 — 3만~5만 폴리로 생성해 UE에서 3천~1만으로 줄임.
- 반입: FBX를 짧은 경로(`C:/Temp/L2d/`)에 받아 `world_step(name, "prop_import", {"fbx", "prop", "target_tris"})`(`jm_kit/props.py`) — `/Game/Environment/<테마>/<분류>/<이름>/`에(D-245 — `prop`에 `Village/Props/Cart`처럼 테마 경로를 주고, 이미 있는 소품은 이름만 줘도 됨) `SM_`·`M_`(공용 PBR 인스턴스)·`T_` 이름으로 정리하고 LOD 3단계를 만든다. 같은 이름으로 다시 가져오면 폴더를 비우고 가져온다(안 비우면 새 메시에 옛 텍스처가 물림).
- VARCO 메시는 긴 변이 1m로 정규화돼 오고(pivot 바닥), 앞면 방향이 제각각이다 → 실제 크기로 `probe`해 두 방향(yaw 0·90)에서 찍어 앞면과 긴 축을 확인한 뒤 배치 값(`yaw_off`, `width_m`)을 정한다. 장비처럼 GLB를 6방향 렌더하지 않아도 UE `probe` 캡처로 형상 확인이 된다.
- 배치와 충돌은 `world-build` 스킬의 `jm_kit.world` `dress`(`ring_prop`·`gate_prop`·`prop`). 배치 값을 만든 스크립트는 이 폴더 `scripts/l2d_update_village.py`(정의 파일의 마을 항목을 다시 만든다).

## 생성·반입

- 단단한 정적 장비는 **고폴리로 생성하고 UE에서 줄인다**(`items.json` `attach.target_tris` → 가져올 때 `garment.reduce_static_lods`로 LOD 3단계).
- UE에 넣기 전에 GLB를 6방향으로 렌더해 형상을 확인한다(시점 라벨 방향 주의). 렌더 스크립트를 새로 만들면 **이 스킬 폴더에 저장**한다(scratchpad는 세션이 끝나면 사라짐 — 이전 스크립트는 그렇게 잃었다).
- 투구는 정적 메시 + `head` 소켓 `fit: "helm"`, 방패는 `fit: "shield"`. 도구 사용법은 `kit-gear` 스킬의 `jm_kit.garment`.
- 실행 전에 예상 포인트를 사용자에게 알리고, 노드 ID와 사용 포인트는 `TESTING.md`에 기록한다.

## 연결 문제

세션 시작 시 `unreal` MCP 연결이 실패해도 에디터 서버(127.0.0.1:8000)는 살아 있을 수 있다 → JSON-RPC(`initialize` → `tools/call` `call_tool`)로 직접 호출할 수 있다.
