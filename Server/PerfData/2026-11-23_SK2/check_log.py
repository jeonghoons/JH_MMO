# SK2 서버 Debug 로그 검증: Pierce 명중이 모두 폭/2 + 몸 반경(최대 67) 안, 맞은 시각 ≈ 생성 시각 + 진행 ÷ 초속(± 0.15초), 생성마다 대상 한 번씩.
# 사용: python -I -X utf8 check_log.py <로그 파일>
import re, sys, datetime
SPEED, HALF, BODY_MAX = 1000.0, 40.0, 67.0
ts = lambda s: datetime.datetime.strptime(s, "%Y-%m-%d %H:%M:%S.%f")
made, fails, n = {}, [], 0
for line in open(sys.argv[1], encoding="utf-8", errors="replace"):
    m = re.match(r"(\S+ \S+) .*Pierce 생성: 시전자 (\d+) ", line)
    if m:
        made[m.group(2)] = (ts(m.group(1)), set())
        continue
    m = re.match(r"(\S+ \S+) .*Pierce 명중: 시전자 (\d+) 대상 (\d+) 진행 (-?\d+) 옆 (\d+)", line)
    if m:
        n += 1
        t, who, tgt, along, side = ts(m.group(1)), m.group(2), m.group(3), float(m.group(4)), float(m.group(5))
        t0, seen = made[who]
        dt = (t - t0).total_seconds()
        if side > HALF + BODY_MAX: fails.append(f"옆 거리 {side} > {HALF + BODY_MAX}: {line.strip()}")
        if abs(dt - along / SPEED) > 0.15 + BODY_MAX / SPEED: fails.append(f"시각 {dt:.2f}s 기대 {along / SPEED:.2f}s: {line.strip()}")
        if tgt in seen: fails.append(f"두 번 맞음: {line.strip()}")
        seen.add(tgt)
print(f"Pierce 생성 시전자 {len(made)}명, 명중 {n}줄")
for f in fails: print("FAIL", f)
print("PASS pierce-log" if n and not fails else ("FAIL pierce-log: 명중 없음" if not n else f"FAIL pierce-log: {len(fails)}건"))
