# A5.4d 결과 표: runs/<이름>/ 의 서버 지표(server_metrics.csv·server_cpu.csv)와 봇 요약(loadtest.summary.csv·loadtest_console.txt).
# 서버 값 = 전원 입장 뒤 구간(players >= 목표 인원의 98%)의 5초 값 중앙값(최대) — A5.4c summarize.py와 같은 방식.
# 목표 인원은 이름 안의 숫자(dense500_mb33 → 500). 노트북 봇 결과가 있으면(Tools/BotPack/results/<이름>/) 함께 적는다.
# 사용: python -I -X utf8 summarize.py [이름...] > results.md
import csv, os, re, statistics, sys

HERE = os.path.dirname(os.path.abspath(__file__))
RUNS = os.path.join(HERE, 'runs')
LAPTOP = os.path.normpath(os.path.join(HERE, '..', '..', '..', 'Tools', 'BotPack', 'results'))


def mm(vals, f='{:g}'):
    if not vals:
        return '-'
    return f'{f.format(statistics.median(vals))}({f.format(max(vals))})'


def read_csv(path):
    if not os.path.exists(path):
        return []
    with open(path, encoding='utf-8') as f:
        return list(csv.DictReader(f))


def target_of(name):
    m = re.search(r'(\d{3,4})', name)
    return int(m.group(1)) if m else 0


def server_part(run, target):
    rows = [r for r in read_csv(os.path.join(RUNS, run, 'server_metrics.csv')) if int(float(r['players'])) >= target * 0.98]
    out = {'n': len(rows)}
    for key, col in [('late', 'tick_late_p99_ms'), ('body', 'tick_body_p99_ms'), ('wait', 'job_wait_p99_ms'),
                     ('in', 'packets_in_ps'), ('out', 'messages_out_ps'), ('calls', 'send_calls_ps'), ('sendq', 'sendq_session_max'),
                     ('rooms', 'rooms')]:
        out[key] = [float(r[col]) for r in rows if r.get(col, '') != '']
    cpu = read_csv(os.path.join(RUNS, run, 'server_cpu.csv'))
    out['cpu'] = []
    if rows and len(cpu) > 2:
        t0 = min(int(r['time']) for r in rows) * 1000 - 5000
        t1 = max(int(r['time']) for r in rows) * 1000
        pts = [(int(c['epoch_ms']), int(c['user_ms']) + int(c['kernel_ms'])) for c in cpu]
        out['cpu'] = [(b[1] - a[1]) / max(1, b[0] - a[0]) * 100 for a, b in zip(pts, pts[1:]) if t0 <= b[0] <= t1]
    return out


def bot_part(folder, summary_name='loadtest.summary.csv'):
    s = read_csv(os.path.join(folder, summary_name))
    if not s:
        return None
    s = s[0]
    b = {'ok': f"{s['connect_ok']}/{s['bots']}", 'fail': int(s['connect_fail']) + int(s['login_fail']) + int(s['login_give_up']),
         'ping': f"{s['ping_p50_ms']}/{s['ping_p99_ms']}", 'enter': s.get('enter_p99_ms', '-'), 'botcpu': s['bot_cpu_max_pct'], 'recv': '-'}
    console = os.path.join(folder, 'loadtest_console.txt')
    if os.path.exists(console):
        m = re.search(r'totalRecvBytes=(\d+)', open(console, encoding='utf-8', errors='replace').read())
        if m:
            b['recv'] = f"{int(m.group(1)) / 1e6:.0f}"
    return b


names = sys.argv[1:] or sorted(d for d in os.listdir(RUNS) if os.path.isdir(os.path.join(RUNS, d)) and not d.startswith('smoke'))
head = ['실행', '구간수', 'Room수', '틱지연p99', '틱본문p99', '잡대기p99', '수신패킷/s', '송신메시지/s', 'WSASend/s', '송신큐최대',
        '서버CPU%', '봇(위치)', '접속', '실패', '핑p50/p99', '접속시간p99', '봇CPU최대', '봇수신MB']
print('| ' + ' | '.join(head) + ' |')
print('|' + '---|' * len(head))
for st in names:
    sv = server_part(st, target_of(st))
    base = [st, str(sv['n']), mm(sv['rooms']), mm(sv['late']), mm(sv['body'], '{:.2f}'), mm(sv['wait']), mm(sv['in'], '{:.0f}'),
            mm(sv['out'], '{:.0f}'), mm(sv['calls'], '{:.0f}'), mm(sv['sendq'], '{:.0f}'), mm(sv['cpu'], '{:.0f}')]
    bots = [('PC', bot_part(os.path.join(RUNS, st)))]
    if os.path.isdir(os.path.join(LAPTOP, st)):
        bots.insert(0, ('노트북', bot_part(os.path.join(LAPTOP, st))))
    if os.path.exists(os.path.join(RUNS, st, 'pc_loadtest.summary.csv')):
        bots.append(('PC', bot_part(os.path.join(RUNS, st), 'pc_loadtest.summary.csv')))
    bots = [(w, b) for w, b in bots if b is not None] or [('-', None)]
    for i, (where, b) in enumerate(bots):
        lead = base if i == 0 else [''] * len(base)
        if b is None:
            print('| ' + ' | '.join(lead + [where, '(결과 없음)', '', '', '', '', '']) + ' |')
            continue
        print('| ' + ' | '.join(lead + [where, b['ok'], str(b['fail']), b['ping'], b['enter'], f"{b['botcpu']}%", b['recv']]) + ' |')
