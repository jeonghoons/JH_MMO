"""장갑 무늬 칠하기 (MC2.2 천 장갑, 2026-11-05).

키트 손 장갑(garment.build_gloves_kit_hand, UV 재배치)은 VARCO 장갑과 모양이 달라 텍스처 베이크로 무늬가 옮겨지지 않았다
(VARCO 장갑 손가락이 붙어 있고 좁아서 광선이 엉뚱한 곳에 맞음). 그래서 장갑 UV 위에 영역별 패턴을 직접 래스터화한다.
영역(컨셉 SourceArt/Concept/Cloth_Gloves.png 기준)
  - 소매 끝(손목 위 CUFF_LO~CUFF_HI cm): 파란 자수 띠 + 양쪽 가죽 테두리 선
  - 손등 패널(너클 선 뒤 PANEL_S, 손등 쪽): 베이지 리넨 + 테두리 선
  - 검지·약지 등(손가락 등 쪽): 파란 자수 줄 + 테두리 선
  - 나머지: 갈색 가죽(리넨 결을 갈색으로 물들임 — 컨셉 가죽의 잎사귀 양각 무늬)
패턴 원본 = SourceArt/Patterns/<스타일>_Band.png, <스타일>_Linen.png (VARCO 장갑 텍스처에서 자른 것).
전부 에디터 Python 표준 라이브러리만 쓴다(PNG 읽기·쓰기 = zlib).
"""
import math
import os
import struct
import zlib

import unreal

from . import body_mask, garment, profiles

BW = unreal.GeometryScript_BoneWeights
Q = unreal.GeometryScript_MeshQueries
L = unreal.GeometryScript_List
EAL = unreal.EditorAssetLibrary

SIZE = 1024
PAT_DIR = os.path.normpath(os.path.join(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()), "..", "..", "SourceArt", "Patterns"))
OUT_DIR = os.path.join(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_saved_dir()), "GlovePaint")

STYLES = {
    "Cloth": {"band": "Cloth_Band.png", "panel": "Cloth_Linen.png", "leather_src": "Cloth_Linen.png",
              "leather": (104, 62, 40), "trim": (150, 100, 62), "stripe_fingers": ("index", "ring")},
    # 판금 건틀릿(MC2.3): 패턴은 컨셉 SourceArt/Concept/Plate_Gloves.png에서 자름(손등 판 → Steel, 소매 끝 → Band).
    # 손가락 다섯 개 등에 강철 판(stripe = Steel, 반폭 넓게), 가죽은 리넨 결을 진한 갈색으로.
    # orm = 영역별 (AO, 거칠기, 금속도) — 있으면 ORM 텍스처도 칠한다(M_Equip_PBR용)
    "Plate": {"band": "Plate_Band.png", "panel": "Plate_Steel.png", "stripe": "Plate_Steel.png", "leather_src": "Cloth_Linen.png",
              "leather": (66, 42, 28), "trim": (196, 150, 72), "stripe_fingers": ("thumb", "index", "middle", "ring", "pinky"),
              "stripe_half": 0.95,
              "orm": {"band": (1.0, 0.35, 1.0), "panel": (1.0, 0.35, 1.0), "stripe": (1.0, 0.35, 1.0),
                      "trim": (1.0, 0.3, 1.0), "leather": (1.0, 0.7, 0.0)}},
}
CUFF_LO, CUFF_HI = 2.0, 5.8      # 손목 위 자수 띠 범위(cm)
TRIM_CM = 0.35                   # 테두리 선 폭
PANEL_S = (-6.5, -0.8)           # 손등 패널: 너클 선(middle_01) 기준 손 방향 위치(cm)
BACK_PANEL, BACK_STRIPE = 0.30, 0.50   # 손등 쪽 판정(정점 노멀 · 손등 방향)
STRIPE_HALF_CM = 0.75            # 손가락 자수 줄 반폭
TILE_CM = 4.0                    # 리넨·가죽 패턴 한 장 크기
BAND_H_CM = CUFF_HI - CUFF_LO    # 자수 띠 높이 = 패턴 세로


