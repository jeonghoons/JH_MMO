# UI3 창 틀·칸 텍스처 초안(D-270): 어두운 가죽/철 바탕 + 금 테두리(커서와 같은 금색).
# 결과(이 폴더 out/): T_UI_Window(9-slice, 128px·여백 24), T_UI_TitleBar, T_UI_Button, T_UI_Slot(테두리는 흰색 — UI가 등급 색을 곱함),
# mock.png(아이콘을 넣은 모의 창 — 승인용). 실행: python -I SourceArt/UI/draw_ui_frame.py (저장소 루트에서)
import os
from PIL import Image, ImageDraw, ImageFilter

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "out")
ICONS = os.path.normpath(os.path.join(HERE, "..", "Icons"))
os.makedirs(OUT, exist_ok=True)
GOLD = (214, 172, 92, 255)
GOLD_DK = (120, 88, 40, 255)
GOLD_HI = (250, 222, 150, 255)
BG = (30, 27, 25, 238)
BG2 = (42, 37, 33, 240)
GRADE = {"Common": (0x9D, 0x9D, 0x9D), "Uncommon": (0x1E, 0xFF, 0x00), "Rare": (0x00, 0x70, 0xDD), "Epic": (0xA3, 0x35, 0xEE)}


def noise_fill(img, box, c1, c2, seed=1):
    # 가죽 느낌의 아주 약한 얼룩(세로 그라데이션 + 잡음)
    import random
    rnd = random.Random(seed)
    x0, y0, x1, y1 = box
    px = img.load()
    for y in range(y0, y1):
        t = (y - y0) / max(1, (y1 - y0 - 1))
        base = [int(c1[i] * (1 - t) + c2[i] * t) for i in range(4)]
        for x in range(x0, x1):
            n = rnd.randint(-5, 5)
            px[x, y] = (max(0, base[0] + n), max(0, base[1] + n), max(0, base[2] + n), base[3])


def window(size=128, m=24):
    img = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    noise_fill(img, (3, 3, size - 3, size - 3), BG2, BG)
    d = ImageDraw.Draw(img)
    d.rectangle([1, 1, size - 2, size - 2], outline=GOLD_DK, width=2)
    d.rectangle([3, 3, size - 4, size - 4], outline=GOLD, width=1)
    d.line([(4, 4), (size - 5, 4)], fill=GOLD_HI, width=1)
    # 네 모서리 금 장식(여백 m 안에만 — 늘어나지 않음)
    for cx, cy, sx, sy in ((0, 0, 1, 1), (size - 1, 0, -1, 1), (0, size - 1, 1, -1), (size - 1, size - 1, -1, -1)):
        pts = [(cx + sx * 2, cy + sy * 2), (cx + sx * 16, cy + sy * 2), (cx + sx * 2, cy + sy * 16)]
        d.polygon(pts, fill=GOLD)
        d.polygon([(cx + sx * 4, cy + sy * 4), (cx + sx * 11, cy + sy * 4), (cx + sx * 4, cy + sy * 11)], fill=GOLD_HI)
    return img


def title_bar(w=256, h=40):
    img = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    noise_fill(img, (0, 0, w, h), (58, 48, 38, 245), (40, 34, 28, 245), seed=3)
    d = ImageDraw.Draw(img)
    d.line([(0, h - 2), (w, h - 2)], fill=GOLD, width=2)
    d.line([(0, h - 4), (w, h - 4)], fill=GOLD_DK, width=1)
    return img


def button(w=96, h=32, hover=False):
    img = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    noise_fill(img, (2, 2, w - 2, h - 2), (78, 64, 44, 255) if hover else (62, 52, 40, 255), (44, 37, 30, 255), seed=5)
    d = ImageDraw.Draw(img)
    d.rounded_rectangle([0, 0, w - 1, h - 1], radius=4, outline=GOLD, width=2)
    d.line([(4, 3), (w - 5, 3)], fill=GOLD_HI, width=1)
    return img


def slot(size=64):
    img = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    d.rectangle([0, 0, size - 1, size - 1], fill=(18, 16, 15, 230))
    # 테두리는 흰색 계열 — 위젯이 등급 색을 곱한다
    d.rectangle([1, 1, size - 2, size - 2], outline=(255, 255, 255, 255), width=2)
    d.rectangle([4, 4, size - 5, size - 5], outline=(255, 255, 255, 70), width=1)
    return img


def tint(img, rgb):
    r, g, b, a = img.split()
    px = img.load()
    out = Image.new("RGBA", img.size)
    po = out.load()
    for y in range(img.size[1]):
        for x in range(img.size[0]):
            pr, pg, pb, pa = px[x, y]
            if pr > 200 and pg > 200 and pb > 200:   # 흰 테두리만 등급 색
                po[x, y] = (rgb[0], rgb[1], rgb[2], pa)
            else:
                po[x, y] = (pr, pg, pb, pa)
    return out


