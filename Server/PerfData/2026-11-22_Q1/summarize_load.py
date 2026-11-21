# run_load.sh 결과 요약: 봇이 다 들어온 구간(players >= 봇 수)의 틱 지연·틱 본문·잡 대기를 직전 항목(B2)의 같은 조건 실행과 나란히 본다.
# 사용: python -I -X utf8 summarize_load.py <이름> <봇 수>    (예: hunt_200 200) → runs/<이름>/summary.txt에도 남긴다
import csv, os, re, statistics, sys
HERE = os.path.dirname(os.path.abspath(__file__))
name, bots = sys.argv[1], int(sys.argv[2])
out = []


def read(path):
    rows = [r for r in csv.DictReader(open(path, encoding="utf-8")) if int(float(r["players"])) >= bots]
    late = [float(r["tick_late_p99_ms"]) for r in rows]
    body = [float(r["tick_body_p99_ms"]) for r in rows]
    wait = [float(r["job_wait_p99_ms"]) for r in rows]
    return len(rows), statistics.median(late), max(late), statistics.median(body), max(body), max(wait)


for label, folder in (("Q1", HERE), ("B2", os.path.join(HERE, "..", "2026-11-22_B2"))):
    run = os.path.join(folder, "runs", name)
    try:
        n, late_med, late_max, body_med, body_max, wait_max = read(os.path.join(run, "server_metrics.csv"))
        out.append(f"{label} {name}: 표본 {n}행(players >= {bots}), 틱 지연 p99 중앙값 {late_med:.1f}ms(최대 {late_max:.1f}), "
                   f"틱 본문 p99 중앙값 {body_med:.2f}ms(최대 {body_max:.2f}), 잡 대기 p99 최대 {wait_max:.0f}ms")
    except Exception as e:
        out.append(f"{label} {name}: 읽지 못함 ({e})")
        continue
    console = os.path.join(run, "loadtest_console.txt")
    if os.path.exists(console):
        text = open(console, encoding="utf-8", errors="replace").read()
        for key in ("attempts=", "login   p50", "ping    p50", "moveCorrected=", "room:"):
            m = re.search(r"\[summary\] [^\n]*" + re.escape(key) + r"[^\n]*", text)
            if m:
                out.append(f"  {label} {m.group(0)}")

run = os.path.join(HERE, "runs", name)
logs = [os.path.join(run, "logs", f) for f in os.listdir(os.path.join(run, "logs"))] if os.path.isdir(os.path.join(run, "logs")) else []
warn = err = 0
last = ""
for p in logs:
    for line in open(p, encoding="utf-8", errors="replace"):
        warn += " WARN " in line
        err += " ERROR " in line
        if "[metrics]" in line:
            last = line
out.append(f"Q1 {name}: 서버 로그 ERROR {err}줄 · WARN {warn}줄")
pick = re.findall(r"sessions=\d+|players=\d+|itemOp\([^)]*\)=\S+ \S+|login\(total kick/busy/forced\)=\S+|world\(channels/roster/chMax\)=\S+|roomMove\([^)]*\)=\S+ \S+|"
                  r"move\([^)]*\)=\S+|roomsAlive=\d+|rooms=\d+|quest\([^)]*\)=\S+ \S+ \S+|auction\([^)]*\)=\S+", last)
out.append("Q1 마지막 [metrics]: " + " ".join(pick))
print("\n".join(out))
open(os.path.join(run, "summary.txt"), "w", encoding="utf-8").write("\n".join(out) + "\n")