# ---------------------------------------------------------------- PNG
def _read_png(path):
    """8비트 RGB/RGBA, 비인터레이스 PNG → (w, h, RGB bytes)."""
    with open(path, "rb") as f:
        data = f.read()
    pos, idat, w = 8, b"", 0
    while pos < len(data):
        n = struct.unpack(">I", data[pos:pos + 4])[0]
        tag, body = data[pos + 4:pos + 8], data[pos + 8:pos + 8 + n]
        if tag == b"IHDR":
            w, h, depth, ctype = struct.unpack(">IIBB", body[:10])
            if depth != 8 or ctype not in (2, 6):
                raise RuntimeError(f"지원하지 않는 PNG 형식: {path}")
            bpp = 3 if ctype == 2 else 4
        elif tag == b"IDAT":
            idat += body
        pos += 12 + n
    raw, stride = zlib.decompress(idat), w * bpp
    out, prev, i = bytearray(), bytearray(stride), 0
    for _y in range(h):
        ft, line = raw[i], bytearray(raw[i + 1:i + 1 + stride])
        i += 1 + stride
        for x in range(stride):
            a = line[x - bpp] if x >= bpp else 0
            b = prev[x]
            c = prev[x - bpp] if x >= bpp else 0
            if ft == 1:
                line[x] = (line[x] + a) & 255
            elif ft == 2:
                line[x] = (line[x] + b) & 255
            elif ft == 3:
                line[x] = (line[x] + (a + b) // 2) & 255
            elif ft == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[x] = (line[x] + (a if pa <= pb and pa <= pc else (b if pb <= pc else c))) & 255
        prev = line
        if bpp == 3:
            out += line
        else:
            for x in range(w):
                out += line[x * 4:x * 4 + 3]
    return w, h, bytes(out)


def _write_png(path, rgb):
    raw = bytearray()
    for y in range(SIZE):
        raw.append(0)
        raw += rgb[y * SIZE * 3:(y + 1) * SIZE * 3]

    def chunk(tag, data):
        return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)
    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", SIZE, SIZE, 8, 2, 0, 0, 0)) \
        + chunk(b"IDAT", zlib.compress(bytes(raw), 9)) + chunk(b"IEND", b"")
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as f:
        f.write(png)


class _Pat:
    def __init__(self, name):
        self.w, self.h, self.px = _read_png(os.path.join(PAT_DIR, name))
        n = self.w * self.h
        self.mean = sum(self.px[i * 3] + self.px[i * 3 + 1] + self.px[i * 3 + 2] for i in range(n)) / (3.0 * n)

    def at(self, u, v, wrap_v=True):
        x = int((u % 1.0) * self.w) % self.w
        y = int(((v % 1.0) if wrap_v else min(max(v, 0.0), 0.999)) * self.h) % self.h
        i = (y * self.w + x) * 3
        return self.px[i], self.px[i + 1], self.px[i + 2]


