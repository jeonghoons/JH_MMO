"""장비 카탈로그 페이지 생성(IT0, D-241 "한눈에 보기") — 에디터 없이 실행.

읽는 것: jm_kit/items.json(세트·부위), Common/DataTable/ItemTable.csv·DropTable.csv·ShopTable.csv(능력치·판매가·드랍·상점),
        <저장소>/SourceArt/Icons/T_Icon_<키>.png(KitTools.icon_shoot), Saved/MCShots/gallery_<세트>_<M|F>_<front|back>.png(eq_gallery_shots).
쓰는 것: UEClient/JM/Saved/Catalog/index.html + img/*.jpg → Artifact로 게시(files에 img 폴더).

실행: python -I -X utf8 .claude/skills/kit-gear/scripts/gear_catalog.py
"""
import csv
import html
import json
import os
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", "..", ".."))
UE = os.path.join(ROOT, "UEClient", "JM")
DT = os.path.join(ROOT, "Common", "DataTable")
OUT = os.path.join(UE, "Saved", "Catalog")
SHOTS = os.path.join(UE, "Saved", "MCShots")
ICONS = os.path.join(ROOT, "SourceArt", "Icons")   # icon.SRC_DIR(저장소 최상위 SourceArt)

SLOTS = [("head", "머리", None), ("top", "상의", None), ("bottom", "하의", None), ("gloves", "장갑", None), ("boots", "신발", None),
         ("weapon", "한손검", "hand_r"), ("twohand", "양손검", None), ("weapon", "활", "hand_l"), ("offhand", "방패", None)]
SET_NAMES = {"00": "00 견습(천) · 기본 지급", "01": "01 민병(누빔)", "02": "02 사냥꾼(가죽)", "03": "03 용병(브리간딘)",
             "04": "04 기사(판금)", "05": "05 성기사(장식 판금)", "Plate": "옛 판금 · 처리 대기"}
MONSTER = {"1001": "초원 하급", "1002": "숲 중급", "1003": "숲 보스", "1004": "던전 일반", "1005": "던전 우두머리"}


def _csv(name):
    with open(os.path.join(DT, name), encoding="utf-8-sig", newline="") as f:
        return list(csv.DictReader(f))


def _pkg(p):
    return p.split(".", 1)[0] if p else ""


def _png(src, dst, width):
    """아이콘(투명 배경 유지)."""
    from PIL import Image
    im = Image.open(src).convert("RGBA")
    im.thumbnail((width, width))
    im.save(dst, "PNG", optimize=True)


def _jpg(src, dst, width):
    from PIL import Image
    im = Image.open(src).convert("RGB")
    if im.width > width:
        im = im.resize((width, int(im.height * width / im.width)))
    im.save(dst, "JPEG", quality=84)


