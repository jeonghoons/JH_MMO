# A7.4 시연 영상 카드(D-311): 제목·기술·그래프·끝 카드를 1920×1080 PNG로 만든다. 그래프 카드는 Docs/benchmark/ 그림을 다시 쓴다.
# 사용(저장소 루트): python -I -X utf8 Tools/Demo/make_cards.py   → Docs/demo/cards/*.png
import os

import matplotlib

matplotlib.use("Agg")
import matplotlib.image as mpimg
import matplotlib.pyplot as plt
from matplotlib.patches import FancyArrowPatch, FancyBboxPatch

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
OUT = os.path.join(ROOT, "Docs", "demo", "cards")
BENCH = os.path.join(ROOT, "Docs", "demo", "fig")   # Tools/Benchmark/make_figures.py --demo(내부 항목 ID·날짜 없는 시연용 그림)

plt.rcParams["font.family"] = "Malgun Gothic"
plt.rcParams["axes.unicode_minus"] = False

BG = "#12161c"
FG = "#e8ecf1"
MUTED = "#9aa5b1"
ACCENT = "#4fa3ff"
GOOD = "#5cc98a"
BOX = "#1e2530"
EDGE = "#3a4656"


def canvas():
    fig = plt.figure(figsize=(19.2, 10.8), dpi=100)
    fig.patch.set_facecolor(BG)
    ax = fig.add_axes([0, 0, 1, 1])
    ax.set_xlim(0, 1920)
    ax.set_ylim(1080, 0)
    ax.axis("off")
    ax.set_facecolor(BG)
    return fig, ax


def save(fig, name):
    os.makedirs(OUT, exist_ok=True)
    fig.savefig(os.path.join(OUT, name), facecolor=BG, dpi=100)
    plt.close(fig)


def box(ax, x, y, w, h, title, lines=(), color=EDGE, title_color=FG):
    ax.add_patch(FancyBboxPatch((x, y), w, h, boxstyle="round,pad=0,rounding_size=14",
                                facecolor=BOX, edgecolor=color, linewidth=2.5))
    ax.text(x + w / 2, y + 42, title, ha="center", va="center", fontsize=25, color=title_color, weight="bold")
    for i, ln in enumerate(lines):
        ax.text(x + w / 2, y + 92 + i * 38, ln, ha="center", va="center", fontsize=18, color=MUTED)


def arrow(ax, x1, y1, x2, y2, label="", color=ACCENT, dy=-18):
    ax.add_patch(FancyArrowPatch((x1, y1), (x2, y2), arrowstyle="-|>", mutation_scale=26,
                                 color=color, linewidth=3))
    if label:
        ax.text((x1 + x2) / 2, (y1 + y2) / 2 + dy, label, ha="center", va="center", fontsize=17, color=color)


def title_card():
    fig, ax = canvas()
    ax.text(960, 400, "JH_MMO", ha="center", va="center", fontsize=96, color=FG, weight="bold")
    ax.text(960, 530, "IOCP C++ 게임 서버 + Unreal Engine 5 클라이언트 MMORPG", ha="center", va="center",
            fontsize=34, color=FG)
    ax.text(960, 610, "사냥 · 드랍 · 장비 · 거래 · 경매 · 파티 던전 · 동접 5,000명", ha="center", va="center",
            fontsize=26, color=MUTED)
    ax.text(960, 900, "개인 포트폴리오 프로젝트 · 화면은 개발 중인 에디터 실행(PIE)", ha="center", va="center",
            fontsize=18, color=MUTED)
    save(fig, "00_title.png")


def tech_card():
    fig, ax = canvas()
    ax.text(960, 70, "서버 구조", ha="center", va="center", fontsize=40, color=FG, weight="bold")

    # ① 서버 권위 흐름(위 줄)
    ax.text(80, 150, "① 서버 권위 흐름", ha="left", va="center", fontsize=24, color=ACCENT, weight="bold")
    box(ax, 80, 190, 400, 190, "UE 클라이언트", ["입력 → 요청 패킷(CS_)", "결과는 받은 대로 반영"])
    box(ax, 760, 190, 400, 190, "게임 서버", ["거리·쿨타임·소유권 검증", "피해·드랍·아이템 계산"], color=ACCENT)
    box(ax, 1440, 190, 400, 190, "시야 안 플레이어", ["AOI(쿼드트리) 안에만", "SC_ 결과 방송"])
    arrow(ax, 480, 285, 760, 285, "CS_ 요청 (Protobuf)")
    arrow(ax, 1160, 285, 1440, 285, "SC_ 결과")

    # ② 서버 구조(아래 줄)
    ax.text(80, 470, "② 스레드 구조", ha="left", va="center", fontsize=24, color=ACCENT, weight="bold")
    box(ax, 80, 510, 400, 230, "IOCP 워커 6", ["소켓 수신·송신 완료 처리", "세션별 송신 묶기(10ms)"])
    box(ax, 760, 510, 400, 230, "Room 잡 큐", ["마을 · 사냥터 · 던전 인스턴스", "Room마다 한 번에 한 잡", "이동은 33ms 묶음 송신"],
        color=ACCENT)
    box(ax, 1440, 510, 400, 230, "DB 작업 스레드", ["SQL Server Express (ODBC)", "아이템·골드는 트랜잭션", "성공한 뒤 메모리 반영"])
    arrow(ax, 480, 625, 760, 625, "패킷 → 잡")
    arrow(ax, 1160, 600, 1440, 600, "저장 요청")
    arrow(ax, 1440, 660, 1160, 660, "완료 → 잡", color=GOOD, dy=22)

    ax.text(960, 810, "C++ · IOCP · Protobuf · SQL Server · Unreal Engine 5.8", ha="center", va="center",
            fontsize=24, color=MUTED)
    ax.text(960, 865, "클라이언트는 계산하지 않고, 서버가 보낸 위치·스탯·전투 결과·장비 상태를 그대로 보여 줍니다",
            ha="center", va="center", fontsize=20, color=MUTED)
    save(fig, "09_tech.png")


def graph_card(src, name, heading):
    fig, ax = canvas()
    # 아래 약 130px은 자막 자리로 비워 둔다(burn.ps1 자막이 화면 아래에 들어감)
    ax.text(960, 60, heading, ha="center", va="center", fontsize=38, color=FG, weight="bold")
    img = mpimg.imread(os.path.join(BENCH, src))
    h, w = img.shape[:2]
    box_w, box_h = 1640, 830
    scale = min(box_w / w, box_h / h)
    dw, dh = w * scale, h * scale
    x0, y0 = 960 - dw / 2, 115 + (box_h - dh) / 2
    ax.imshow(img, extent=(x0, x0 + dw, y0 + dh, y0), zorder=1)
    save(fig, name)


def end_card():
    fig, ax = canvas()
    ax.text(960, 470, "감사합니다", ha="center", va="center", fontsize=72, color=FG, weight="bold")
    save(fig, "12_end.png")


if __name__ == "__main__":
    title_card()
    tech_card()
    graph_card("4_5000_timeline.png", "11a_move_batch.png", "동접 5,000명: 이동 패킷 묶기 전과 후")
    graph_card("5_p1b_hotzone100.png", "11b_send_batch.png", "핫존 100명 테스트: 송신 묶기 전과 후")
    end_card()
    print("cards ->", OUT)
