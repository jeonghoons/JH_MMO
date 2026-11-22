# UI2 하드웨어 커서 초안(D-270): 어두운 철 바탕 + 금 테두리 화살표 3종(기본·클릭 가능·대화), 32·48px.
# 4배로 그린 뒤 줄여서 가장자리를 부드럽게. 결과: UEClient/JM/Content/Slate/Cursors/Cursor_<종류>.png(32px)·Cursor_<종류>@1.5x.png(48px)
# — UE 하드웨어 커서 규칙(이름@배율x.png, DefaultEngine.ini HardwareCursors), 미리보기 sheet.png는 이 폴더에(git 제외 아님 — 승인 기록).
# 실행: python -I SourceArt/Cursors/draw_cursors.py (저장소 루트에서)
import os
import sys
from PIL import Image, ImageDraw, ImageFilter

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.normpath(os.path.join(HERE, "..", "..", "UEClient", "JM", "Content", "Slate", "Cursors"))
os.makedirs(OUT, exist_ok=True)
S = 4  # 초과 표본 배율
GOLD = (214, 172, 92, 255)
GOLD_HI = (250, 222, 150, 255)
IRON = (44, 40, 38, 255)
IRON_HI = (88, 80, 72, 255)
SHADOW = (0, 0, 0, 150)


def arrow_poly(u):
    # 32 기준 좌표(핫스팟 = 왼쪽 위 끝 (1,1)), u = 픽셀 배율
    pts = [(1, 1), (1, 22), (6.5, 17), (10.5, 26), (14, 24.5), (10, 16), (17, 16)]
    return [(x * u, y * u) for x, y in pts]


def draw_arrow(size, accent=None):
    u = size / 32 * S
    W = size * S
    img = Image.new("RGBA", (W, W), (0, 0, 0, 0))
    # 그림자
    sh = Image.new("RGBA", (W, W), (0, 0, 0, 0))
    ImageDraw.Draw(sh).polygon([(x + 1.2 * u, y + 1.6 * u) for x, y in arrow_poly(u)], fill=SHADOW)
    sh = sh.filter(ImageFilter.GaussianBlur(1.2 * u))
    img.alpha_composite(sh)
    d = ImageDraw.Draw(img)
    poly = arrow_poly(u)
    d.polygon(poly, fill=GOLD)                       # 금 테두리(바깥)
    # 안쪽 철판: 테두리 두께만큼 줄인 다각형
    inner = [(1 + 1.6, 1 + 3.2), (1 + 1.6, 22 - 3.4), (6.5 + 0.3, 17 - 2.2), (10.5 + 0.9, 26 - 3.0),
             (14 - 2.3, 24.5 - 1.3), (10 - 1.6, 16 - 1.9), (17 - 4.0, 16 - 1.7)]
    d.polygon([(x * u, y * u) for x, y in inner], fill=IRON)
    # 철판 위쪽 반사(밝은 띠)
    d.polygon([(x * u, y * u) for x, y in [(2.8, 4.6), (2.8, 13), (7.5, 9.2)]], fill=IRON_HI)
    # 금 테두리 하이라이트(왼쪽 위 모서리)
    d.line([(1 * u, 1 * u), (1 * u, 21 * u)], fill=GOLD_HI, width=max(1, int(0.7 * u)))
    d.line([(1 * u, 1 * u), (16 * u, 15.6 * u)], fill=GOLD_HI, width=max(1, int(0.7 * u)))
    if accent == "click":
        # 클릭 가능: 오른쪽 아래 금빛 마름모(보석)
        cx, cy, r = 22 * u, 24 * u, 4.6 * u
        d.polygon([(cx, cy - r), (cx + r, cy), (cx, cy + r), (cx - r, cy)], fill=GOLD, outline=(60, 40, 10, 255))
        r2 = r * 0.55
        d.polygon([(cx, cy - r2), (cx + r2, cy), (cx, cy + r2), (cx - r2, cy)], fill=(255, 236, 170, 255))
    elif accent == "talk":
        # 대화: 오른쪽 아래 말풍선(양피지 색) + 점 세 개
        x0, y0, x1, y1 = 15.5 * u, 17.5 * u, 30.5 * u, 27 * u
        d.rounded_rectangle([x0, y0, x1, y1], radius=3 * u, fill=(236, 220, 180, 255), outline=(90, 62, 25, 255), width=max(1, int(0.8 * u)))
        d.polygon([(18 * u, 26.5 * u), (17 * u, 30.5 * u), (21.5 * u, 26.5 * u)], fill=(236, 220, 180, 255), outline=(90, 62, 25, 255))
        for i in range(3):
            cx = (19.5 + i * 3.5) * u
            d.ellipse([cx - 1 * u, 22.2 * u - 1 * u, cx + 1 * u, 22.2 * u + 1 * u], fill=(90, 62, 25, 255))
    return img.resize((size, size), Image.LANCZOS)


def main():
    kinds = {"Default": None, "Hand": "click", "Talk": "talk"}
    out = {}
    for name, acc in kinds.items():
        for size in (32, 48):
            im = draw_arrow(size, acc)
            p = os.path.join(OUT, f"Cursor_{name}.png" if size == 32 else f"Cursor_{name}@1.5x.png")
            im.save(p)
            out[(name, size)] = im
    # 미리보기 시트: 위 = 어두운 배경, 아래 = 밝은 배경, 각 4배 확대(최근접) + 원래 크기
    cell = 220
    sheet = Image.new("RGBA", (cell * 3, cell * 2 + 40), (255, 255, 255, 255))
    d = ImageDraw.Draw(sheet)
    for row, bg in enumerate([(52, 58, 50, 255), (205, 196, 170, 255)]):
        for col, name in enumerate(kinds):
            x0, y0 = col * cell, row * cell
            d.rectangle([x0, y0, x0 + cell - 1, y0 + cell - 1], fill=bg)
            big = out[(name, 48)].resize((48 * 3, 48 * 3), Image.NEAREST)
            sheet.alpha_composite(big, (x0 + 10, y0 + 10))
            sheet.alpha_composite(out[(name, 32)], (x0 + 170, y0 + 20))
            sheet.alpha_composite(out[(name, 48)], (x0 + 162, y0 + 70))
    for col, label in enumerate(["Default", "Hand (click)", "Talk"]):
        d.text((col * cell + 10, cell * 2 + 12), label, fill=(0, 0, 0, 255))
    sheet.save(os.path.join(HERE, "sheet.png"))
    print("ok", OUT)


if __name__ == "__main__":
    main()
