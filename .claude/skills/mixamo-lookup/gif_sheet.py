"""GIF 애니메이션을 고르게 뽑은 프레임으로 가로 시트(PNG)를 만듭니다.

사용법: python -I gif_sheet.py <입력.gif> <출력.png> [프레임 수=8]
각 칸 위에 프레임 번호와 시각(ms)을 적습니다.
"""

import sys

from PIL import Image, ImageDraw, ImageSequence


def main() -> int:
    sys.stdout.reconfigure(encoding="utf-8")
    if len(sys.argv) < 3:
        print(__doc__)
        return 1
    src, dst = sys.argv[1], sys.argv[2]
    count = int(sys.argv[3]) if len(sys.argv) > 3 else 8

    gif = Image.open(src)
    frames, times, elapsed = [], [], 0
    for frame in ImageSequence.Iterator(gif):
        frames.append(frame.convert("RGB"))
        times.append(elapsed)
        elapsed += frame.info.get("duration", 100)

    total = len(frames)
    picks = sorted({round(i * (total - 1) / max(count - 1, 1)) for i in range(count)})
    w, h = frames[0].size
    label_h = 16
    sheet = Image.new("RGB", (w * len(picks), h + label_h), "white")
    draw = ImageDraw.Draw(sheet)
    for col, idx in enumerate(picks):
        sheet.paste(frames[idx], (col * w, label_h))
        draw.text((col * w + 4, 2), f"#{idx} {times[idx]}ms", fill="black")
    sheet.save(dst)
    print(f"{total}프레임, 전체 약 {elapsed}ms → {len(picks)}칸 시트: {dst}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
