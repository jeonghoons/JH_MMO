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
    """입은 슬롯 목록 → 숨길 몸 부위 집합(키트 BP 기본값)."""
    rules = load()["hide"]
    return {p for s in slots for p in rules.get(s, [])}