def nine(img, w, h, m=24):
    # 9-slice 늘이기(미리보기용)
    W, H = img.size
    out = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    xs = [(0, m, 0, m), (m, W - m, m, w - m), (W - m, W, w - m, w)]
    ys = [(0, m, 0, m), (m, H - m, m, h - m), (H - m, H, h - m, h)]
    for sx0, sx1, dx0, dx1 in xs:
        for sy0, sy1, dy0, dy1 in ys:
            part = img.crop((sx0, sy0, sx1, sy1)).resize((max(1, dx1 - dx0), max(1, dy1 - dy0)), Image.NEAREST)
            out.alpha_composite(part, (dx0, dy0))
    return out


def main():
    win, tb, bt, sl = window(), title_bar(), button(), slot()
    win.save(os.path.join(OUT, "T_UI_Window.png"))
    tb.save(os.path.join(OUT, "T_UI_TitleBar.png"))
    bt.save(os.path.join(OUT, "T_UI_Button.png"))
    button(hover=True).save(os.path.join(OUT, "T_UI_Button_Hover.png"))
    sl.save(os.path.join(OUT, "T_UI_Slot.png"))
    # 모의 창
    mock = Image.new("RGBA", (900, 560), (96, 110, 84, 255))
    panel = nine(win, 520, 500)
    mock.alpha_composite(panel, (30, 30))
    mock.alpha_composite(tb.resize((500, 40)), (40, 40))
    d = ImageDraw.Draw(mock)
    d.text((58, 52), "Inventory (title = Noto Serif KR)", fill=(240, 220, 170, 255))
    xb = button(28, 26)
    mock.alpha_composite(xb, (510, 46))
    d.text((520, 51), "X", fill=(240, 220, 170, 255))
    icons = ["T_Icon_00_Top.png", "T_Icon_01_OneHandSword.png", "T_Icon_02_Head.png", "T_Icon_03_Bow.png",
             "T_Icon_00_Boots.png", "T_Icon_01_Shield.png", "T_Icon_02_TwoHandSword.png", "T_Icon_03_Top.png"]
    grades = ["Common", "Uncommon", "Rare", "Epic"] * 2
    for i, (ic, gr) in enumerate(zip(icons, grades)):
        x, y = 60 + (i % 4) * 76, 110 + (i // 4) * 76
        mock.alpha_composite(tint(sl, GRADE[gr]), (x, y))
        p = os.path.join(ICONS, ic)
        if os.path.isfile(p):
            mock.alpha_composite(Image.open(p).convert("RGBA").resize((54, 54), Image.LANCZOS), (x + 5, y + 5))
    for i, gr in enumerate(["Common", "Uncommon", "Rare", "Epic"]):
        d.text((60 + i * 76, 270), gr, fill=GRADE[gr] + (255,))
    mock.alpha_composite(bt, (60, 470))
    d.text((90, 478), "Button", fill=(240, 220, 170, 255))
    mock.alpha_composite(button(hover=True), (170, 470))
    d.text((195, 478), "Hover", fill=(240, 220, 170, 255))
    # 경매 줄 예시: 아이콘 + 등급 테두리 + 글자
    for i, (ic, gr) in enumerate([("T_Icon_02_OneHandSword.png", "Rare"), ("T_Icon_03_Head.png", "Epic")]):
        y = 310 + i * 60
        d.rectangle([60, y, 520, y + 52], fill=(40, 35, 31, 255), outline=GOLD_DK)
        mock.alpha_composite(tint(sl, GRADE[gr]).resize((48, 48)), (64, y + 2))
        p = os.path.join(ICONS, ic)
        if os.path.isfile(p):
            mock.alpha_composite(Image.open(p).convert("RGBA").resize((40, 40), Image.LANCZOS), (68, y + 6))
        d.text((124, y + 18), f"auction row  ({gr})   x1   12,000 G", fill=(230, 225, 210, 255))
    # 오른쪽: 원본 텍스처 확대
    mock.alpha_composite(win.resize((256, 256), Image.NEAREST), (600, 30))
    d.text((600, 292), "T_UI_Window 128px (9-slice margin 24)", fill=(0, 0, 0, 255))
    mock.alpha_composite(sl.resize((128, 128), Image.NEAREST), (600, 320))
    d.text((600, 452), "T_UI_Slot 64px (white = grade tint)", fill=(0, 0, 0, 255))
    mock.save(os.path.join(HERE, "mock.png"))
    print("ok", OUT)


if __name__ == "__main__":
    main()
