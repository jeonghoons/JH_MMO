# runs/<이름>/ 의 원자료를 읽어 실행별 정상 구간 수치를 표로 낸다. 사용: python -I -X utf8 analyze.py [이름 접두사]
# 정상 구간 = 접속(램프)이 끝나고 10초 뒤부터. 서버 지표는 server_console.txt의 players == 봇 수인 [metrics] 줄(처음 2줄 제외).
# (2026-11-17 room_capacity/analyze.py에서 가져와 송신 지표 sendCalls/s·bufsPerSend를 더함 — P1a)
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
    ramp = int(args[args.index("--ramp-rate") + 1])
    t0 = bots / float(ramp) + 10.0
    r = {"name": name, "bots": bots}

    con = open(os.path.join(run, "loadtest_console.txt"), encoding="utf-8", errors="replace").read()
    m = re.search(r"attempts=(\d+) ok=(\d+) fail=(\d+) loginOk=(\d+) loginFail=(\d+) unexpectedClose=(\d+)", con)
    r["ok"] = int(m.group(2)) if m else -1
    r["fail"] = (int(m.group(3)) + int(m.group(5)) + int(m.group(6))) if m else -1
    m = re.search(r"ping\s+p50/p95/p99/max = ([\d.]+)/([\d.]+)/([\d.]+)/([\d.]+)", con)
    r["ping_all_p99"] = float(m.group(3)) if m else float("nan")
    m = re.search(r"login\s+p50/p95/p99/max = ([\d.]+)/([\d.]+)/([\d.]+)/([\d.]+)", con)
    r["login_p99"] = float(m.group(3)) if m else float("nan")

    rows = [x for x in csv.DictReader(open(os.path.join(run, "loadtest.csv"))) if float(x["t_sec"]) > t0 + 0.01]
    r["ping_p99_mean"] = mean([float(x["ping_p99_ms"]) for x in rows])
    r["ping_p99_max"] = max([float(x["ping_p99_ms"]) for x in rows], default=float("nan"))
    r["bot_recv_kb"] = max([float(x["max_bot_recv_KBps"]) for x in rows], default=float("nan"))
    r["bot_cpu"] = mean([float(x["bot_cpu_pct"]) for x in rows])
    r["corrected_ps"] = mean([float(x["move_corrected"]) for x in rows]) / 5.0

    lines = [ln for ln in open(os.path.join(run, "server_console.txt"), encoding="utf-8", errors="replace")
             if "[metrics]" in ln and f" players={bots} " in ln]
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
    r["rooms"] = med(grab(r"rooms=(\d+)", 1))
    r["out_ps"] = mean(grab(r"out/s=(\d+)", 1))
    r["calls_ps"] = mean(grab(r"sendCalls/s=(\d+)", 1))
    bufs = grab(r"bufsPerSend=([\d.]+)", 1)
    r["bufs"] = mean(bufs); r["bufs_max"] = max(bufs, default=float("nan"))
    r["late_p50"] = med(grab(late, 2)); r["late_p99_med"] = med(grab(late, 4)); r["late_p99_max"] = max(grab(late, 4), default=float("nan"))
    r["body_p99_med"] = med(grab(body, 4)); r["body_p99_max"] = max(grab(body, 4), default=float("nan"))
    r["view_avg"] = mean(grab(r"viewPlayers\(avg/p99/max\)=([\d.]+)/(\d+)/(\d+)", 1))
    r["sess_max"] = max(grab(r"sendQ\(total/sessionMax\)=(\d+)/(\d+)", 2), default=float("nan"))
    r["log_drop"] = max(grab(r"logDrop=(\d+)", 1), default=0)

    cpu = [x for x in csv.DictReader(open(os.path.join(run, "server_cpu.csv"), encoding="utf-8-sig", errors="replace")) if (x.get("epoch_ms") or "").isdigit()]
    win = [x for x in cpu if (start + t0) * 1000 <= int(x["epoch_ms"]) <= (start + dur) * 1000]
    if len(win) >= 2:
        dt = (int(win[-1]["epoch_ms"]) - int(win[0]["epoch_ms"]))
        r["cpu_user"] = (int(win[-1]["user_ms"]) - int(win[0]["user_ms"])) / dt * 100
        r["cpu_kernel"] = (int(win[-1]["kernel_ms"]) - int(win[0]["kernel_ms"])) / dt * 100
    else:
        r["cpu_user"] = r["cpu_kernel"] = float("nan")
    # 합격선(D-057): 틱 본문·지연 p99 <= 20(구간 p99의 중앙값), 핑 p99 <= 50(구간 p99 평균), 봇 수신 <= 64KB/s, 실패·로그 유실 0
    r["pass"] = (r["body_p99_med"] <= 20 and r["late_p99_med"] <= 20 and r["ping_p99_mean"] <= 50 and r["bot_recv_kb"] <= 64
                 and r["fail"] == 0 and r["ok"] == bots and r["log_drop"] == 0)
    return r

runs = sorted(glob.glob(os.path.join(HERE, "runs", prefix + "*")))
hdr = ["실행", "봇", "방", "통과", "핑 p99(구간 평균/최대, 전체)", "로그인 p99", "틱 지연 p50 / p99(중앙/최대)", "틱 본문 p99(중앙/최대)", "시야 인원 평균", "송신 메시지/s", "WSASend/s", "호출당 메시지(평균/최대)", "CPU 사용자+커널(코어1=100)", "세션 큐 최대", "봇 수신 KB/s", "봇 CPU", "보정/s", "실패", "표본 줄"]
print("| " + " | ".join(hdr) + " |"); print("|" + "---|" * len(hdr))
for run in runs:
    if not os.path.exists(os.path.join(run, "args.txt")): continue
    try: r = load(run)
    except Exception as e:
        print(f"| {os.path.basename(run)} | 읽기 실패: {e} |"); continue
    print("| {name} | {bots} | {rooms:.0f} | {p} | {ping_p99_mean:.0f}/{ping_p99_max:.0f}, {ping_all_p99:.0f} | {login_p99:.0f} | {late_p50:.0f} / {late_p99_med:.0f}/{late_p99_max:.0f} | {body_p99_med:.1f}/{body_p99_max:.1f} | {view_avg:.1f} | {out_ps:.0f} | {calls_ps:.0f} | {bufs:.2f}/{bufs_max:.2f} | {cpu_user:.0f}+{cpu_kernel:.0f} | {sess_max:.0f} | {bot_recv_kb:.1f} | {bot_cpu:.0f} | {corrected_ps:.1f} | {fail} | {n_lines} |".format(p="O" if r["pass"] else "X", **r))
