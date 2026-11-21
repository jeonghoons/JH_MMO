---
name: world-build
description: 맵(대륙·마을·던전 레벨)을 만들거나 고칠 때 사용. jm_kit.world와 MCP WorldTools로 지형 정의에서 Landscape 생성, 블록아웃, 마커, 지면 레이어·풀, 자연물·마을 소품 배치와 충돌, 조명, PIE 걷기 확인·성능 측정.
---

# 맵 제작 (jm_kit.world, MCP WorldTools)

설계와 제작 순서는 `Docs/DESIGN_WORLD.md`. 소품 메시 생성은 `varco-gear` 스킬, 내보내기·내비메시는 `nav-export` 스킬.

**먼저 지킬 것**: 실행 전에 `SceneTools.get_current_level`로 열린 레벨을 확인한다. 보이는 메시의 충돌은 프로필을 `NoCollision`으로 바꿔서 끈다. 충돌 윗면은 지면 위 1m 이상. 배치 뒤에는 `check_dress`가 0이어야 한다.

`jm_kit.world` — 맵 제작(L2, MCP `WorldTools`, 설계 `Docs/DESIGN_WORLD.md`): 지형은 손으로 조각하지 않고 지형 정의 `world_<이름>.json`(m 단위, x = 북·y = 동 — 기복 `rolling`, 둔덕 `mounds`, 평지 `flats`, 가장자리 능선 `border`, 구역 `zones`)의 식으로 만든다. `world_new_level`(World Partition 아닌 빈 레벨) → `world_basic_lighting` → `world_build_landscape(이름, 재질, replace)` = `.r16`(`Saved/World/`) 생성 후 C++ `ULandscapeBuildLibrary::CreateLandscape`(기존 지형은 지우고 다시 만듦 — 정의를 고친 뒤 다시 부르면 됨, 결과에 걷는 구역 최대 경사와 30°~60° 비율), `world_describe_landscape`, `world_height_at(이름, [[x_cm, y_cm]])`(마커·소품을 지면에 놓을 높이). 

블록아웃(L2b): `world_blockout(이름)` = 정의의 `blockout` 목록(`box`·`ring`·`scatter`·`arch` — `box`의 `"sink"`(m)는 그만큼 지면 아래로 내림: Landscape 없는 레벨의 바닥 상자는 두께만큼 내려 윗면을 높이 0에 맞춤. 예: 임시 던전 `world_continent01_dungeon.json`, DG3c-1 — `border.height_m` 0·`rolling` 빈 목록이면 지면 높이가 0인 평평한 레벨이고 `world_build_landscape`는 부르지 않음)대로 기본 도형을 `BO_*` 라벨·`Blockout` 폴더에 놓음(다시 부르면 지우고 다시, 시드 고정), `world_place_markers(이름)` = 정의의 `markers`(`player_start`, `monster` 묶음 — `flat_margin_m`으로 평지에서 떨어뜨림, `portals` 목록 — 같은 레벨의 짝 포털은 `target_map`이 이 레벨·`target_point`가 짝의 id)대로 `ALevelMarker`(C++ `SpawnLevelMarker` — `MarkerParams`가 `FInstancedStruct`라 Python으로 못 채움)와 PIE용 `PlayerStart`(L3, 2026-11-18: 스폰 묶음의 `zone`이 마커 `ZoneName`으로 들어가고 — 서버가 `SpawnTable`에서 종류·수를 찾는 키, `safe_zones` 목록(`{"id", "label", "x", "y", "radius_cm"}`, 좌표 m)은 `SafeZone` 마커로 놓음. **열린 레벨에 놓고 그 레벨의 `PlayerStart`를 지우므로 실행 전에 `SceneTools.get_current_level`로 레벨을 확인**(2026-11-18 `DevMap`에 놓은 실수 — 저장 전에 결과의 `removed: 0`으로 발견)), `world_list_markers`, `world_set_game_mode`(`Continent01`은 2026-11-18 L2e부터 실제 GameMode `/Game/_Core/Blueprints/BP_GameMode` — 서버 없이 걸어 볼 때만 `BP_TestGameMode`로 바꿨다가 되돌림). 

