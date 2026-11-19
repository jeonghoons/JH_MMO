# runs/<이름>/server_console.txt 의 [metrics] 줄에서 송신 묶기(P1b) 조사 지표를 뽑아 표로 낸다.
# 사용: python -I -X utf8 analyze_cause.py [이름 접두사]
# 정상 구간 = players == 봇 수인 줄(처음 2줄 제외) — analyze.py와 같은 기준. 값은 구간 평균(최대라고 적힌 것만 최대).
import glob, os, re, sys

HERE = os.path.dirname(os.path.abspath(__file__))
prefix = sys.argv[1] if len(sys.argv) > 1 else ""

def mean(v): return sum(v) / len(v) if v else float("nan")

PATS = {
    "calls": r"sendCalls/s=(\d+)",
    "bufs": r"bufsPerSend=([\d.]+)",
    "path": r"sendPath\(flush/continue/direct(?:/kick)? per s\)=(\d+)/(\d+)/(\d+)(?:/(\d+))?",
    "gap": r"flushGap\(p50/p90/p99 ms\)=(\d+)/(\d+)/(\d+)",
    "wake": r"sendWake\(per s, late avg/p50/p99/max ms\)=(\d+) ([\d.]+)/([\d.]+)/([\d.]+)/([\d.]+)",
    "jobs": r"jobs\(exec/repost per s\)=(\d+)/(\d+)",
    "wait": r"jobWait\(p50/p90/p99 ms\)=(\d+)/(\d+)/(\d+) jobRunMax=([\d.]+)ms flushRunMax=([\d.]+)ms",
    "late": r"tickLate\(avg/p50/p95/p99/max ms\)=([\d.]+)/([\d.]+)/([\d.]+)/([\d.]+)/([\d.]+)",
}

hdr = ["실행", "WSASend/s", "호출당 메시지", "송신 경로/s(모음/이어서/바로/워커로 넘김)", "세션 송신 간격 ms(p50/p90/p99)",
       "깨우기/s", "깨우기 지연 ms(평균/p50/p99/최대)", "작업/s", "재포스트/s", "작업 큐 대기 ms(p50/p90/p99)", "작업 실행 최대 ms", "내보내기 최대 ms", "틱 지연 ms(p50/p99)", "표본 줄"]
print("| " + " | ".join(hdr) + " |"); print("|" + "---|" * len(hdr))
for run in sorted(glob.glob(os.path.join(HERE, "runs", prefix + "*"))):
    name = os.path.basename(run)
    try:
        args = open(os.path.join(run, "args.txt"), encoding="utf-8").read().split()
        bots = int(args[args.index("--bots") + 1])
        lines = [ln for ln in open(os.path.join(run, "server_console.txt"), encoding="utf-8", errors="replace")
                 if "[metrics]" in ln and f" players={bots} " in ln][2:]
    except Exception as e:
        print(f"| {name} | 읽기 실패: {e} |"); continue
    def g(key, idx):
        out = []
        for ln in lines:
            m = re.search(PATS[key], ln)
            if m and m.group(idx) is not None: out.append(float(m.group(idx)))
        return out
    wake_max = max(g("wake", 5), default=float("nan"))
    print(f"| {name} | {mean(g('calls',1)):.0f} | {mean(g('bufs',1)):.2f} | "
          f"{mean(g('path',1)):.0f}/{mean(g('path',2)):.0f}/{mean(g('path',3)):.0f}/{mean(g('path',4)):.0f} | "
          f"{mean(g('gap',1)):.0f}/{mean(g('gap',2)):.0f}/{mean(g('gap',3)):.0f} | "
          f"{mean(g('wake',1)):.0f} | {mean(g('wake',2)):.1f}/{mean(g('wake',3)):.1f}/{mean(g('wake',4)):.1f}/{wake_max:.1f} | "
          f"{mean(g('jobs',1)):.0f} | {mean(g('jobs',2)):.0f} | {mean(g('wait',1)):.0f}/{mean(g('wait',2)):.0f}/{mean(g('wait',3)):.0f} | {max(g('wait',4), default=float('nan')):.1f} | {max(g('wait',5), default=float('nan')):.1f} | {mean(g('late',2)):.1f}/{mean(g('late',4)):.1f} | {len(lines)} |")
