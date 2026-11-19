# (RC2: 2026-11-17_room_capacity/analyze.py에 P1a·P1b 지표 열을 더한 것)
# runs/<이름>/ 의 원자료를 읽어 실행별 정상 구간 수치를 표로 낸다. 사용: python -I -X utf8 analyze.py [이름 접두사]
# 정상 구간 = 접속(램프)이 끝나고 10초 뒤부터. 서버 지표는 players == 봇 수인 [metrics] 줄(처음 2줄 제외).
import csv, glob, os, re, statistics, sys

HERE = os.path.dirname(os.path.abspath(__file__))
prefix = sys.argv[1] if len(sys.argv) > 1 else ""

def med(v): return statistics.median(v) if v else float("nan")
def mean(v): return sum(v) / len(v) if v else float("nan")

def load(run):
    name = os.path.basename(run)
    args = open(os.path.join(run, "args.txt"), encoding="utf-8").read().split()
    start = int(args[0])
    bots = int(args[args.index("--bots") + 1]); dur = int(args[args.index("--duration") + 1])
    t0 = bots / 50.0 + 10.0
    r = {"name": name, "bots": bots}

    con = open(os.path.join(run, "loadtest_console.txt"), encoding="utf-8", errors="replace").read()
    m = re.search(r"attempts=(\d+) ok=(\d+) fail=(\d+) loginOk=(\d+) loginFail=(\d+) unexpectedClose=(\d+)", con)
    r["ok"] = int(m.group(2)) if m else -1
    r["fail"] = (int(m.group(3)) + int(m.group(5)) + int(m.group(6))) if m else -1
    m = re.search(r"ping\s+p50/p95/p99/max = ([\d.]+)/([\d.]+)/([\d.]+)/([\d.]+)", con)
    r["ping_all_p99"] = float(m.group(3)) if m else float("nan")

    rows = [x for x in csv.DictReader(open(os.path.join(run, "loadtest.csv"))) if float(x["t_sec"]) > t0 + 0.01]
    r["ping_p50"] = mean([float(x["ping_p50_ms"]) for x in rows])
    r["ping_p99_mean"] = mean([float(x["ping_p99_ms"]) for x in rows])
    r["ping_p99_max"] = max([float(x["ping_p99_ms"]) for x in rows], default=float("nan"))
    r["bot_recv_kb"] = max([float(x["max_bot_recv_KBps"]) for x in rows], default=float("nan"))
    r["bot_cpu"] = mean([float(x["bot_cpu_pct"]) for x in rows])
    r["corrected_ps"] = mean([float(x["move_corrected"]) for x in rows]) / 5.0

    lines = []
    for lg in glob.glob(os.path.join(run, "logs", "*.log")):
        for ln in open(lg, encoding="utf-8", errors="replace"):
            if "[metrics]" in ln and f" players={bots} " in ln:
                lines.append(ln)
    lines = lines[2:]
    def grab(pat, idx):
        out = []
        for ln in lines:
            m = re.search(pat, ln)
            if m: out.append(float(m.group(idx)))
        return out
    late = r"tickLate\(avg/p50/p95/p99/max ms\)=([\d.]+)/([\d.]+)/([\d.]+)/([\d.]+)/([\d.]+)"
    body = r"tickBody\(avg/p50/p95/p99/max ms\)=([\d.]+)/([\d.]+)/([\d.]+)/([\d.]+)/([\d.]+)"
    r["n_lines"] = len(lines)
    r["out_ps"] = mean(grab(r"out/s=(\d+)", 1)); r["in_ps"] = mean(grab(r"in/s=(\d+)", 1))
    r["late_p50"] = med(grab(late, 2)); r["late_p99_med"] = med(grab(late, 4)); r["late_p99_max"] = max(grab(late, 4), default=float("nan"))
    r["body_p99_med"] = med(grab(body, 4)); r["body_p99_max"] = max(grab(body, 4), default=float("nan")); r["body_avg"] = mean(grab(body, 1))
    r["view_avg"] = mean(grab(r"viewPlayers\(avg/p99/max\)=([\d.]+)/(\d+)/(\d+)", 1)); r["view_p99"] = med(grab(r"viewPlayers\(avg/p99/max\)=([\d.]+)/(\d+)/(\d+)", 2))
    r["sess_max"] = max(grab(r"sendQ\(total/sessionMax\)=(\d+)/(\d+)", 2), default=float("nan"))
    r["sendq_total"] = max(grab(r"sendQ\(total/sessionMax\)=(\d+)/(\d+)", 1), default=float("nan"))
    r["log_drop"] = max(grab(r"logDrop=(\d+)", 1), default=0)
    r["monsters"] = med(grab(r"monsters=(\d+)", 1))
    # P1a·P1b 지표: WSASend 호출 수·호출당 메시지, 잡 큐(방) 스레드가 일한 시간(작업 + 내보내기, 초당 ms — 10으로 나누면 %), 작업 큐 대기
    r["calls_ps"] = mean(grab(r"sendCalls/s=(\d+)", 1)); r["bufs"] = mean(grab(r"bufsPerSend=([\d.]+)", 1))
    busy = r"busy\(job/flush ms per s\)=(\d+)/(\d+)"
    bj, bf = grab(busy, 1), grab(busy, 2)
    r["busy_mean"] = mean([a + b for a, b in zip(bj, bf)]); r["busy_max"] = max([a + b for a, b in zip(bj, bf)], default=float("nan"))
    r["wait_p99"] = mean(grab(r"jobWait\(p50/p90/p99 ms\)=(\d+)/(\d+)/(\d+)", 3))
    r["rooms"] = max(grab(r"rooms=(\d+)", 1), default=float("nan"))

    cpu = [x for x in csv.DictReader(open(os.path.join(run, "server_cpu.csv"), encoding="utf-8-sig", errors="replace")) if x.get("epoch_ms", "").isdigit()]
    win = [x for x in cpu if (start + t0) * 1000 <= int(x["epoch_ms"]) <= (start + dur) * 1000]
    if len(win) >= 2:
        dt = (int(win[-1]["epoch_ms"]) - int(win[0]["epoch_ms"]))
        r["cpu_user"] = (int(win[-1]["user_ms"]) - int(win[0]["user_ms"])) / dt * 100
        r["cpu_kernel"] = (int(win[-1]["kernel_ms"]) - int(win[0]["kernel_ms"])) / dt * 100
    else:
        r["cpu_user"] = r["cpu_kernel"] = float("nan")
    # 합격선: 틱 본문·지연 p99 <= 20(구간 p99의 중앙값), 핑 p99 <= 50(구간 p99 평균), 봇 수신 <= 64KB/s, 실패·로그 유실 0
    r["pass"] = (r["body_p99_med"] <= 20 and r["late_p99_med"] <= 20 and r["ping_p99_mean"] <= 50 and r["bot_recv_kb"] <= 64
                 and r["fail"] == 0 and r["ok"] == bots and r["log_drop"] == 0)
    return r

