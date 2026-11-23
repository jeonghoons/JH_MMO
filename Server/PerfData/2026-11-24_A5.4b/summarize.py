# A5.4b 결과 표: runs/login_*/ 마다 봇 요약(접속 시간·로그인·실패)과 서버 로그인 지표(구간 값의 최대 — 몰릴 때를 봄).
# 사용: python -I -X utf8 summarize.py [runs 폴더] > results.md
import csv, os, sys

HERE = os.path.dirname(os.path.abspath(__file__))
RUNS = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, 'runs')

SERVER = [  # (표 제목, metrics.csv 열) — 로그인이 있던 구간들의 최대
    ('accept/s', 'accepts_ps'),
    ('로그인/s', 'logins_ps'),
    ('서버로그인p99', 'login_total_p99_ms'),
    ('DB실행p99', 'login_db_exec_p99_ms'),
    ('DB대기p99', 'db_wait_p99_ms'),
    ('DB큐최대', 'db_queue_max'),
    ('로비p99', 'login_lobby_p99_ms'),
    ('틱지연p99', 'tick_late_p99_ms'),
]


def fmt(v):
    return f'{v:g}'


def one(run):
    path = os.path.join(RUNS, run)
    out = [run]
    with open(os.path.join(path, 'loadtest.summary.csv'), encoding='utf-8') as f:
        s = next(csv.DictReader(f))
    out += [f"{s['connect_ok']}/{s['bots']}", s['connect_fail'], s['login_fail'], s['login_retry'], s['login_give_up'],
            f"{s['enter_p50_ms']}/{s['enter_p99_ms']}/{s['enter_max_ms']}", f"{s['login_p50_ms']}/{s['login_p99_ms']}",
            s['server_cpu_max_pct'], s['bot_cpu_max_pct']]
    rows = []
    with open(os.path.join(path, 'server_metrics.csv'), encoding='utf-8') as f:
        for r in csv.DictReader(f):
            if float(r['logins_ps']) > 0:
                rows.append(r)
    for _, col in SERVER:
        out.append(fmt(max(float(r[col]) for r in rows)) if rows else '-')
    return out


head = ['실행', '접속', '접속실패', '로그인실패', '재시도', '포기', '접속시간p50/p99/max(ms)', '로그인p50/p99', '서버CPU최대', '봇CPU최대'] + [c[0] for c in SERVER]
print('| ' + ' | '.join(head) + ' |')
print('|' + '---|' * len(head))
for run in sorted(os.listdir(RUNS), key=lambda r: (len(r), r)):
    if run.startswith('login_') and os.path.exists(os.path.join(RUNS, run, 'loadtest.summary.csv')):
        print('| ' + ' | '.join(one(run)) + ' |')