# ---------------------------------------------------------------- 정점 정보
def _vertex_info(dm, body):
    """정점별 dict: side, arm(bool), dist(손목에서 cm), s(너클 선 기준), back(노멀·손등), arc(팔 둘레 cm), finger, q(손가락 축 옆 cm), lat(cm)."""
    leader = profiles.body(body)["leader"]
    pos = {n: t.translation for n, _p, t in garment.skeleton_check.bones(leader)}
    dm, infos = BW.get_all_bones_info(dm)
    names = {i.index: str(i.name) for i in infos}
    dm, vids, _ = Q.get_all_vertex_i_ds(dm)
    verts = L.convert_index_list_to_array(vids)
    P = {v: garment._v(Q.get_vertex_position(dm, v)[0]) for v in verts}
    N = {v: [0.0, 0.0, 0.0] for v in verts}
    dm, tids, _ = Q.get_all_triangle_i_ds(dm)
    for t in L.convert_index_list_to_array(tids):
        idx, _ = Q.get_triangle_indices(dm, t)
        a, b, c = P[idx.x], P[idx.y], P[idx.z]
        n = garment._cross(garment._sub(b, a), garment._sub(c, a))   # 면적 가중
        for v in (idx.x, idx.y, idx.z):
            N[v][0] += n[0]; N[v][1] += n[1]; N[v][2] += n[2]
    frames = {}
    for side in ("l", "r"):
        hand = garment._v(pos[f"hand_{side}"])
        mid01 = garment._v(pos[f"middle_01_{side}"])
        fwd, _ = garment._norm(garment._sub(mid01, hand))
        lat, _ = garment._norm(garment._sub(garment._v(pos[f"pinky_01_{side}"]), garment._v(pos[f"index_01_{side}"])))
        pn, _ = garment._norm(garment._cross(fwd, lat))
        thumb = garment._sub(garment._v(pos[f"thumb_02_{side}"]), hand)
        # 손등 방향: 키트 뼈대에서는 엄지(thumb_02)가 손등 쪽 평면에 가깝게 놓인다 — 반대로 두면 패널이 손바닥에 칠해짐(2026-11-05 촬영 확인)
        back = pn if garment._dot(thumb, pn) > 0 else (-pn[0], -pn[1], -pn[2])
        arm_segs, arm_total = garment._chain(pos, body_mask.ARM[side])
        fingers = {f: garment._finger_chain(pos, f, side) for f in garment.FINGERS}
        frames[side] = (hand, mid01, fwd, lat, back, arm_segs, arm_total, fingers)
    info = {}
    for v in verts:
        dm, ws, _ = BW.get_vertex_bone_weights(dm, v)
        top = max(ws, key=lambda w: w.weight) if ws else None
        bone = names.get(top.bone_index, "") if top else ""
        side = "l" if bone.lower().endswith("_l") else "r"
        hand, mid01, fwd, lat, back, arm_segs, arm_total, fingers = frames[side]
        p = P[v]
        n, ln = garment._norm(tuple(N[v])) if any(N[v]) else ((0.0, 0.0, 1.0), 1.0)
        reg = body_mask._region_of(bone)
        dist = 0.0
        if reg == "arm":
            h = garment._on_chain(p, arm_segs, arm_total, extend=10.0)
            dist = max(0.0, arm_total - h[0]) if h else 0.0
        rel = garment._sub(p, hand)
        # 팔 둘레 위치(cm): 손목 단면에서 손등 방향을 0으로 한 각도 × 반지름 3cm
        u_ax = back
        v_ax = garment._cross(fwd, back)
        arc = math.atan2(garment._dot(rel, v_ax), garment._dot(rel, u_ax)) * 3.0
        d = {f: garment._chain_dist(p, fingers[f][0]) for f in garment.FINGERS}
        finger = min(d, key=d.get)
        segs = fingers[finger][0]
        a0, dir0 = segs[0][0], segs[0][1]
        q = garment._dot(garment._sub(p, a0), lat)
        info[v] = {"arm": reg == "arm", "dist": dist, "s": garment._dot(garment._sub(p, mid01), fwd),
                   "back": garment._dot(n, back), "arc": arc, "finger": finger, "q": q, "lat": garment._dot(rel, lat)}
    return dm, info


# ---------------------------------------------------------------- 색 규칙
def _mix(a, b, t):
    return (a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t, a[2] + (b[2] - a[2]) * t)


def _leather(st, pats, u, v):
    r, g, b = pats["leather"].at(u, v)
    k = ((r + g + b) / 3.0) / pats["leather"].mean
    c = st["leather"]
    return (c[0] * k, c[1] * k, c[2] * k)


