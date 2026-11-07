"""ItemTable.csv 외형 열 + BodyTable.csv 생성(MC4.1, DECISIONS 2026-11-07).

- ItemTable: 서버 열·기존 클라 열은 그대로 두고 외형 열만 다시 쓴다.
  AttachSocket·AttachM·AttachF = 머리·무기의 부착 본과 체형별 상대 변환(garment.static_attachment 계산 결과 — 투구 배율은 체형별 머리 상자 기준이라 런타임에 계산하지 않음),
  Mask = 몸 마스크 파라미터(kit_profiles.json 슬롯 기본값 + items.json 아이템 mask), HideHair = attach.hide_parts에 hair가 있으면 1.
  items.json 아이템 ↔ CSV 행은 메시 경로(MeshM = out{M}, StaticMesh = source)로 맞춘다. 맞는 아이템이 없는 행이 있으면 실패.
- BodyTable: kit_profiles.json bodies → Name 1(M)·2(F), Leader·Eyes·Teeth·Hair·BodyMaterial(MI_Body_<M|F>)·AnimClass,
  kit_profiles.json montages(hit·death, 공용 1벌) → HitMontage·DeathMontage(MC4.2, DECISIONS 2026-11-08). 비면 빈 칸.

예)
  import importlib, jm_kit.item_table as it; importlib.reload(it)
  it.export(write=False)   # 바뀔 값만 확인
  it.export()              # CSV 쓰기 → DataTableReimportTools.reimport_csv("/Game/Data/DT_ItemTable")
"""
import csv
import io
import os

import unreal

from . import body_mask, garment, profiles

CSV_DIR = os.path.normpath(os.path.join(unreal.Paths.project_dir(), "../../Common/DataTable"))
ITEM_COLS = ["AttachSocket", "AttachM", "AttachF", "Mask", "HideHair"]
BODY_COLS = ["Name", "Leader", "Eyes", "Teeth", "Hair", "BodyMaterial", "AnimClass", "HitMontage", "DeathMontage"]
BODY_IDS = {"M": 1, "F": 2}   # = Protocol PlayerType MALE/FEMALE
ANIM_CLASS = "/Game/Character/Mannequin/Anims/ABP_PlayerBase.ABP_PlayerBase_C"   # 플레이어 공용 AnimBP(DECISIONS 2026-11-07)


def _obj(path):
    """/Game/A/B → /Game/A/B.B (CSV 소프트 경로 형식). 빈 값은 그대로."""
    if not path:
        return ""
    return path if "." in path.rsplit("/", 1)[-1] else f"{path}.{path.rsplit('/', 1)[-1]}"


def _pkg(path):
    return path.split(".", 1)[0] if path else ""


def _mask_text(params):
    vals = {n: float(params.get(n, 8.0 if n.endswith("KeepCm") else 0.0)) for n in body_mask.PARAMS}
    return "(" + ",".join(f"{n}={v:.6f}" for n, v in vals.items()) + ")"


def _identity_text():
    return unreal.Transform().export_text()


def _read(path):
    with open(path, encoding="utf-8", newline="") as f:
        text = f.read()
    bom = text.startswith("﻿")
    rows = list(csv.reader(io.StringIO(text.lstrip("﻿"))))
    eol = "\r\n" if "\r\n" in text else "\n"
    return rows[0], rows[1:], bom, eol


def _write(path, header, rows, bom=False, eol="\n"):
    buf = io.StringIO()
    w = csv.writer(buf, lineterminator=eol)
    w.writerow(header)
    w.writerows(rows)
    with open(path, "w", encoding="utf-8-sig" if bom else "utf-8", newline="") as f:
        f.write(buf.getvalue())


def _item_by_mesh():
    """메시 패키지 경로 → items.json 키."""
    out = {}
    for key, it in garment.load_items().items():
        if "out" in it:
            out[it["out"].format(body="M")] = key
        else:
            out[it["source"]] = key
    return out


def item_values(key):
    """items.json 아이템 키 → 외형 열 값 dict."""
    it = garment.load_items()[key]
    if "attach" in it:
        tf = {b: garment.static_attachment(key, b) for b in ("M", "F")}
        return {"AttachSocket": it["attach"]["socket"],
                "AttachM": tf["M"]["transform"].export_text(),
                "AttachF": tf["F"]["transform"].export_text(),
                "Mask": _mask_text({}),
                "HideHair": "1" if "hair" in it["attach"].get("hide_parts", []) else "0"}
    p = dict(profiles.load().get("mask", {}).get(it["slot"], {}))
    p.update(it.get("mask", {}))
    return {"AttachSocket": "", "AttachM": _identity_text(), "AttachF": _identity_text(),
            "Mask": _mask_text(p), "HideHair": "0"}


def export_items(write=True):
    path = os.path.join(CSV_DIR, "ItemTable.csv")
    header, rows, bom, eol = _read(path)
    for c in ITEM_COLS:
        if c not in header:
            header.append(c)
    col = {c: i for i, c in enumerate(header)}
    by_mesh = _item_by_mesh()
    changed, missing = [], []
    for r in rows:
        r += [""] * (len(header) - len(r))
        mesh = _pkg(r[col["MeshM"]]) or _pkg(r[col["StaticMesh"]])
        key = by_mesh.get(mesh)
        if key is None:
            missing.append(r[col["Name"]])
            continue
        for c, v in item_values(key).items():
            if r[col[c]] != v:
                changed.append({"id": r[col["Name"]], "item": key, "col": c, "old": r[col[c]], "new": v})
                r[col[c]] = v
    if missing:
        raise RuntimeError(f"items.json에 맞는 아이템이 없는 행: {missing}")
    if write and changed:
        _write(path, header, rows, bom, eol)
    return {"csv": path, "rows": len(rows), "changed": changed, "written": bool(write and changed)}


def export_bodies(write=True):
    path = os.path.join(CSV_DIR, "BodyTable.csv")
    montages = profiles.load().get("montages", {})
    reaction = [montages.get("hit", ""), montages.get("death", "")]   # 공용 1벌 — 체형과 무관
    for v in reaction:
        if v and not unreal.EditorAssetLibrary.does_asset_exist(v):
            raise RuntimeError(f"몽타주 없음: {v}")
    rows = []
    for b, bid in BODY_IDS.items():
        bp = profiles.body(b)
        vals = [bp["leader"], bp["parts"]["eyes"], bp["parts"]["teeth"], bp["parts"]["hair"],
                f"{body_mask.MAT_DIR}/MI_Body_{b}"]
        for v in vals:
            if not unreal.EditorAssetLibrary.does_asset_exist(v):
                raise RuntimeError(f"에셋 없음({b}): {v}")
        rows.append([str(bid)] + [_obj(v) for v in vals] + [ANIM_CLASS] + [_obj(v) for v in reaction])
    old = None
    if os.path.exists(path):
        h, r, _bom, _eol = _read(path)
        old = [h] + r
    changed = old != [BODY_COLS] + rows
    if write and changed:
        _write(path, BODY_COLS, rows)
    return {"csv": path, "rows": rows, "changed": changed, "written": bool(write and changed)}


def export(write=True):
    """ItemTable 외형 열 + BodyTable을 생성한다. write=False면 바뀔 값만 돌려준다."""
    return {"items": export_items(write), "bodies": export_bodies(write)}
