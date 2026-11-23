# A5.4c 결과 표: 서버 PC 자료(runs/<단계>/) + 노트북 봇 결과(Tools/BotPack/results/<단계>/) + PC 몫 봇(runs/s5000_*/pc_loadtest*).
# 서버 값 = 전원 입장 뒤 구간(players >= 목표 인원의 98%)의 5초 값 중앙값(최대). 서버 CPU는 server_cpu.csv(1초 누적 ms)를 같은 구간으로 자름.
# 사용: python -I -X utf8 summarize.py > results.md
import csv, os, statistics

HERE = os.path.dirname(os.path.abspath(__file__))
RUNS = os.path.join(HERE, 'runs')
LAPTOP = os.path.normpath(os.path.join(HERE, '..', '..', '..', 'Tools', 'BotPack', 'results'))
STAGES = ['s1000_1', 's1000_2', 's2500_1', 's2500_2', 's5000_1', 's5000_2', 'dense_250', 'dense_500']
TARGET = {'s1000': 1000, 's2500': 2500, 's5000': 5000, 'dense_250': 250, 'dense_500': 500}


def mm(vals, f='{:g}'):
    if not vals:
        return '-'
    return f'{f.format(statistics.median(vals))}({f.format(max(vals))})'


def read_csv(path):
    if not os.path.exists(path):
        return []
    with open(path, encoding='utf-8') as f:
        return list(csv.DictReader(f))


def server_part(run, target):
    rows = [r for r in read_csv(os.path.join(RUNS, run, 'server_metrics.csv')) if int(float(r['players'])) >= target * 0.98]
    out = {'n': len(rows)}
    for key, col in [('late', 'tick_late_p99_ms'), ('body', 'tick_body_p99_ms'), ('wait', 'job_wait_p99_ms'),
                     ('in', 'packets_in_ps'), ('out', 'messages_out_ps'), ('calls', 'send_calls_ps'), ('sendq', 'sendq_session_max'),
                     ('rooms', 'rooms')]:
        out[key] = [float(r[col]) for r in rows if r.get(col, '') != '']
    # 서버 CPU(코어 1개 = 100%)·메모리: 같은 구간
    cpu = read_csv(os.path.join(RUNS, run, 'server_cpu.csv'))
    if rows and len(cpu) > 2:
        t0 = min(int(r['time']) for r in rows) * 1000 - 5000
        t1 = max(int(r['time']) for r in rows) * 1000
        pts = [(int(c['epoch_ms']), int(c['user_ms']) + int(c['kernel_ms']), int(c.get('ws_mb') or 0)) for c in cpu]
        pct = [(b[1] - a[1]) / max(1, b[0] - a[0]) * 100 for a, b in zip(pts, pts[1:]) if t0 <= b[0] <= t1]
        out['cpu'] = pct
        out['mem'] = [p[2] for p in pts if t0 <= p[0] <= t1 and p[2] > 0]
    else:
        out['cpu'], out['mem'] = [], []
    return out


def bot_part(summary_path, cpu_path=None):
    s = read_csv(summary_path)
    if not s:
        return None
    s = s[0]
    b = {'ok': f"{s['connect_ok']}/{s['bots']}", 'fail': int(s['connect_fail']) + int(s['login_fail']) + int(s['login_give_up']),
         'ping': f"{s['ping_p50_ms']}/{s['ping_p99_ms']}", 'enter': s.get('enter_p99_ms', '-'), 'botcpu': s['bot_cpu_max_pct']}
    if cpu_path:
        c = read_csv(cpu_path)
        if c:
            b['botcpu_all'] = max(float(r['cpu_pct_of_all']) for r in c)
    return b


head = ['단계', '구간수', 'Room수', '틱지연p99', '틱본문p99', '잡대기p99', '수신패킷/s', '송신메시지/s', 'WSASend/s', '송신큐최대',
        '서버CPU%', '서버메모리MB', '봇(위치)', '접속', '실패', '핑p50/p99', '접속시간p99', '봇CPU최대']
print('| ' + ' | '.join(head) + ' |')
print('|' + '---|' * len(head))
for st in STAGES:
    if not os.path.isdir(os.path.join(RUNS, st)):
        continue
    target = TARGET.get(st, TARGET.get(st.split('_')[0], 0))
    sv = server_part(st, target)
    base = [st, str(sv['n']), mm(sv['rooms']), mm(sv['late']), mm(sv['body'], '{:.2f}'), mm(sv['wait']), mm(sv['in'], '{:.0f}'),
            mm(sv['out'], '{:.0f}'), mm(sv['calls'], '{:.0f}'), mm(sv['sendq'], '{:.0f}'), mm(sv['cpu'], '{:.0f}'), mm(sv['mem'], '{:.0f}')]
    bots = []
    if st.startswith('dense'):
        bots.append(('PC', bot_part(os.path.join(RUNS, st, 'loadtest.summary.csv'))))
    else:
        bots.append(('노트북', bot_part(os.path.join(LAPTOP, st, 'loadtest.summary.csv'), os.path.join(LAPTOP, st, 'bot_cpu.csv'))))
        if os.path.exists(os.path.join(RUNS, st, 'pc_loadtest.summary.csv')):
            bots.append(('PC', bot_part(os.path.join(RUNS, st, 'pc_loadtest.summary.csv'))))
    for i, (where, b) in enumerate(bots):
        lead = base if i == 0 else [''] * len(base)
        if b is None:
            print('| ' + ' | '.join(lead + [where, '(결과 없음)', '', '', '', '']) + ' |')
            continue
        cpu = f"{b['botcpu']}%" + (f" (전체 {b['botcpu_all']:.0f}%)" if 'botcpu_all' in b else '')
        print('| ' + ' | '.join(lead + [where, b['ok'], str(b['fail']), b['ping'], b['enter'], cpu]) + ' |')
