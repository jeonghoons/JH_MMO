---
name: mixamo-lookup
description: Mixamo 동작·동작 팩을 검색하거나 미리보기로 비교할 때 사용. 검색 API는 사용자가 `!` curl로 실행하고, 미리보기 GIF는 프레임 시트로 펼쳐 비교한다.
---

# Mixamo 동작 조회

## 검색 (사용자가 실행)

Claude가 직접 실행하면 자동 권한 검사가 막는다(2026-11-09). 사용자에게 아래 명령을 `!`로 실행해 달라고 요청한다.

```
! curl -s -m 20 "https://www.mixamo.com/api/v1/products?page=1&limit=96&type=Motion%2CMotionPack&query=<검색어>" -H "X-Api-Key: mixamo2"
```

- 결과의 `description`이 실제 동작 구분 이름이다(`name`은 겹친다).
- 팩은 `motions` 목록을 포함한다.
- `thumbnail_animated`는 CloudFront GIF이고 Claude가 curl로 받을 수 있다.

## 미리보기 비교

GIF를 받아 `gif_sheet.py`로 8프레임 시트를 만든 뒤 Read로 본다(프레임당 100ms라 대략 길이도 알 수 있다).

```
python -I .claude/skills/mixamo-lookup/gif_sheet.py <입력.gif> <출력.png> [프레임 수=8]
```

받은 GIF와 시트는 scratchpad에 둔다. 다운로드 설정(In Place, 스킨 없음 등)은 `Docs/ExternalAssets.md`.
