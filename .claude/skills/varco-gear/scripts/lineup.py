# VARCO 컨셉 나란히 보기 + 화풍 수치 검사(D-279).
# 사용:
#   python -I lineup.py out.png [--cols N] [--h 320] [--stats] a.png [b.png ...]   — 격자 한 장으로 붙임(+ 수치)
#   python -I lineup.py - --stats a.png ...                                         — 붙이지 않고 수치만
#   - 각 칸 아래에 파일 이름을 찍는다. 배경은 중간 회색 — 흰 배경 컨셉과 어두운 컨셉을 같은 조건에서 본다.
#   - --stats: 배경을 뺀 물체 부분의 평균 채도·명도를 이미지마다, 그리고 묶음 평균·편차를 출력한다.
#       배경 제거: 알파가 있으면 알파(아이콘), 없으면 네 귀퉁이 평균색과 차이(RGB 합) 0.15 초과 + 크로마키 초록 제외.
#   - 통과 규칙(새 컨셉): 같은 묶음 승인 컨셉 평균과의 채도 차이 ±0.08 이내 + 눈으로 D-228 세 항목(색·톤 / 질감·화풍 / 시대감) 각 17/20 이상.
#     세계 기준 채도 0.36 = 마을·던전·00등급 컨셉 평균(2026-11-23 측정, Docs/DESIGN_ART_CONCEPT.md §7-2).
import argparse
import os
import numpy as np
from PIL import Image, ImageDraw

BG = (128, 128, 128)
LABEL_H = 22
WORLD_SAT = 0.36


def color_stats(path):
    """(채도 평균, 명도 평균, 채도 90백분위) — 256px로 줄여 물체 부분만."""
    im = Image.open(path)
    has_alpha = im.mode in ("RGBA", "LA") or "transparency" in im.info
    a = np.asarray(im.convert("RGBA").resize((256, 256)), dtype=np.float32) / 255
    c = a[..., :3]
    mx, mn = c.max(-1), c.min(-1)
    s = np.where(mx > 0, (mx - mn) / np.maximum(mx, 1e-6), 0)
    if has_alpha and (a[..., 3] < 0.5).mean() > 0.05:
        fg = a[..., 3] > 0.5
    else:
        k = 8
        corner = np.concatenate([c[:k, :k].reshape(-1, 3), c[:k, -k:].reshape(-1, 3),
                                 c[-k:, :k].reshape(-1, 3), c[-k:, -k:].reshape(-1, 3)]).mean(0)
        r, g, b = c[..., 0], c[..., 1], c[..., 2]
        green = (g > r * 1.25) & (g > b * 1.25) & (g > 0.3)
        fg = (np.abs(c - corner).sum(-1) > 0.15) & ~green
    if fg.sum() < 500:
        fg = np.ones_like(fg)
    return float(s[fg].mean()), float(mx[fg].mean()), float(np.percentile(s[fg], 90))


def print_stats(paths):
    rows = [(os.path.basename(p),) + color_stats(p) for p in paths]
    for name, s, v, s90 in rows:
        print(f"  {name:40s} 채도 {s:.2f} (p90 {s90:.2f})  명도 {v:.2f}")
    S = np.array([r[1] for r in rows]); V = np.array([r[2] for r in rows])
    print(f"  묶음 {len(rows)}장: 채도 평균 {S.mean():.2f} (편차 {S.std():.2f}, 세계 기준 {WORLD_SAT:.2f} 대비 {S.mean() - WORLD_SAT:+.2f})"
          f"  명도 평균 {V.mean():.2f} (편차 {V.std():.2f})")


def make_sheet(out, paths, cols, h):
    tiles = []
    for p in paths:
        im = Image.open(p).convert("RGBA")
        w = max(1, round(im.width * h / im.height))
        tiles.append((os.path.splitext(os.path.basename(p))[0], im.resize((w, h), Image.LANCZOS)))
    cols = cols or len(tiles)
    rows = [tiles[i:i + cols] for i in range(0, len(tiles), cols)]
    cell_w = max(t.width for _, t in tiles)
    sheet = Image.new("RGB", (cols * cell_w, len(rows) * (h + LABEL_H)), BG)
    d = ImageDraw.Draw(sheet)
    for r, row in enumerate(rows):
        for c, (name, t) in enumerate(row):
            y = r * (h + LABEL_H)
            sheet.paste(t, (c * cell_w + (cell_w - t.width) // 2, y), t)
            d.text((c * cell_w + 4, y + h + 4), name, fill=(255, 255, 255))
    sheet.save(out)
    print(f"{out}: {len(tiles)}장, {cols}열 x {len(rows)}행, {sheet.width}x{sheet.height}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("out", help="출력 PNG, '-'면 붙이지 않음")
    ap.add_argument("images", nargs="+")
    ap.add_argument("--cols", type=int, default=0, help="열 수(0 = 한 줄)")
    ap.add_argument("--h", type=int, default=320, help="칸 높이(px)")
    ap.add_argument("--stats", action="store_true", help="물체 부분 채도·명도 출력")
    a = ap.parse_args()
    if a.out != "-":
        make_sheet(a.out, a.images, a.cols, a.h)
    if a.stats:
        print_stats(a.images)


if __name__ == "__main__":
    main()
