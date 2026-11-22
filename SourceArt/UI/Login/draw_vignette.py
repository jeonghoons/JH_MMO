"""로그인 화면 비네트(UI7, D-275): 검정 + 알파(가장자리·아래쪽이 짙음). python -I draw_vignette.py"""
import math
from PIL import Image

W, H = 512, 288
im = Image.new("RGBA", (W, H))
px = im.load()
for y in range(H):
    for x in range(W):
        dx = (x / (W - 1) - 0.5) * 2.0
        dy = (y / (H - 1) - 0.5) * 2.0
        r = math.sqrt(dx * dx * 0.8 + dy * dy * 0.9)
        edge = max(0.0, min(1.0, (r - 0.55) / 0.75)) ** 1.6 * 0.85
        bottom = max(0.0, (y / (H - 1) - 0.55) / 0.45) ** 1.5 * 0.55
        a = min(1.0, edge + bottom)
        px[x, y] = (0, 0, 0, int(a * 255))
im.save("vignette.png")
print("ok")
