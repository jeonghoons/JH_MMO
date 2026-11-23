# A5.2 실행 결과 요약: python -I summarize.py [이름...] (생략하면 runs/ 전부)
# 실행마다: 봇 요약([summary] 중 판정에 쓰는 줄), 서버 [metrics]의 판정 값(틱 지연 p99 중앙값·최대, 잡 대기 p99 최대, 이동 거부 누적, 공격 요청/성공,
# 끝난 뒤 잔류 sessions·players·itemOp inflight·auction dbInFlight), WARN·ERROR 수, DB 판정(db_before/after: gold item_qty item_rows broken active sales trades mails).
import os, re, statistics, sys

HERE = os.path.dirname(os.path.abspath(__file__))
RUNS = os.path.join(HERE, 'runs')
KEEP = ('[summary] attempts', '[summary] login', '[summary] profiles', '[summary] lock', '[summary] chat', '[summary] loot',
        '[summary] trade', '[summary] auction', '[summary] attack', '[summary] inventory', '[summary] room')

def metrics_lines(run):
    logs = os.path.join(RUNS, run, 'logs')
    if not os.path.isdir(logs): return [], 0, 0
    lines, warn, err = [], 0, 0
    for f in sorted(os.listdir(logs)):
        for l in open(os.path.join(logs, f), encoding='utf-8', errors='replace'):
            if '[metrics]' in l: lines.append(l)
            elif ' WARN ' in l: warn += 1
            elif ' ERROR ' in l: err += 1
    return lines, warn, err

def field(line, pat):
    m = re.search(pat, line)
    return m.groups() if m else None

def summarize(run):
    print(f'== {run}')
    d = os.path.join(RUNS, run)
    try: print('  ' + open(os.path.join(d, 'args.txt'), encoding='utf-8').read().split('\n')[0][11:].split(' --out')[0])
    except OSError: pass
    con = os.path.join(d, 'loadtest_console.txt')
    if os.path.exists(con):
        for l in open(con, encoding='utf-8', errors='replace'):
            if l.startswith(KEEP): print('  ' + l.rstrip())
    lines, warn, err = metrics_lines(run)
    if lines:
        active = [l for l in lines if (field(l, r' players=(\d+)') or ('0',))[0] != '0']
        p99 = [float(field(l, r'tickLate\(avg/p50/p95/p99/max ms\)=[\d.]+/[\d.]+/[\d.]+/([\d.]+)')[0]) for l in active if 'tickLate(avg' in l]
        jw = [float(field(l, r'jobWait\(p50/p90/p99 ms\)=\d+/\d+/(\d+)')[0]) for l in active if 'jobWait(' in l]
        last = lines[-1]
        mv = field(last, r'move\(total ok/speed/nav/bad/stale/rate\)=(\d+)/(\d+)/(\d+)/(\d+)/(\d+)/(\d+)')
        atk = [field(l, r'atk\(req/ok per s\)=([\d.]+)/([\d.]+)') for l in active]
        atk = [(float(a), float(b)) for a, b in atk if a]
        print(f'  server: tickLate p99 median/max = {statistics.median(p99) if p99 else 0:.1f}/{max(p99) if p99 else 0:.1f} ms, jobWait p99 max = {max(jw) if jw else 0:.0f} ms, '
              f'peak players = {max(int(field(l, r" players=(\d+)")[0]) for l in lines)}')
        if mv: print(f'  server: move ok/speed/nav/bad = {mv[0]}/{mv[1]}/{mv[2]}/{mv[3]} (cumulative)')
        if atk: print(f'  server: atk req/ok per s (active avg) = {sum(a for a, _ in atk) / len(atk):.1f}/{sum(b for _, b in atk) / len(atk):.1f}')
        res = {k: (field(last, p) or ('?',))[0] for k, p in (('sessions', r'sessions=(\d+)'), ('players', r' players=(\d+)'),
               ('itemOpInflight', r'itemOp\(total sent/fail/busy inflight\)=\d+/\d+/\d+ (\d+)'),
               ('auctionDbInFlight', r'auction\(now listings/dbInFlight[^)]*\)=\d+/(\d+)'), ('logDrop', r'logDrop=(\d+)'),
               ('trades', r'trades=(\d+)'), ('pickup', r'pickup\(picked/discarded\)=(\d+/\d+)'),
               ('kind town/field', r'kind\(town/field players\)=(\d+/\d+)'))}
        peak_kind = max((field(l, r'kind\(town/field players\)=(\d+)/(\d+)') for l in lines), key=lambda t: int(t[0]) + int(t[1]) if t else -1)
        print(f'  server end: ' + ' '.join(f'{k}={v}' for k, v in res.items()) + f', peak town/field = {peak_kind[0]}/{peak_kind[1]}' + f', WARN={warn} ERROR={err}')
    for name in ('db_before.txt', 'db_after.txt'):
        p = os.path.join(d, name)
        if os.path.exists(p): print(f'  {name[:-4]}: ' + open(p, encoding='utf-8', errors='replace').read().strip())

names = sys.argv[1:] or sorted(os.listdir(RUNS))
for n in names:
    if os.path.isdir(os.path.join(RUNS, n)): summarize(n)