구역(DG3b, D-215·D-223): 정의의 마커 항목·묶음마다 `"area"`(`town`/`field`)를 적으면 마커의 `Area`로 들어가 로직 JSON의 `area`로 나갑니다 — 서버가 같은 레벨을 마을 Room과 사냥터 Room으로 나눌 때 그 마커를 쓰는 Room을 정합니다(둘 다 있어야 나뉨). 시작 지점은 `player_start`(PointId 0, PIE용 `PlayerStart`도 이 자리) + `player_starts` 목록(`{"id", "label", "x", "y", "yaw", "area"}` — id는 다른 마커와 겹치지 않게 501~). 마커를 고친 뒤에는 `world_list_markers`의 `area`를 확인하고 `nav-export`로 다시 내보냅니다. 

걷기 자동 확인은 `BP_TestPlayer`를 레벨에 놓고 인스턴스의 `AutoScript`(`0:move 1 0;24:move 0 0`)·`AutoPossessPlayer`를 채운 뒤 PIE(끝나면 지움). 

정점 수 - 1은 63의 배수. 지형 규칙: 걸을 곳 30° 이하, 막을 곳 60° 이상(D-172). 

지면(L2c, D-181): 정의의 `ground`(레이어 = 맵 단위, `kind` zone → pad·road·slope 순서로 덮음, `textures`의 `desat`·`tint`·`vertical`, `grass` = Grass Type별 메시·밀도)를 `world_build_landscape`가 가중치(`Saved/World/<이름>.w8`, 정점마다 합 255)로 계산해 C++ `CreateLandscapeLayered`로 넣고 레이어 정보 `LI_*`를 `<asset_dir>/Layers`에 만든다. 

그 밖의 단계는 `world_step(name, step, args_json)` 하나로 부른다(`jm_kit.world.run_step` — 단계를 추가해도 툴셋을 안 고치므로 재시작 불필요): `grass`(Landscape Grass Type `LGT_*`) → `material`(지형 재질 — Grass Type이 먼저 있어야 풀 출력이 연결됨) → `apply_material`, `weights`(가중치만 계산), `audit`(메시 LOD·삼각형·충돌, 텍스처 — 텍스처 크기는 스트리밍 전 값이라 믿지 말 것), `probe`(메시를 `PROBE_*`로 임시 배치 — 저장 전에 빈 목록으로 지움). `grass` 항목의 `patch`(`wave_m`·`threshold`·`sharp`)는 잡음 얼룩으로 무리 지어 나게 한다. 

자연물(D-182): `dress` = 정의의 `dress.items`(`tree`·`rock`·`ring_rock`은 블록아웃 항목 `from`을 메시로 교체, `scatter` 잔돌, `place` 메시 하나)를 `NA_*` 라벨·`Nature` 폴더에 놓고 바꾼 `BO_*`를 지움(다시 부르면 지우고 다시, `world_blockout`은 `dress`가 바꾼 항목을 건너뜀). **보이는 메시는 충돌을 끄고 충돌은 숨긴 도형 `NA_*_Col`이 맡는다** — 팩 바위는 단순 충돌이 없어 캐릭터가 통과하고, 나무는 서버 내보내기가 메시 삼각형 전체를 쓰기 때문. 나무 충돌은 줄기 원기둥(`trunk_r_m`), 바위 충돌은 놓인 메시의 크기·방향에 맞춘 타원 기둥(`col_fit` = 메시 경계 상자 가로 대비 폭, 높이는 보이는 바위의 60%·0.7~3m이되 **윗면이 기둥이 덮는 자리의 가장 높은 지면보다 1m 이상** — 낮으면 서버 내비메시(단차 50cm)와 UE(걸음 높이 45cm)가 걸어 넘는 곳으로 봄, D-187. 블록아웃 상자를 그대로 쓰면 바위와 어긋남). 바위는 길에서 비켜 놓고 나무는 길·바위·마커·`dress.avoid` 원을 피한다. `sets`의 `sink`는 묻는 깊이(메시 높이 비율 — 밑판이 드러나는 메시는 크게). **보이는 메시의 충돌은 프로필을 `NoCollision`으로 바꿔서 끈다** — `set_collision_enabled(NO_COLLISION)`만 하면 에디터에서는 꺼진 것으로 보여도 프로필(`BlockAll`)이 남아 PIE·맵 로드 때 다시 켜진다(2026-11-18 문틀 메시가 문을 막아 발견, FBX 가져오기는 메시 전체를 덮는 단순 충돌을 만든다). 

