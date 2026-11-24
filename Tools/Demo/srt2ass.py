# A7.4 시연 영상 자막(D-311): Docs/demo/subs.srt → .ass(1920x1080 기준 스타일 3개). ffmpeg의 SRT 변환은 위치 태그를 일부만 받아
# 줄마다 자리를 정하려고 ASS로 바꾼다. 줄 앞 표시: {\an8} = 화면 위(게임 장면 — 아래에 HUD가 있음), {L} = 왼쪽 1280px 위 가운데
# (부하 장면 — 오른쪽 640px는 DummyClient 창), 표시 없음 = 화면 아래(카드). \N = 줄바꿈.
# 사용: python -I -X utf8 Tools/Demo/srt2ass.py Docs/demo/subs.srt <출력.ass>
import io
import re
import sys

src, dst = sys.argv[1], sys.argv[2]
text = io.open(src, encoding="utf-8-sig").read().replace("\r\n", "\n")

HEAD = """[Script Info]
ScriptType: v4.00+
PlayResX: 1920
PlayResY: 1080
WrapStyle: 0
ScaledBorderAndShadow: yes

[V4+ Styles]
Format: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, Bold, Italic, Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, MarginL, MarginR, MarginV, Encoding
Style: Bottom,Malgun Gothic,46,&H00FFFFFF,&H00FFFFFF,&H00000000,&H64000000,0,0,0,0,100,100,0,0,1,3,0,2,60,60,40,1
Style: Top,Malgun Gothic,46,&H00FFFFFF,&H00FFFFFF,&H00000000,&H64000000,0,0,0,0,100,100,0,0,1,3,0,8,60,60,36,1
Style: Load,Malgun Gothic,40,&H00FFFFFF,&H00FFFFFF,&H00000000,&H64000000,0,0,0,0,100,100,0,0,1,3,0,8,40,680,36,1

[Events]
Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text
"""


def ts(t):
    h, m, rest = t.split(":")
    s, ms = rest.split(",")
    cs = int(ms) // 10
    return f"{int(h)}:{m}:{s}.{cs:02d}"


out = [HEAD]
for block in re.split(r"\n\s*\n", text.strip()):
    lines = block.split("\n")
    if len(lines) < 3:
        continue
    start, end = [x.strip() for x in lines[1].split("-->")]
    body = "\\N".join(lines[2:])
    style = "Bottom"
    if body.startswith("{\\an8}"):
        style, body = "Top", body[len("{\\an8}"):]
    elif body.startswith("{L}"):
        style, body = "Load", body[len("{L}"):]
    out.append(f"Dialogue: 0,{ts(start)},{ts(end)},{style},,0,0,0,,{body}\n")
io.open(dst, "w", encoding="utf-8-sig").write("".join(out))
print("ass ->", dst, len(out) - 1, "lines")