def _color(st, pats, arm, dist, s, back, arc, finger, q, lat):
    """반환: (RGB, 영역 이름) — 영역은 ORM 칠하기에 쓴다(band/panel/stripe/trim/leather)."""
    trim = st["trim"]
    half = st.get("stripe_half", STRIPE_HALF_CM)
    if arm and dist >= CUFF_LO - TRIM_CM:
        if dist < CUFF_LO or dist > CUFF_HI:
            return trim, "trim"
        return pats["band"].at(arc / (BAND_H_CM * pats["band"].w / pats["band"].h), (dist - CUFF_LO) / BAND_H_CM, wrap_v=False), "band"
    if not arm:
        if finger in st["stripe_fingers"] and s > 0.8 and back > BACK_STRIPE:
            # 손가락 축 옆 위치 q의 기준: 손가락별 축 → q는 lateral 좌표 차이(정점 계산 시 손가락 시작점 기준)
            sp = pats["stripe"]
            if abs(q) <= half:
                return sp.at(s / (2 * half * sp.w / sp.h), 0.5 + q / (2 * half), wrap_v=False), "stripe"
            if abs(q) <= half + TRIM_CM:
                return trim, "trim"
        if PANEL_S[0] <= s <= PANEL_S[1] and back > BACK_PANEL:
            edge = min(s - PANEL_S[0], PANEL_S[1] - s, (back - BACK_PANEL) * 8.0)
            if edge < TRIM_CM:
                return trim, "trim"
            return pats["panel"].at(lat / TILE_CM, s / TILE_CM), "panel"
    return _leather(st, pats, (lat if not arm else arc) / TILE_CM, (s if not arm else dist) / TILE_CM), "leather"