마을 소품(L2d, D-183): `jm_kit/props.py` — `world_step` `prop_import`(`{"fbx", "prop", "target_tris"}` → `/Game/World/Props/<이름>/SM_·M_·T_`, 공용 PBR 인스턴스, LOD 3단계, 다시 가져올 때 폴더를 비움)·`prop_info`. `dress` 항목(group `village`): `ring_prop`(블록아웃 ring을 따라 한 칸씩 — 목책, 긴 축을 접선에 맞춤), `gate_prop`(arch 자리의 문틀, 충돌은 기둥 둘 — `post_off`·`post_w`는 메시 비율. `closed: true`면 문 전체 폭을 막는 충돌 하나(`door_thick_m`) — L5 닫힌 문. `ring_prop`의 `close_gaps_m`은 그 폭 이하인 틈(문 자리) 양 끝에 칸을 하나씩 더 놓아 문 옆으로 바깥이 보이지 않게 함. L5 값은 `.claude/skills/varco-gear/scripts/l5_wall_gate.py`가 정의 파일에 씀), `prop`(`from` 블록아웃 box 자리 또는 `x`·`y`·`yaw`, 크기 `width_m`·`height_m`, 충돌 `col_fit` [가로X, 가로Y, 높이]·`col_shape`·`col_max_h`·`col_off`(상자 중심 이동)·`col_extra`(기둥 추가)·`collide` — 충돌 윗면은 지면 위 1m 이상(작은 소품은 보이는 높이의 90%, `col_max_h`를 주면 그 값. 낮으면 걸어 넘음, D-189)). VARCO 메시는 긴 변 1m로 정규화돼 오고 앞면 방향이 제각각이라 `probe`로 확인한 뒤 `yaw_off`를 정한다(문틀·가판대·깃발·차양·천막은 +Y가 앞, 수레 손잡이는 +X). 

`clear_dress`, `colliders`(충돌 도형 위치 조회), `near`(`{"x", "y", "r"}` 주변 액터의 충돌 프로필 — 무엇이 막는지 찾을 때), `pie_pawn`(PIE 중 플레이어 위치·지면 높이), `pie_chars`(PIE 중 모든 캐릭터 — 내 플레이어·다른 플레이어·몬스터 — 의 위치와 발 높이 − 지면 높이: 서버 높이대로 놓인 캐릭터가 묻히거나 뜨는지 볼 때), **`check_dress`(배치 뒤 반드시 — 길 침범·마커 겹침·평지 침범·나무와 바위 겹침·충돌 돌출 수, 0이어야 함)**, `show_colliders`(`{"show": true}`로 충돌 도형을 보이게 해 캡처로 메시와 비교 — `dress`를 다시 부르거나 false로 되돌림). 

조명·성능 측정(L2f): `lighting`(정의의 `lighting.actors`대로 조명 액터 값을 넣음 — 해·스카이라이트·안개·후처리 볼륨, `{"dump": true}`는 지금 값만 읽음, `{"pie": true, "actors": [...]}`는 PIE 월드에 임시로 넣어 같은 세션에서 비교. 이 프로젝트는 확장 휘도 범위라 노출 EV가 해 세기 8~10 기준 -1~0 부근 — 12~14를 넣으면 화면이 검게 나옴), `pie_exec`(`{"cmds": [...]}` — PIE 콘솔 명령, `csvprofile`·`stat`·`t.`·`r.`·`sg.`·`grass.`·`foliage.`만 받음, 결과에 화면 픽셀 크기), `pie_view`(`{"x", "y", "z"(m), "pitch", "yaw"}` — 플레이어 카메라를 떼어 측정 지점에 고정, `{"clear": true}`로 복귀. 게임 월드에 액터를 만드는 Python 함수가 없어 이 방식). 측정은 `Saved/World/perf.py <라벨> <x> <y> <z> <pitch> <yaw> [대기초] [측정초] [--top]`(시점 고정 → `csvprofile` 구간 → 평균·하위 1%·스레드별 구간, 로컬 전용 — 기준 D-190). 

