# GLB를 6방향(앞·뒤·왼·오른·위·아래)으로 렌더해 한 장의 PNG로 저장한다.
# 의존성: numpy, Pillow 만 사용(소프트웨어 래스터라이저). 압축(Draco/meshopt) GLB는 지원하지 않는다.
# 사용: python -I render_glb_6view.py <입력.glb> <출력.png> [타일크기=512]
# 시점 라벨은 glTF 축 기준(+Y 위, +Z 앞)이다. VARCO 결과물은 보통 +Z가 정면이다.
import io
import json
import struct
import sys

import numpy as np
from PIL import Image, ImageDraw

CT = {5120: np.int8, 5121: np.uint8, 5122: np.int16, 5123: np.uint16, 5125: np.uint32, 5126: np.float32}
NC = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4, "MAT4": 16}


def load_glb(path):
    data = open(path, "rb").read()
    magic, _ver, _length = struct.unpack_from("<4sII", data, 0)
    assert magic == b"glTF", "GLB 파일이 아님"
    off = 12
    gltf, binbuf = None, None
    while off < len(data):
        clen, ctype = struct.unpack_from("<I4s", data, off)
        chunk = data[off + 8: off + 8 + clen]
        if ctype == b"JSON":
            gltf = json.loads(chunk)
        elif ctype == b"BIN\x00":
            binbuf = chunk
        off += 8 + clen
    return gltf, binbuf


def accessor(gltf, binbuf, idx):
    a = gltf["accessors"][idx]
    bv = gltf["bufferViews"][a["bufferView"]]
    dt = np.dtype(CT[a["componentType"]])
    n = NC[a["type"]]
    start = bv.get("byteOffset", 0) + a.get("byteOffset", 0)
    stride = bv.get("byteStride", 0) or dt.itemsize * n
    count = a["count"]
    if stride == dt.itemsize * n:
        arr = np.frombuffer(binbuf, dtype=dt, count=count * n, offset=start).reshape(count, n)
    else:
        raw = np.frombuffer(binbuf, dtype=np.uint8, count=stride * (count - 1) + dt.itemsize * n, offset=start)
        idxs = (np.arange(count)[:, None] * stride + np.arange(dt.itemsize * n)[None, :]).ravel()
        arr = raw[idxs].view(dt).reshape(count, n)
    if a.get("normalized") and dt != np.float32:
        arr = arr.astype(np.float32) / np.iinfo(dt).max
    return arr


def node_matrix(node):
    if "matrix" in node:
        return np.array(node["matrix"], dtype=np.float64).reshape(4, 4).T
    m = np.eye(4)
    if "scale" in node:
        m = np.diag(list(node["scale"]) + [1.0]) @ m
    if "rotation" in node:
        x, y, z, w = node["rotation"]
        r = np.array([
            [1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w), 0],
            [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w), 0],
            [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y), 0],
            [0, 0, 0, 1]])
        m = r @ m
    if "translation" in node:
        t = np.eye(4)
        t[:3, 3] = node["translation"]
        m = t @ m
    return m


def collect(gltf, binbuf):
    """모든 메시 프리미티브를 월드 좌표의 (정점, 삼각형, UV, 텍스처) 목록으로 모은다."""
    textures = {}

    def tex_for(mat_idx):
        if mat_idx is None:
            return None
        mat = gltf["materials"][mat_idx]
        t = mat.get("pbrMetallicRoughness", {}).get("baseColorTexture")
        if not t:
            return None
        src = gltf["textures"][t["index"]]["source"]
        if src not in textures:
            img = gltf["images"][src]
            bv = gltf["bufferViews"][img["bufferView"]]
            o = bv.get("byteOffset", 0)
            pil = Image.open(io.BytesIO(binbuf[o:o + bv["byteLength"]])).convert("RGB")
            pil.thumbnail((1024, 1024))
            textures[src] = np.asarray(pil, dtype=np.float32) / 255.0
        return textures[src]

    out = []

    def walk(ni, parent):
        node = gltf["nodes"][ni]
        m = parent @ node_matrix(node)
        if "mesh" in node:
            for prim in gltf["meshes"][node["mesh"]]["primitives"]:
                if prim.get("mode", 4) != 4:
                    continue
                pos = accessor(gltf, binbuf, prim["attributes"]["POSITION"]).astype(np.float64)
                pos = (np.c_[pos, np.ones(len(pos))] @ m.T)[:, :3]
                if "indices" in prim:
                    tri = accessor(gltf, binbuf, prim["indices"]).astype(np.int64).reshape(-1, 3)
                else:
                    tri = np.arange(len(pos)).reshape(-1, 3)
                uv = None
                if "TEXCOORD_0" in prim["attributes"]:
                    uv = accessor(gltf, binbuf, prim["attributes"]["TEXCOORD_0"]).astype(np.float64)
                out.append((pos, tri, uv, tex_for(prim.get("material"))))
        for c in node.get("children", []):
            walk(c, m)

    scene = gltf["scenes"][gltf.get("scene", 0)]
    for root in scene["nodes"]:
        walk(root, np.eye(4))
    return out


