# DG3c-1 측정 요약: runs/<이름>/ 의 server_metrics.csv(서버 5초 지표)와 loadtest_console.txt(봇 요약)를 표로 만든다.
# 사용: python -I -X utf8 summarize.py [이름 접두사...]  → 표준 출력(마크다운 표). 접속 램프 구간은 빼고 인원이 최대의 95% 이상인 행만 쓴다.
import csv, glob, os, re, sys

HERE = os.path.dirname(os.path.abspath(__file__))


def med(v):
    v = sorted(v)
    return v[len(v) // 2] if v else float("nan")


def mx(v):
    return max(v) if v else float("nan")


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
    last = rows[-1]
    out = {
        "players": peak,
        "dungeon_players": int(mx(f("dungeon_players"))),
        "instances": int(mx(f("dungeons"))),
        "created": int(float(last["dungeons_created"])), "closed": int(float(last["dungeons_closed"])),
        "rooms": max(int(r["rooms"]) for r in rows), "alive_max": max(int(r["rooms_alive"]) for r in rows),
        "monsters": int(mx(f("monsters"))),
        "late": med(f("tick_late_p99_ms")), "late_max": mx(f("tick_late_p99_ms")),
        "late_field": med(f("tick_late_p99_field_ms")), "late_field_max": mx(f("tick_late_p99_field_ms")),
        "late_dg": med(f("tick_late_p99_dungeon_ms")), "late_dg_max": mx(f("tick_late_p99_dungeon_ms")),
        "body_dg": med(f("tick_body_p99_dungeon_ms")), "body_field": med(f("tick_body_p99_field_ms")),
        "jobwait": med(f("job_wait_p99_ms")), "jobwait_max": mx(f("job_wait_p99_ms")),
        "jobrun_max": mx(f("job_run_max_ms")),
        "moves": int(float(last["room_moves"])),
        # 봇이 모두 나간 뒤의 마지막 행: 잔류 확인
        "end": "rooms %s alive %s dungeons %s monsters %s" % (last["rooms"], last["rooms_alive"], last["dungeons"], last["monsters"]),
    }
    con = os.path.join(run, "loadtest_console.txt")
    text = open(con, encoding="utf-8", errors="replace").read() if os.path.exists(con) else ""
    m = re.search(r"ping\s+p50/p95/p99/max = ([\d.]+)/([\d.]+)/([\d.]+)/([\d.]+)", text)
    out["ping"] = (float(m.group(1)), float(m.group(3)), float(m.group(4))) if m else (float("nan"),) * 3
    m = re.search(r"attempts=(\d+) ok=(\d+) fail=(\d+).*unexpectedClose=(\d+) protocolError=(\d+)", text)
    out["conn"] = "%s/%s close %s proto %s" % (m.group(2), m.group(1), m.group(4), m.group(5)) if m else "?"
    m = re.search(r"dungeon: enterSent=(\d+) entered=(\d+) exitSent=(\d+) cleared=(\d+)", text)
    out["bot"] = "입장 %s 퇴장 요청 %s 클리어 %s" % (m.group(2), m.group(3), m.group(4)) if m else "-"
    logs = glob.glob(os.path.join(run, "logs", "*.log"))
    out["errors"] = sum(1 for p in logs for line in open(p, encoding="utf-8", errors="replace") if " ERROR " in line or " WARN " in line)
    # 인스턴스 생성 시간(서비스 큐를 잡는 시간): 로그의 "던전 인스턴스 N 생성 (…, X.Xms, …)"
    ms = [float(x) for p in logs for x in re.findall(r"던전 인스턴스 \d+ 생성 \([^)]*?([\d.]+)ms", open(p, encoding="utf-8", errors="replace").read())]
    out["create_ms"] = (med(ms), mx(ms)) if ms else (float("nan"), float("nan"))
    # 서버 CPU(코어 1개 = 100%): 정상 구간의 사용자 + 커널 시간 증가량
    out["cpu"] = float("nan")
    try:
        data = open(os.path.join(run, "server_cpu.csv"), "rb").read()
        t = data.decode("utf-16" if data[:2] in (bytes([255, 254]), bytes([254, 255])) else "utf-8", "replace")
        c = [l.split(",") for l in t.splitlines()[1:] if l.count(",") == 2]
        if len(c) > 20:
            a, b = c[len(c) // 3], c[-3]
            out["cpu"] = (int(b[1]) + int(b[2]) - int(a[1]) - int(a[2])) / max(1, int(b[0]) - int(a[0])) * 100
    except Exception:
        pass
    return out


prefixes = sys.argv[1:]
runs = sorted(d for d in glob.glob(os.path.join(HERE, "runs", "*")) if os.path.isdir(d))
print("| 실행 | 인원(던전 안) | 인스턴스(최대/만든/닫은) | Room 객체 최대 | 몬스터 | 접속 | 핑 p50/p99/max ms | 틱 지연 p99 전체 중앙값(최대) | 사냥터 | 던전 | 틱 본문 p99 사냥터/던전 ms | 잡 대기 p99 ms(최대) | 잡 최장 ms | 생성 ms 중앙값(최대) | CPU % | 봇 | WARN·ERROR | 끝난 뒤 |")
print("|" + "---|" * 18)
for run in runs:
    name = os.path.basename(run)
    if not os.path.exists(os.path.join(run, "loadtest_console.txt")):
        continue
    if prefixes and not any(name.startswith(p) for p in prefixes):
        continue
    s = summarize(run)
    if s is None:
        continue
    print("| %s | %d (%d) | %d/%d/%d | %d | %d | %s | %.1f / %.1f / %.1f | %.1f (%.1f) | %.1f (%.1f) | %.1f (%.1f) | %.2f / %.2f | %.0f (%.0f) | %.1f | %.1f (%.1f) | %.0f | %s | %d | %s |" % (
        name, s["players"], s["dungeon_players"], s["instances"], s["created"], s["closed"], s["alive_max"], s["monsters"], s["conn"],
        s["ping"][0], s["ping"][1], s["ping"][2], s["late"], s["late_max"], s["late_field"], s["late_field_max"], s["late_dg"], s["late_dg_max"],
        s["body_field"], s["body_dg"], s["jobwait"], s["jobwait_max"], s["jobrun_max"], s["create_ms"][0], s["create_ms"][1], s["cpu"], s["bot"], s["errors"], s["end"]))
