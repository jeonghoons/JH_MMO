# DG3b 측정 요약: runs/<이름>/ 의 server_metrics.csv(서버 5초 지표)와 loadtest_console.txt(봇 요약)를 표로 만든다.
# 사용: python -I -X utf8 summarize.py [이름 접두사...]  → 표준 출력(마크다운 표). 접속 램프 구간은 빼고 인원이 최대의 95% 이상인 행만 쓴다.
import csv, glob, os, re, sys

HERE = os.path.dirname(os.path.abspath(__file__))


def med(v):
    v = sorted(v)
    return v[len(v) // 2] if v else float("nan")


def summarize(run):
    path = os.path.join(run, "server_metrics.csv")
    if not os.path.exists(path):
        return None
    rows = list(csv.DictReader(open(path, encoding="utf-8")))
    if not rows:
        return None
    peak = max(int(r["players"]) for r in rows)
    steady = [r for r in rows if int(r["players"]) >= peak * 0.95 and peak > 0]
    f = lambda k: [float(r[k]) for r in steady if r.get(k) not in (None, "")]
    out = {
        "players": peak,
        "rooms": max(int(r["rooms"]) for r in rows),
        "late_p99_med": med(f("tick_late_p99_ms")), "late_p99_max": max(f("tick_late_p99_ms") or [float("nan")]),
        "body_p99_med": med(f("tick_body_p99_ms")),
        "sendq_max": max(f("sendq_session_max") or [0]),
    }
    if "town_players" in rows[0]:
        out["town"] = max(int(r["town_players"]) for r in steady)
        out["field"] = max(int(r["field_players"]) for r in steady)
        out["late_town"] = med(f("tick_late_p99_town_ms")); out["late_town_max"] = max(f("tick_late_p99_town_ms") or [float("nan")])
        out["late_field"] = med(f("tick_late_p99_field_ms")); out["late_field_max"] = max(f("tick_late_p99_field_ms") or [float("nan")])
        out["body_town"] = med(f("tick_body_p99_town_ms")); out["body_field"] = med(f("tick_body_p99_field_ms"))
        out["moves"] = max(int(float(r["room_moves"])) for r in rows)
    con = os.path.join(run, "loadtest_console.txt")
    text = open(con, encoding="utf-8", errors="replace").read() if os.path.exists(con) else ""
    m = re.search(r"ping\s+p50/p95/p99/max = ([\d.]+)/([\d.]+)/([\d.]+)/([\d.]+)", text)
    out["ping"] = (float(m.group(1)), float(m.group(3)), float(m.group(4))) if m else (float("nan"),) * 3
    m = re.search(r"attempts=(\d+) ok=(\d+) fail=(\d+).*unexpectedClose=(\d+) protocolError=(\d+)", text)
    out["conn"] = "%s/%s close %s proto %s" % (m.group(2), m.group(1), m.group(4), m.group(5)) if m else "?"
    logs = glob.glob(os.path.join(run, "logs", "*.log"))
    out["errors"] = sum(1 for p in logs for line in open(p, encoding="utf-8", errors="replace") if " ERROR " in line or " WARN " in line)
    # 서버 CPU(코어 1개 = 100%): 정상 구간의 사용자 + 커널 시간 증가량
    cpu = os.path.join(run, "server_cpu.csv")
    out["cpu"] = float("nan")
    try:
        data = open(cpu, "rb").read()
        text = data.decode("utf-16" if data[:2] in (bytes([255, 254]), bytes([254, 255])) else "utf-8", "replace")
        c = [l.split(",") for l in text.splitlines()[1:] if l.count(",") == 2]
        if len(c) > 20:
            a, b = c[len(c) // 3], c[-3]
            out["cpu"] = (int(b[1]) + int(b[2]) - int(a[1]) - int(a[2])) / max(1, int(b[0]) - int(a[0])) * 100
    except Exception:
        pass
    return out


prefixes = sys.argv[1:]
runs = sorted(d for d in glob.glob(os.path.join(HERE, "runs", "*")) if os.path.isdir(d))
print("| 실행 | 인원(마을/사냥터) | 접속 | 핑 p50/p99/max ms | 틱 지연 p99 ms 중앙값(최대) | 마을 / 사냥터 틱 지연 p99 중앙값(최대) | 틱 본문 p99 마을 / 사냥터 ms | 서버 CPU % | Room 이동 | WARN·ERROR |")
print("|---|---|---|---|---|---|---|---|---|---|")
for run in runs:
    name = os.path.basename(run)
    if prefixes and not any(name.startswith(p) for p in prefixes):
        continue
    s = summarize(run)
    if s is None:
        continue
    split = "%d (%d/%d)" % (s["players"], s["town"], s["field"]) if "town" in s else "%d (Room %d개)" % (s["players"], s["rooms"])
    kinds = "%.1f (%.1f) / %.1f (%.1f)" % (s["late_town"], s["late_town_max"], s["late_field"], s["late_field_max"]) if "town" in s else "-"
    body = "%.2f / %.2f" % (s["body_town"], s["body_field"]) if "town" in s else "%.2f" % s["body_p99_med"]
    print("| %s | %s | %s | %.1f / %.1f / %.1f | %.1f (%.1f) | %s | %s | %.0f | %s | %d |" % (
        name, split, s["conn"], s["ping"][0], s["ping"][1], s["ping"][2], s["late_p99_med"], s["late_p99_max"], kinds, body, s["cpu"], s.get("moves", "-"), s["errors"]))