def build():
    items = {k: v for k, v in json.load(open(os.path.join(UE, "Content", "Python", "jm_kit", "items.json"), encoding="utf-8")).items()
             if not k.startswith("_")}
    by_mesh = {}
    for k, it in items.items():
        p = it["out"].format(body="M", body_dir="Male") if "out" in it else it["source"]
        by_mesh[p] = k
    rows = {r["Name"]: r for r in _csv("ItemTable.csv")}
    drops, shop = {}, {r["ItemId"]: r["BuyPrice"] for r in _csv("ShopTable.csv")}
    for r in _csv("DropTable.csv"):
        drops.setdefault(r["ItemId"], []).append((r["MonsterId"], int(r["DropRate"]) / 100))
    key_row = {}
    for iid, r in rows.items():
        k = by_mesh.get(_pkg(r["MeshM"]) or _pkg(r["StaticMesh"]))
        if k:
            key_row[k] = (iid, r)
    sets = {}
    for k in items:
        sets.setdefault(k.split("_", 1)[0], []).append(k)
    order = sorted(sets, key=lambda s: (not s.isdigit(), s))

    os.makedirs(os.path.join(OUT, "img"), exist_ok=True)
    missing = []

    def cell(k):
        it = items[k]
        iid, r = key_row.get(k, ("?", {}))
        icon = os.path.join(ICONS, f"T_Icon_{k}.png")
        img = ""
        if os.path.exists(icon):
            _png(icon, os.path.join(OUT, "img", f"icon_{k}.png"), 160)
            img = f'<img src="img/icon_{k}.png" alt="{html.escape(r.get("DisplayName", k))} 아이콘" width="80" height="80" loading="lazy">'
        else:
            missing.append(icon)
        stats = []
        if r.get("BonusAttackDamage", "0") not in ("", "0"):
            stats.append(f'공격 +{r["BonusAttackDamage"]}')
        if r.get("BonusMaxHp", "0") not in ("", "0"):
            stats.append(f'HP +{r["BonusMaxHp"]}')
        dr = ", ".join(f"{MONSTER.get(m, m)} {p:g}%" for m, p in drops.get(iid, [])) or "—"
        sp = shop.get(iid)
        return (f'<div class="item">{img}<div class="meta"><b>{html.escape(r.get("DisplayName", k))}</b>'
                f'<code>{iid} · {html.escape(k)}</code>'
                f'<span class="stat">{" · ".join(stats) or "능력치 없음"}</span>'
                f'<span class="sub">판매 {r.get("SellPrice", "—")}G{f" · 상점 {sp}G" if sp else ""}{" · 기본 지급" if r.get("Starter") == "1" else ""}</span>'
                f'<span class="sub">드랍: {html.escape(dr)}</span></div></div>')

    head = "".join(f'<th scope="col">{html.escape(SET_NAMES.get(s, s))}</th>' for s in order)
    body = ""
    for slot, name, sock in SLOTS:
        tds = ""
        for s in order:
            ks = [k for k in sets[s] if items[k]["slot"] == slot and (sock is None or items[k].get("attach", {}).get("socket", "").lower().endswith(sock[-2:]))]
            tds += f'<td>{"".join(cell(k) for k in ks) or "<span class=empty>없음</span>"}</td>'
        body += f'<tr><th scope="row">{name}</th>{tds}</tr>'

    gal = ""
    for s in order:
        figs = ""
        for b, bn in (("M", "남"), ("F", "여")):
            for side, sn in (("front", "앞"), ("back", "뒤")):
                src = os.path.join(SHOTS, f"gallery_{s}_{b}_{side}.png")
                if not os.path.exists(src):
                    missing.append(src)
                    continue
                _jpg(src, os.path.join(OUT, "img", f"gal_{s}_{b}_{side}.jpg"), 1400)
                figs += (f'<figure><img src="img/gal_{s}_{b}_{side}.jpg" alt="{s} 세트 {bn} {sn}모습" loading="lazy">'
                         f'<figcaption>{bn} · {sn} — 한손검 대기 · 달리기 · 스킬1 · 팔을 머리 위로(멈춤) · 활 당김 끝(멈춤)</figcaption></figure>')
        gal += f'<section class="set"><h3>{html.escape(SET_NAMES.get(s, s))}</h3><div class="shots">{figs}</div></section>'

    page = TEMPLATE.replace("{{HEAD}}", head).replace("{{BODY}}", body).replace("{{GALLERY}}", gal)
    with open(os.path.join(OUT, "index.html"), "w", encoding="utf-8") as f:
        f.write(page)
    return {"out": os.path.join(OUT, "index.html"), "sets": order, "missing": missing}


