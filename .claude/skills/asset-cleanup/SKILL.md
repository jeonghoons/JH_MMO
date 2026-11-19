---
name: asset-cleanup
description: 에셋 팩을 정리하거나 커밋 경로로 옮길 때 사용. 참조 검사와 이동(jm_kit.refs의 refs_find·refs_plan·refs_move·refs_resave), 텍스처 원본 줄이기(jm_kit.tex의 tex_reduce).
---

# 에셋 정리 (참조 검사·이동·텍스처 줄이기)

기본은 dry_run이다. 되돌릴 수 없는 작업이므로 먼저 백업한다.

- `jm_kit.refs` — 참조 검사(MCP `refs_find`): 플레이어 쪽 루트에서 의존을 따라가 Paragon·Greystone·Sparrow에 닿는 사슬과 클래스. 팩 정리(2026-11-12): `refs_plan(roots, targets, deleting)` = 루트에서 닿는 targets 안 에셋 전체(팩 내부 사슬 포함, `deleting`을 남길 에셋이 쓰면 `blockers`, 없는 패키지는 `missing`), `refs_move(roots, mapping_json, deleting, dry_run)` = 그 목록을 `[[원본 접두사, 대상 접두사]]`대로 `AssetTools.rename_assets` → 남은 리디렉터 Fix Up → 저장(기본 dry_run, `blockers`가 있으면 안 옮김). 폴더 삭제는 참조 0 확인 뒤 에디터를 닫고 디스크에서. 맵을 다시 저장하지 않고 옮길 때(L2f, D-192)는 고칠 맵을 에디터에 열어 둔 채 `refs_move(..., fixup=false)`(리디렉터를 남김 — 열려 있지 않은 맵은 리디렉터로 새 위치를 찾음) → `refs_resave(roots)`(roots 가운데 리디렉터를 가리키는 패키지만 다시 저장, 맵은 열려 있는 것만).
- `jm_kit.tex` — 텍스처 원본 줄이기(MCP `tex_reduce(roots, targets, max_size, only, dry_run)`, L2f D-192): `refs_plan`의 대상 가운데 텍스처의 uasset 안 원본 비트맵을 긴 변 `max_size`(기본 2048) 이하로 줄이고 저장(엔진 `TextureAssetActionsBlueprintFns.resize_texture_source` = 에디터 Resize Source). `MaxTextureSize`·`LODBias`는 파일 크기를 줄이지 못함. GitHub 파일당 100MiB 제한 때문에 팩 텍스처를 커밋 경로로 옮기기 전에 쓴다. 기본 dry_run, 되돌릴 수 없으므로 먼저 백업.
