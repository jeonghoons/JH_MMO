# VARCO GLB 형상 확인: 외부 패키지 없이(numpy·PIL) GLB를 읽어 세 축 정사영 실루엣(면 밝기 = 법선)을 한 장으로 그린다.
# 사용: python -I glb_views.py out.png a.glb [b.glb ...]   — 열 = 파일, 행 = 정면(X-Y)·옆면(Z-Y)·위(X-Z)
# 출력에 파일별 bbox(긴 변 기준 비율)도 찍는다. 축 이름은 GLB 원본 축(Y 위) 그대로 — 긴 축이 어느 쪽인지 그림으로 확인.
import json, struct, sys
import numpy as np
from PIL import Image, ImageDraw

CT = {5120: np.int8, 5121: np.uint8, 5122: np.int16, 5123: np.uint16, 5125: np.uint32, 5126: np.float32}
NC = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4}


def load(path):
    b = open(path, "rb").read()
    jl = struct.unpack_from("<I", b, 12)[0]
    j = json.loads(b[20:20 + jl])
    bin0 = 20 + jl + 8
    def acc(i):
        a = j["accessors"][i]; v = j["bufferViews"][a["bufferView"]]
        off = bin0 + v.get("byteOffset", 0) + a.get("byteOffset", 0)
        n = a["count"] * NC[a["type"]]
        return np.frombuffer(b, CT[a["componentType"]], n, off).reshape(a["count"], NC[a["type"]])
    P, F = [], []
    for m in j["meshes"]:
        for p in m["primitives"]:
            v = acc(p["attributes"]["POSITION"]).astype(np.float64)
            f = acc(p["indices"]).reshape(-1, 3).astype(np.int64) if "indices" in p else np.arange(len(v)).reshape(-1, 3)
            F.append(f + sum(len(x) for x in P)); P.append(v)
    return np.vstack(P), np.vstack(F)


def view(V, F, ax, size):
    a, b, d = ax
    P = V[:, [a, b]]; depth = V[:, d]
    lo, hi = V.min(0), V.max(0); span = (hi - lo).max()
    s = (size - 20) / span
    xy = np.c_[(P[:, 0] - lo[a]) * s + 10, size - 10 - (P[:, 1] - lo[b]) * s]
    tri = V[F]; n = np.cross(tri[:, 1] - tri[:, 0], tri[:, 2] - tri[:, 0])
    n /= np.linalg.norm(n, axis=1, keepdims=True) + 1e-12
    shade = (60 + 180 * np.abs(n[:, d])).astype(int)
    order = np.argsort(depth[F].mean(1))
    im = Image.new("RGB", (size, size), (235, 235, 235)); dr = ImageDraw.Draw(im)
    for k in order:
        c = int(shade[k]); dr.polygon([tuple(xy[i]) for i in F[k]], fill=(c, c, c))
    return im


def main():
    out, files = sys.argv[1], sys.argv[2:]
    size = 320
    sheet = Image.new("RGB", (size * len(files), size * 3), "white")
    for c, f in enumerate(files):
        V, F = load(f)
        ext = V.max(0) - V.min(0)
        print(f, "tri", len(F), "bbox(x,y,z)", np.round(ext / ext.max(), 3))
        for r, ax in enumerate([(0, 1, 2), (2, 1, 0), (0, 2, 1)]):
            sheet.paste(view(V, F, ax, size), (c * size, r * size))
    sheet.save(out)


if __name__ == "__main__":
    main()