서버 없는 걷기 자동 확인: `Saved/TestAuto.txt`에 `0.5:move 1 0;3.0:move 0 0`을 쓰고(`BP_TestPlayer.AutoScript`가 비어 있으면 이 파일을 읽음) `StartPIE`의 `startTransform`으로 시작 위치를 준 뒤 `pie_pawn`으로 읽는다 — 끝나면 파일을 지운다(`StartPIE`의 `warmupSeconds`가 30초쯤 넘으면 호출이 시간 초과로 돌아오지만 PIE는 시작돼 있음). 

함정(2026-11-18): ① Import 직후에는 `world_describe_landscape`의 레이어별 컴포넌트 수가 0이다(편집 레이어 합성이 몇 프레임 뒤) ② `MaterialEditingLibrary.delete_all_material_expressions`는 한 번에 절반만 지워 0이 될 때까지 반복한다 ③ 풀은 뷰포트 카메라 주변에 프레임마다 조금씩 생기므로 `bThrottleCPUWhenNotForeground`를 끄고(`UEClient/JM/CLAUDE.md` "알려진 함정"의 백그라운드 스로틀) `SetCameraTransform`으로 카메라를 옮긴 뒤 10초쯤 기다렸다가 찍는다 ④ `CaptureViewport`의 base64 결과가 대화에 들어오지 않게 JSON-RPC로 직접 불러 파일로 저장한다(스크립트 `Saved/World/shot.py` — 로컬 전용).

## 실내 던전 (DG4, `jm_kit.dungeon` — `world_step`의 `dg_*` 단계)

Landscape 없는 실내 레벨은 `blockout`·`dress` 대신 정의의 `dungeon` 구획을 쓴다(예: `world_continent01_dungeon.json`). 정의 파일은 손으로 쓰지 않고 `scripts/dg4_layout.py`(방·통로 사각형 `ROOMS`·`CORRIDORS`·문 `DOORS` → 벽 상자·바닥·Ruins 장식·불빛·마커)가 만든다 — 구도를 고치면 스크립트를 고쳐 다시 돌린다.

- `dg_surface`(`{"name", "base", "normal", "arm", "tile_cm", "tint", "brightness", "rough_mul"}`): 석재 텍스처(색·DirectX 노멀·ARM)를 `/Game/World/Dungeon/Materials`로 가져와 `MI_Dungeon_<이름>`을 만든다. 부모 `M_Dungeon_Surface`는 월드 좌표 세 방향 투영이라 상자를 어떤 크기로 늘려도 타일 크기가 같고, 겹친 상자 사이 이음매가 보이지 않는다. 값만 바꿀 때는 파일 경로 없이 다시 부른다. 텍스처는 CC0(Poly Haven)만 넣는다 — 출처는 `Docs/ExternalAssets.md`.
- `dg_build`(다시 부르면 `DG_*` 액터를 지우고 다시): `surfaces`(상자 — 바닥·벽, 충돌 켜짐, `mat`), `runs`(메시를 선 따라 줄지어, `stretch`면 긴 축을 늘여 빈틈·겹침 없이), `props`(메시 하나 + `col` 숨긴 충돌 상자, `z`·`pitch`·`roll`·`width_m`/`height_m`), `lights`(점광원, `fx`에 파티클 경로를 주면 같은 자리에 이미터). 보이는 메시는 전부 `NoCollision`이고 충돌은 상자만 — 서버 내비메시 입력도 상자뿐이다(던전 전체가 삼각형 약 5천).
- 순서: `dg4_layout.py` → (열린 레벨 확인) `dg_build` → `world_place_markers` → `world_step lighting` → 저장 → `nav-export`. 벽 두께 1m·천장 없음이면 벽 윗면은 `[Prune]`에서 섬으로 꺼진다.
- 함정: ① 석판(`JungleWall_02A`, 두께 1m)을 같은 두께의 벽 상자와 같은 자리에 놓으면 묻혀 보이지 않는다 → 0.2m 앞으로 ② 문설주·기둥·잔해처럼 길로 튀어나온 것은 `col`을 꼭 준다(윗면 1m 이상) ③ 몬스터 인식은 벽을 보지 않는다 — 스폰 자리에서 이웃 방·통로까지의 직선 거리가 `AggroRangeCm`보다 멀게 놓는다 ④ 원시 소켓 시험은 직선으로 걸을 수 없어 방·통로 가운데를 잇는 길을 따라 걷는다(`Server/PerfData/2026-11-21_DG4/dg4_test.py`의 `ROUTE` — 구도를 바꾸면 함께 고친다).