# ---------------------------------------------------------------- 래스터화
def paint(dm, body, style, name):
    """장갑 dm(UV0 재배치 완료)에 style 무늬를 칠한 PNG를 Saved/GlovePaint/<name>.png로 쓴다.
    스타일에 orm이 있으면 ORM PNG(<name>의 BaseColor → ORM)도 쓰고 보고서 "orm_png"에 넣는다. 반환: (PNG 경로, 보고서)."""
    st = STYLES[style]
    pats = {"band": _Pat(st["band"]), "panel": _Pat(st["panel"]), "leather": _Pat(st["leather_src"]),
            "stripe": _Pat(st.get("stripe", st["band"]))}
    orm_tab = st.get("orm")
    dm, info = _vertex_info(dm, body)
    rgb = bytearray(SIZE * SIZE * 3)
    orm = bytearray(SIZE * SIZE * 3) if orm_tab else None
    filled = bytearray(SIZE * SIZE)
    dm, tids, _ = Q.get_all_triangle_i_ds(dm)
    count = {}
    for t in L.convert_index_list_to_array(tids):
        idx, _ = Q.get_triangle_indices(dm, t)
        uv = Q.get_triangle_u_vs(dm, 0, t)
        vs = (idx.x, idx.y, idx.z)
        I = [info[v] for v in vs]
        fs = [i["finger"] for i in I]
        finger = max(set(fs), key=fs.count)
        arm = sum(i["arm"] for i in I) >= 2
        arcs = [I[0]["arc"]]
        for i in I[1:]:   # 둘레 각도가 ±π에서 넘어가면 한쪽으로 맞춘다
            a = i["arc"]
            while a - arcs[0] > math.pi * 3.0:
                a -= 2 * math.pi * 3.0
            while arcs[0] - a > math.pi * 3.0:
                a += 2 * math.pi * 3.0
            arcs.append(a)
        attrs = [(i["dist"], i["s"], i["back"], arcs[k], i["q"], i["lat"]) for k, i in enumerate(I)]
        ax, ay = uv[0].x * SIZE, uv[0].y * SIZE
        bx, by = uv[1].x * SIZE, uv[1].y * SIZE
        cx, cy = uv[2].x * SIZE, uv[2].y * SIZE
        den = (by - cy) * (ax - cx) + (cx - bx) * (ay - cy)
        if abs(den) < 1e-12:
            continue
        x0, x1 = max(0, int(min(ax, bx, cx)) - 1), min(SIZE - 1, int(max(ax, bx, cx)) + 1)
        y0, y1 = max(0, int(min(ay, by, cy)) - 1), min(SIZE - 1, int(max(ay, by, cy)) + 1)
        for y in range(y0, y1 + 1):
            py = y + 0.5
            for x in range(x0, x1 + 1):
                px = x + 0.5
                w0 = ((by - cy) * (px - cx) + (cx - bx) * (py - cy)) / den
                w1 = ((cy - ay) * (px - cx) + (ax - cx) * (py - cy)) / den
                w2 = 1.0 - w0 - w1
                if w0 < -0.02 or w1 < -0.02 or w2 < -0.02:
                    continue
                i = y * SIZE + x
                if filled[i] == 2:
                    continue
                inside = w0 >= 0 and w1 >= 0 and w2 >= 0
                if filled[i] == 1 and not inside:
                    continue
                vals = [w0 * attrs[0][k] + w1 * attrs[1][k] + w2 * attrs[2][k] for k in range(6)]
                c, kind = _color(st, pats, arm, vals[0], vals[1], vals[2], vals[3], finger, vals[4], vals[5])
                o = i * 3
                rgb[o], rgb[o + 1], rgb[o + 2] = (int(min(255, max(0, ch))) for ch in c)
                if orm is not None:
                    orm[o], orm[o + 1], orm[o + 2] = (int(round(255 * ch)) for ch in orm_tab[kind])
                filled[i] = 2 if inside else 1
        count[finger] = count.get(finger, 0) + 1
    # 가장자리 번짐(밉맵·샘플링 이음새 방지)
    for _ in range(6):
        src, src_f = bytes(rgb), bytes(filled)
        src_o = bytes(orm) if orm is not None else None
        for y in range(SIZE):
            for x in range(SIZE):
                i = y * SIZE + x
                if src_f[i]:
                    continue
                for j in (i - 1 if x else -1, i + 1 if x < SIZE - 1 else -1, i - SIZE, i + SIZE):
                    if 0 <= j < SIZE * SIZE and src_f[j]:
                        rgb[i * 3:i * 3 + 3] = src[j * 3:j * 3 + 3]
                        if orm is not None:
                            orm[i * 3:i * 3 + 3] = src_o[j * 3:j * 3 + 3]
                        filled[i] = 1
                        break
    path = os.path.join(OUT_DIR, f"{name}.png")
    _write_png(path, rgb)
    rep = {"png": path, "tris_by_finger": count, "filled_pct": round(100.0 * sum(1 for f in filled if f) / (SIZE * SIZE), 1)}
    if orm is not None:
        orm_path = os.path.join(OUT_DIR, f"{name.replace('_BaseColor', '')}_ORM.png")
        _write_png(orm_path, orm)
        rep["orm_png"] = orm_path
    return path, rep


def import_texture(png, dest_dir, linear_mask=False):
    """PNG → 텍스처 에셋(같은 이름이면 덮어씀). 기본 sRGB 색 텍스처, linear_mask=True면 선형·마스크 압축(ORM). 반환: 텍스처."""
    t = unreal.AssetImportTask()
    t.filename = png
    t.destination_path = dest_dir
    t.automated = True
    t.save = True
    t.replace_existing = True
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([t])
    tex = unreal.load_asset(f"{dest_dir}/{os.path.splitext(os.path.basename(png))[0]}")
    if tex is None:
        raise RuntimeError(f"텍스처 임포트 실패: {png}")
    if linear_mask:
        tex.set_editor_property("srgb", False)
        tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_MASKS)
        EAL.save_loaded_asset(tex)
    return tex