# (라벨, 화면 오른쪽 축, 화면 위쪽 축, 카메라가 바라보는 방향)
VIEWS = [
    ("FRONT (+Z)", (1, 0, 0), (0, 1, 0), (0, 0, -1)),
    ("BACK (-Z)", (-1, 0, 0), (0, 1, 0), (0, 0, 1)),
    ("LEFT (+X)", (0, 0, -1), (0, 1, 0), (-1, 0, 0)),
    ("RIGHT (-X)", (0, 0, 1), (0, 1, 0), (1, 0, 0)),
    ("TOP (+Y)", (1, 0, 0), (0, 0, -1), (0, -1, 0)),
    ("BOTTOM (-Y)", (1, 0, 0), (0, 0, 1), (0, 1, 0)),
]


def render(prims, right, up, fwd, size, center, scale):
    right, up, fwd = (np.array(v, dtype=np.float64) for v in (right, up, fwd))
    color = np.full((size, size, 3), 0.18, dtype=np.float32)
    zbuf = np.full((size, size), np.inf)
    light = -fwd + 0.4 * up + 0.3 * right
    light /= np.linalg.norm(light)
    for pos, tri, uv, tex in prims:
        p = pos - center
        sx = (p @ right) * scale + size / 2
        sy = size / 2 - (p @ up) * scale
        sz = p @ fwd
        a, b, c = pos[tri[:, 0]], pos[tri[:, 1]], pos[tri[:, 2]]
        nrm = np.cross(b - a, c - a)
        ln = np.linalg.norm(nrm, axis=1)
        ln[ln == 0] = 1
        nrm /= ln[:, None]
        shade = 0.35 + 0.65 * np.abs(nrm @ light)
        X, Y, Z = sx[tri], sy[tri], sz[tri]
        x0 = np.clip(np.floor(X.min(1)).astype(int), 0, size - 1)
        x1 = np.clip(np.ceil(X.max(1)).astype(int), 0, size - 1)
        y0 = np.clip(np.floor(Y.min(1)).astype(int), 0, size - 1)
        y1 = np.clip(np.ceil(Y.max(1)).astype(int), 0, size - 1)
        det = (Y[:, 1] - Y[:, 2]) * (X[:, 0] - X[:, 2]) + (X[:, 2] - X[:, 1]) * (Y[:, 0] - Y[:, 2])
        for i in np.nonzero(np.abs(det) > 1e-9)[0]:
            xs = np.arange(x0[i], x1[i] + 1) + 0.5
            ys = np.arange(y0[i], y1[i] + 1) + 0.5
            gx, gy = np.meshgrid(xs, ys)
            w0 = ((Y[i, 1] - Y[i, 2]) * (gx - X[i, 2]) + (X[i, 2] - X[i, 1]) * (gy - Y[i, 2])) / det[i]
            w1 = ((Y[i, 2] - Y[i, 0]) * (gx - X[i, 2]) + (X[i, 0] - X[i, 2]) * (gy - Y[i, 2])) / det[i]
            w2 = 1 - w0 - w1
            z = w0 * Z[i, 0] + w1 * Z[i, 1] + w2 * Z[i, 2]
            sub = zbuf[y0[i]:y1[i] + 1, x0[i]:x1[i] + 1]
            mask = (w0 >= 0) & (w1 >= 0) & (w2 >= 0) & (z < sub)
            if not mask.any():
                continue
            sub[mask] = z[mask]
            if tex is not None and uv is not None:
                t = uv[tri[i]]
                u = (w0 * t[0, 0] + w1 * t[1, 0] + w2 * t[2, 0])[mask] % 1.0
                v = (w0 * t[0, 1] + w1 * t[1, 1] + w2 * t[2, 1])[mask] % 1.0
                th, tw = tex.shape[:2]
                col = tex[(v * (th - 1)).astype(int), (u * (tw - 1)).astype(int)]
            else:
                col = np.array([0.7, 0.7, 0.7], dtype=np.float32)
            color[y0[i]:y1[i] + 1, x0[i]:x1[i] + 1][mask] = col * shade[i]
    return (np.clip(color, 0, 1) * 255).astype(np.uint8)


def main():
    src, dst = sys.argv[1], sys.argv[2]
    size = int(sys.argv[3]) if len(sys.argv) > 3 else 512
    gltf, binbuf = load_glb(src)
    used = gltf.get("extensionsUsed", [])
    if any("draco" in e.lower() or "meshopt" in e.lower() for e in used):
        sys.exit("압축된 GLB는 지원하지 않음: %s" % used)
    prims = collect(gltf, binbuf)
    allp = np.vstack([p for p, _, _, _ in prims])
    lo, hi = allp.min(0), allp.max(0)
    center = (lo + hi) / 2
    scale = size * 0.9 / (hi - lo).max()
    ntri = sum(len(t) for _, t, _, _ in prims)
    print("verts=%d tris=%d bbox_min=%s bbox_max=%s size=%s" % (len(allp), ntri, lo.round(3), hi.round(3), (hi - lo).round(3)))
    print("skins=%d animations=%d nodes=%d" % (len(gltf.get("skins", [])), len(gltf.get("animations", [])), len(gltf.get("nodes", []))))
    sheet = Image.new("RGB", (size * 3, size * 2))
    for i, (label, r, u, f) in enumerate(VIEWS):
        tile = Image.fromarray(render(prims, r, u, f, size, center, scale))
        ImageDraw.Draw(tile).text((8, 8), label, fill=(255, 255, 0))
        sheet.paste(tile, ((i % 3) * size, (i // 3) * size))
    sheet.save(dst)
    print("saved", dst)


if __name__ == "__main__":
    main()