TEMPLATE = """<title>JH_MMO 장비 카탈로그</title>
<meta name="claude:viewport-fit-sides" content="cover">
<link rel="stylesheet" href="https://fonts.googleapis.com/css2?family=Noto+Sans+KR:wght@400;500;700&family=IBM+Plex+Mono:wght@400;500&display=swap">
<style>
/* 레이아웃: 위 요약 → 등급(가로) × 부위(세로) 표 → 세트별 진열 사진(앞·뒤) */
:root{--bg:#f4f3f0;--panel:#ffffff;--fg:#22211e;--muted:#6b6760;--line:#dcd8cf;--accent:#8a5a1c;--chip:#efe7da;
--font:"Noto Sans KR",system-ui,sans-serif;--mono:"IBM Plex Mono",ui-monospace,monospace}
@media (prefers-color-scheme:dark){:root:not([data-theme="light"]){--bg:#17161a;--panel:#211f24;--fg:#ece9e3;--muted:#a39e95;--line:#38343c;--accent:#d9a45e;--chip:#2e2a26;color-scheme:dark}}
:root[data-theme="dark"]{--bg:#17161a;--panel:#211f24;--fg:#ece9e3;--muted:#a39e95;--line:#38343c;--accent:#d9a45e;--chip:#2e2a26;color-scheme:dark}
body{background:var(--bg);color:var(--fg);font:14px/1.55 var(--font);padding-block:28px 48px;padding-left:max(16px,env(safe-area-inset-left,0px));padding-right:max(16px,env(safe-area-inset-right,0px))}
main{max-width:1500px;margin:0 auto;display:grid;gap:28px}
h1{font-size:26px;margin:0;text-wrap:balance}h2{font-size:18px;margin:0 0 10px}h3{font-size:15px;margin:0 0 10px;color:var(--accent)}
p{margin:0;max-width:72ch;color:var(--muted)}
.scroll{overflow-x:auto;border:1px solid var(--line);border-radius:8px;background:var(--panel)}
table{border-collapse:collapse;min-width:720px;width:100%}
th,td{border-bottom:1px solid var(--line);padding:10px 12px;vertical-align:top;text-align:left}
thead th{font-size:12px;letter-spacing:.03em;color:var(--muted);background:var(--chip);position:sticky;top:0}
tbody th{white-space:nowrap;font-weight:700;width:72px}
.item{display:flex;gap:10px;align-items:flex-start}
.item img{width:80px;height:80px;border-radius:6px;background:var(--chip);flex:none}
.meta{display:grid;gap:2px;min-width:0}
code{font:12px var(--mono);color:var(--muted)}
.stat{font-variant-numeric:tabular-nums;color:var(--accent);font-weight:500}
.sub{font-size:12px;color:var(--muted)}
.empty{color:var(--muted);font-size:12px}
.set{background:var(--panel);border:1px solid var(--line);border-radius:8px;padding:14px}
.shots{display:grid;grid-template-columns:repeat(auto-fit,minmax(min(100%,520px),1fr));gap:12px}
figure{margin:0;display:grid;gap:6px}figure img{width:100%;height:auto;border-radius:6px;background:var(--chip)}
figcaption{font-size:12px;color:var(--muted)}
</style>
<main>
<header style="display:grid;gap:8px">
<h1>장비 카탈로그</h1>
<p>지금 게임에 들어 있는 장비를 등급(가로)과 부위(세로)로 놓은 표입니다. 능력치·판매가·드랍은 <code>ItemTable.csv</code>·<code>DropTable.csv</code>·<code>ShopTable.csv</code>에서, 사진은 진열 레벨 <code>L_GearGallery</code>의 Simulate 화면에서 가져왔습니다. 새 등급(01~05)과 양손검은 만들어지는 대로 열이 늘어납니다.</p>
</header>
<section><h2>등급 × 부위</h2><div class="scroll"><table><thead><tr><th scope="col">부위</th>{{HEAD}}</tr></thead><tbody>{{BODY}}</tbody></table></div></section>
<section style="display:grid;gap:12px"><h2>입힌 모습 (진열 레벨)</h2>
<p>가중치 문제는 팔을 머리 위로 든 자세(한손검 스킬1, 0.875초)와 양팔을 어깨 높이로 든 활 당김 끝(1.0초)에서 잘 드러납니다. 이 둘은 그 프레임에 멈춰 세웠습니다. 에디터에서 직접 보려면 <code>/Game/Dev/Gallery/L_GearGallery</code>를 열고 Simulate를 누르세요.</p>
{{GALLERY}}</section>
</main>
"""

if __name__ == "__main__":
    r = build()
    print(json.dumps(r, ensure_ascii=False, indent=1))
    sys.exit(0)
