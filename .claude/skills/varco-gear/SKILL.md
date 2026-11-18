---
name: varco-gear
description: VARCO 3D로 장비(방어구·무기·방패·투구)나 맵 소품(마을·던전 소품) 메시를 생성할 때 사용. 단일 이미지 3D 생성 요령, 3D 생성 전 멀티뷰 확인, 포인트 비용, UE 반입 전 확인 절차.
---

# VARCO 장비 생성 요령

MC2.3 판금 세트(2026-11-06)에서 얻은 요령이다. 투구는 4번 실패했다(누적 약 2,250 + 1,000포인트). 성공 조건은 **사슬 제거 + 눈높이 30° 회전 입력(초록 단색 배경) + 3만 폴리 생성 → UE `reduce_static_lods`로 4천**이었다.

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
- 반입: FBX를 짧은 경로(`C:/Temp/L2d/`)에 받아 `world_step(name, "prop_import", {"fbx", "prop", "target_tris"})`(`jm_kit/props.py`) — `/Game/World/Props/<이름>/`에 `SM_`·`M_`(공용 PBR 인스턴스)·`T_` 이름으로 정리하고 LOD 3단계를 만든다. 같은 이름으로 다시 가져오면 폴더를 비우고 가져온다(안 비우면 새 메시에 옛 텍스처가 물림).
- VARCO 메시는 긴 변이 1m로 정규화돼 오고(pivot 바닥), 앞면 방향이 제각각이다 → 실제 크기로 `probe`해 두 방향(yaw 0·90)에서 찍어 앞면과 긴 축을 확인한 뒤 배치 값(`yaw_off`, `width_m`)을 정한다. 장비처럼 GLB를 6방향 렌더하지 않아도 UE `probe` 캡처로 형상 확인이 된다.
- 배치와 충돌은 `UEClient/JM/CLAUDE.md`의 `jm_kit.world` `dress`(`ring_prop`·`gate_prop`·`prop`). 배치 값을 만든 스크립트는 이 폴더 `scripts/l2d_update_village.py`(정의 파일의 마을 항목을 다시 만든다).

## 생성·반입

- 단단한 정적 장비는 **고폴리로 생성하고 UE에서 줄인다**(`items.json` `attach.target_tris` → 가져올 때 `garment.reduce_static_lods`로 LOD 3단계).
- UE에 넣기 전에 GLB를 6방향으로 렌더해 형상을 확인한다(시점 라벨 방향 주의). 렌더 스크립트를 새로 만들면 **이 스킬 폴더에 저장**한다(scratchpad는 세션이 끝나면 사라짐 — 이전 스크립트는 그렇게 잃었다).
- 투구는 정적 메시 + `head` 소켓 `fit: "helm"`, 방패는 `fit: "shield"`. 도구 사용법은 `UEClient/JM/CLAUDE.md`의 `jm_kit.garment`.
- 실행 전에 예상 포인트를 사용자에게 알리고, 노드 ID와 사용 포인트는 `TESTING.md`에 기록한다.

## 연결 문제

세션 시작 시 `unreal` MCP 연결이 실패해도 에디터 서버(127.0.0.1:8000)는 살아 있을 수 있다 → JSON-RPC(`initialize` → `tools/call` `call_tool`)로 직접 호출할 수 있다.
