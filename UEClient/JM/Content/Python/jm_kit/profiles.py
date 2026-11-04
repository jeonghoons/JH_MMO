"""kit_profiles.json 읽기."""
import json
import os

PATH = os.path.join(os.path.dirname(__file__), "kit_profiles.json")


def load():
    with open(PATH, encoding="utf-8") as f:
        return json.load(f)


def body(key):
    """체형 key("M"/"F")의 리더·부위·기본 의상."""
    return load()["bodies"][key]


def hidden_parts(slots):
    """입은 슬롯 목록 → 통째로 숨길 몸 부위 집합(2026-11-05부터 기본은 없음 — 마스크 사용)."""
    rules = load().get("hide", {})
    return {p for s in slots for p in rules.get(s, [])}


def _item_by_mesh():
    path = os.path.join(os.path.dirname(__file__), "items.json")
    with open(path, encoding="utf-8") as f:
        items = {k: v for k, v in json.load(f).items() if not k.startswith("_")}
    out = {}
    for it in items.values():
        for b in ("M", "F"):
            out[it["out"].format(body=b)] = it
    return out


def mask_params(items):
    """입은 {슬롯: 메시} → 몸 마스크 파라미터. 슬롯 기본값(kit_profiles mask) 위에 아이템 mask 값을 덮어쓴다."""
    rules = load().get("mask", {})
    by_mesh = _item_by_mesh()
    params = {}
    for slot, mesh in items.items():
        p = dict(rules.get(slot, {}))
        p.update(by_mesh.get(mesh, {}).get("mask", {}))
        for k, v in p.items():
            params[k] = max(params.get(k, v), v) if k.startswith("Hide") else v
    return params