# analyze.py --pass <이름> : 그 실행의 통과 여부만 O/X로 출력(run_matrix.sh의 건너뛰기 판단용)
if prefix == "--pass":
    try: print("O" if load(os.path.join(HERE, "runs", sys.argv[2]))["pass"] else "X")
    except Exception: print("X")
    sys.exit(0)

runs = sorted(glob.glob(os.path.join(HERE, "runs", prefix + "*")), key=os.path.getmtime)
hdr = ["실행", "봇", "통과", "핑 p99(구간 평균/최대, 전체)", "틱 지연 p50 / p99(중앙/최대)", "틱 본문 p99(중앙/최대)", "시야 인원(평균/p99)", "송신/s", "WSASend/s", "호출당 메시지", "방 스레드 ms/s(평균/최대)", "작업 대기 p99", "몬스터", "Room", "CPU 사용자+커널(코어1=100)", "세션 큐 최대", "봇 수신 KB/s", "봇 CPU", "보정/s", "실패"]
print("| " + " | ".join(hdr) + " |"); print("|" + "---|" * len(hdr))
for run in runs:
    if not os.path.exists(os.path.join(run, "args.txt")): continue
    try: r = load(run)
    except Exception as e:
        print(f"| {os.path.basename(run)} | 읽기 실패: {e} |"); continue
    print("| {name} | {bots} | {p} | {ping_p99_mean:.0f}/{ping_p99_max:.0f}, {ping_all_p99:.0f} | {late_p50:.0f} / {late_p99_med:.0f}/{late_p99_max:.0f} | {body_p99_med:.1f}/{body_p99_max:.1f} | {view_avg:.1f}/{view_p99:.0f} | {out_ps:.0f} | {calls_ps:.0f} | {bufs:.1f} | {busy_mean:.0f}/{busy_max:.0f} | {wait_p99:.0f} | {monsters:.0f} | {rooms:.0f} | {cpu_user:.0f}+{cpu_kernel:.0f} | {sess_max:.0f} | {bot_recv_kb:.1f} | {bot_cpu:.0f} | {corrected_ps:.1f} | {fail} |".format(p="O" if r["pass"] else "X", **r))
