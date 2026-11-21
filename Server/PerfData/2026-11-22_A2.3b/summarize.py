# PL1 측정 요약: runs/<이름>/ 의 server_console.txt([metrics] 5초 줄), loadtest.csv(봇 5초 구간), loadtest_console.txt(봇 요약),
# server_cpu.csv(서버 사용자 + 커널 누적 ms), logs/*.log(WARN·ERROR)를 표로 만든다.
# 사용: python -I -X utf8 summarize.py [이름 접두사...]  → 표준 출력(마크다운 표 두 개).
# 정상 구간 = 서버 players가 최대의 95% 이상인 [metrics] 줄(접속 램프·종료 구간 제외). 값은 그 구간 5초 값들의 중앙값(최대).
import csv, glob, os, re, sys

HERE = os.path.dirname(os.path.abspath(__file__))
NAN = float("nan")


def med(v):
    v = sorted(v)
    return v[len(v) // 2] if v else NAN


def mx(v):
    return max(v) if v else NAN


def parse_metrics(path):
    rows = []
    if not os.path.exists(path):
        return rows
    for line in open(path, encoding="utf-8", errors="replace"):
        if "[metrics]" not in line:
            continue
        r = {}
        def g(pat, names, conv=float):
            m = re.search(pat, line)
            if m:
                for n, v in zip(names, m.groups()):
                    r[n] = conv(v)
        g(r"sessions=(\d+) rooms=(\d+) players=(\d+)", ["sessions", "rooms", "players"], int)
        g(r"in/s=(\d+) out/s=(\d+) sendCalls/s=(\d+) bufsPerSend=([\d.]+)", ["in", "out", "sendCalls", "bufs"])
        g(r"tickLate\(avg/p50/p95/p99/max ms\)=([\d.]+)/([\d.]+)/([\d.]+)/([\d.]+)/([\d.]+)", ["lateAvg", "lateP50", "lateP95", "lateP99", "lateMax"])
        g(r"tickBody\(avg/p50/p95/p99/max ms\)=([\d.]+)/([\d.]+)/([\d.]+)/([\d.]+)/([\d.]+)", ["bodyAvg", "bodyP50", "bodyP95", "bodyP99", "bodyMax"])
        g(r"viewPlayers\(avg/p99/max\)=([\d.]+)/(\d+)/(\d+)", ["viewAvg", "viewP99", "viewMax"])
        g(r"sendQ\(total/sessionMax\)=(\d+)/(\d+)", ["sendqTotal", "sendqMax"])
        g(r"logDrop=(\d+)", ["logDrop"], int)
        g(r"jobs\(exec/repost per s\)=(\d+)/(\d+)", ["jobs", "repost"])
        g(r"jobWait\(p50/p90/p99 ms\)=([\d.]+)/([\d.]+)/([\d.]+)", ["jwP50", "jwP90", "jwP99"])
        g(r"jobRunMax=([\d.]+)ms flushRunMax=([\d.]+)ms", ["jobRunMax", "flushRunMax"])
        g(r"jobDispatch\(p50/p90/p99 ms\)=([\d.]+)/([\d.]+)/([\d.]+)", ["jdP50", "jdP90", "jdP99"])
        g(r"busy\(job/flush ms per s\)=(\d+)/(\d+)", ["busyJob", "busyFlush"])
        g(r"sendWake\(per s, late avg/p50/p99/max ms\)=(\d+) ([\d.]+)/([\d.]+)/([\d.]+)/([\d.]+)", ["wake", "wakeAvg", "wakeP50", "wakeP99", "wakeMax"])
        g(r"flushGap\(p50/p90/p99 ms\)=(\d+)/(\d+)/(\d+)", ["gapP50", "gapP90", "gapP99"])
        g(r"world\(channels/roster/chMax\)=(\d+)/(\d+)/(\d+)", ["channels", "roster", "chMax"], int)
        g(r"kind\(town/field players\)=(\d+)/(\d+)", ["town", "field"], int)
        g(r"tickLateP99\(town/field ms\)=([\d.]+)/([\d.]+)", ["lateTown", "lateField"])
        g(r"tickBodyP99\(town/field ms\)=([\d.]+)/([\d.]+)", ["bodyTown", "bodyField"])
        g(r"conn\(sessionObjs/handles idleKick/sendqKick\)=(\d+)/(\d+) (\d+)/(\d+)", ["sessionObjs", "handles", "idleKick", "sendqKick"], int)
        g(r"login\(total kick/busy/forced\)=(\d+)/(\d+)/(\d+)", ["kick", "busy", "forced"], int)
        if "players" in r:
            rows.append(r)
    return rows


def read_csv_any(path):
    if not os.path.exists(path):
        return []
    data = open(path, "rb").read()
    text = data.decode("utf-16" if data[:2] in (b"\xff\xfe", b"\xfe\xff") else "utf-8", "replace")
    return list(csv.DictReader(text.splitlines()))


def summarize(run):
    rows = parse_metrics(os.path.join(run, "server_console.txt"))
    if not rows:
        return None
    peak = max(r["players"] for r in rows)
    steady = [r for r in rows if peak > 0 and r["players"] >= peak * 0.95]
    f = lambda k: [r[k] for r in steady if k in r]
    s = {"peak": peak, "n": len(steady), "rooms": max(r["rooms"] for r in rows),
         "channels": max(r.get("channels", 0) for r in rows),
         "town": mx(f("town")), "field": mx(f("field")), "chMax": mx(f("chMax"))}
    for k in ["in", "out", "sendCalls", "bufs", "lateP50", "lateP99", "lateMax", "bodyP99", "bodyMax", "viewAvg", "viewMax",
              "sendqMax", "jwP50", "jwP99", "jdP50", "jdP99", "busyJob", "busyFlush", "jobRunMax", "wakeP99", "gapP50", "gapP99",
              "lateTown", "lateField", "bodyField", "handles", "jobs", "repost"]:
        s[k] = (med(f(k)), mx(f(k)))
    last = rows[-1]
    s["logDrop"] = max(r.get("logDrop", 0) for r in rows)
    s["end"] = "s%d r%d forced%d sendq%d" % (last.get("sessions", -1), last.get("roster", -1), last.get("forced", -1), last.get("sendqTotal", -1))
    s["kicks"] = "%d/%d" % (last.get("idleKick", 0), last.get("sendqKick", 0))
    # 봇 5초 구간(정상 = in_game이 최대의 95% 이상, 마지막 구간 제외)
    lt = read_csv_any(os.path.join(run, "loadtest.csv"))
    if lt:
        pk = max(int(r["in_game"]) for r in lt)
        st = [r for r in lt if int(r["in_game"]) >= pk * 0.95][:-1] or lt
        g = lambda k: [float(r[k]) for r in st if r.get(k) not in (None, "")]
        s["pingWin"] = (med(g("ping_p99_ms")), mx(g("ping_p99_ms")))
        s["mem"] = max(float(r["server_mem_mb"]) for r in lt)
        s["botCpu"] = (med(g("bot_cpu_pct")), mx(g("bot_cpu_pct")))
        s["botRecv"] = mx(g("max_bot_recv_KBps"))
        s["srvCpuLT"] = med(g("server_cpu_pct"))
    con = os.path.join(run, "loadtest_console.txt")
    text = open(con, encoding="utf-8", errors="replace").read() if os.path.exists(con) else ""
    m = re.search(r"ping\s+p50/p95/p99/max = ([\d.]+)/([\d.]+)/([\d.]+)/([\d.]+)", text)
    s["ping"] = tuple(float(x) for x in m.groups()) if m else (NAN,) * 4
    m = re.search(r"attempts=(\d+) ok=(\d+) fail=(\d+) loginOk=(\d+) loginFail=(\d+) unexpectedClose=(\d+) protocolError=(\d+) sendDropped=(\d+)", text)
    s["conn"] = "%s/%s 실패 %s 로그인실패 %s 끊김 %s" % (m.group(2), m.group(1), m.group(3), m.group(5), m.group(6)) if m else "?"
    s["sendDropped"] = int(m.group(8)) if m else -1
    logs = glob.glob(os.path.join(run, "logs", "*.log"))
    lines = []
    for p in logs:
        try:
            lines += open(p, encoding="utf-8", errors="replace").readlines()
        except OSError:  # 아직 서버가 쓰는 중인 실행
            pass
    s["warn"] = sum(1 for l in lines if " WARN " in l)
    s["error"] = sum(1 for l in lines if " ERROR " in l)
    # 서버 CPU(코어 1개 = 100%): 정상 구간(앞 1/3 이후 ~ 끝 3초 전)의 사용자 + 커널 시간 증가량, 커널 비중
    s["cpu"] = s["kern"] = NAN
    c = [l for l in read_csv_any(os.path.join(run, "server_cpu.csv"))]
    if len(c) > 20:
        a, b = c[len(c) // 3], c[-3]
        dt = max(1, int(b["epoch_ms"]) - int(a["epoch_ms"]))
        du, dk = int(b["user_ms"]) - int(a["user_ms"]), int(b["kernel_ms"]) - int(a["kernel_ms"])
        s["cpu"] = (du + dk) / dt * 100
        s["kern"] = dk / max(1, du + dk) * 100
    return s


def fm(t, p=1):
    return ("%%.%df (%%.%df)" % (p, p)) % t


prefixes = sys.argv[1:]
runs = sorted((d for d in glob.glob(os.path.join(HERE, "runs", "*")) if os.path.isdir(d)), key=os.path.getmtime)
res = []
for run in runs:
    name = os.path.basename(run)
    if prefixes and not any(name.startswith(p) for p in prefixes):
        continue
    s = summarize(run)
    if s:
        res.append((name, s))

print("| 실행 | 동접(채널/Room, 시야 평균) | 접속 | 틱 지연 p50 / p99 ms 중앙값(최대) | 틱 본문 p99 | 작업 큐 대기 p50 / p99 | IOCP 배정 p99 | 방 스레드 busy ms/s | 핑 p99 전체 / 5초 구간 중앙값(최대) | 핑 max | WSASend/s · 메시지/호출 | out/s | 서버 CPU 코어% (커널%) · 메모리 MB | 봇 CPU % · 봇1 수신 KB/s | logDrop · WARN/ERROR |")
print("|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|")
for name, s in res:
    print("| %s | %d (%d/%d, %.0f) | %s | %.1f / %s | %s | %.0f / %s | %s | %s | %.1f / %s | %.0f | %.0f · %.2f | %.0f | %.0f (%.0f) · %.0f | %s · %.1f | %d · %d/%d |" % (
        name, s["peak"], s["channels"], s["rooms"], s["viewAvg"][0], s["conn"],
        s["lateP50"][0], fm(s["lateP99"]), fm(s["bodyP99"], 2), s["jwP50"][0], fm(s["jwP99"], 0), fm(s["jdP99"], 0), fm(s["busyJob"], 0),
        s["ping"][2], fm(s.get("pingWin", (NAN, NAN))), s["ping"][3], s["sendCalls"][0], s["bufs"][0], s["out"][0],
        s["cpu"], s["kern"], s.get("mem", NAN), fm(s.get("botCpu", (NAN, NAN)), 0), s.get("botRecv", NAN),
        s["logDrop"], s["warn"], s["error"]))
print()
print("| 실행 | 정상 구간 표본 | 채널 최대 인원 | in/s | jobs/s · 재포스트/s | jobRunMax ms | 송신 깨우기 지연 p99 | flushGap p50/p99 | sendQ 세션 최대 | 핸들 | 사냥터 틱 지연 p99 | 끝난 뒤(세션/명부/forced/sendQ) | idle/sendq 킥 | 봇 sendDropped |")
print("|---|---|---|---|---|---|---|---|---|---|---|---|---|---|")
for name, s in res:
    print("| %s | %d | %s | %.0f | %.0f · %.0f | %s | %s | %.0f / %.0f | %s | %.0f | %s | %s | %s | %d |" % (
        name, s["n"], s["chMax"], s["in"][0], s["jobs"][0], s["repost"][0], fm(s["jobRunMax"]), fm(s["wakeP99"]), s["gapP50"][0], s["gapP99"][0],
        fm(s["sendqMax"], 0), s["handles"][1], fm(s["lateField"]), s["end"], s["kicks"], s["sendDropped"]))
