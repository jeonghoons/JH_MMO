# A5.4a 결과 표: runs/*/ 마다 전원 입장 뒤 구간(players = 봇 수)의 서버 지표 중앙값(최대)과 LoadTest 요약.
# 사용: python -I summarize.py [runs 폴더] > results.md
import csv, os, statistics, sys

HERE = os.path.dirname(os.path.abspath(__file__))
RUNS = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, 'runs')

COLS = [  # (표 제목, metrics.csv 열)
    ('틱지연p99', 'tick_late_p99_field_ms'),
    ('타이머늦음p99', 'tick_timer_late_p99_ms'),
    ('큐대기p99', 'tick_queue_wait_p99_ms'),
    ('앞작업p99', 'tick_jobs_ahead_p99'),
    ('틱당이동p50', 'tick_moves_p50'),
    ('틱당이동p99', 'tick_moves_p99'),
    ('틱당이동max', 'tick_moves_max'),
    ('잡대기p99', 'job_wait_p99_ms'),
    ('틱본문p99', 'tick_body_p99_field_ms'),
]


def med_max(vals):
    if not vals:
        return '-'
    return f'{statistics.median(vals):g}({max(vals):g})'


def one(run):
    path = os.path.join(RUNS, run)
    bots = None
    with open(os.path.join(path, 'args.txt'), encoding='utf-8') as f:
        words = f.readline().split()
        bots = int(words[words.index('--bots') + 1])
    rows = []
    with open(os.path.join(path, 'server_metrics.csv'), encoding='utf-8') as f:
        for r in csv.DictReader(f):
            if int(float(r['players'])) >= bots:
                rows.append(r)
    out = [run, str(len(rows))]
    for _, col in COLS:
        out.append(med_max([float(r[col]) for r in rows]))
    summ = os.path.join(path, 'loadtest_summary.csv')
    if os.path.exists(summ):
        with open(summ, encoding='utf-8') as f:
            s = next(csv.DictReader(f))
        out += [s['ping_p99_ms'], s['server_cpu_avg_pct'], s['bot_cpu_avg_pct'], f"{s['connect_ok']}/{s['bots']}"]
    else:
        out += ['-'] * 4
    return out


head = ['실행', '구간수'] + [c[0] for c in COLS] + ['핑p99', '서버CPU평균', '봇CPU평균', '접속']
print('| ' + ' | '.join(head) + ' |')
print('|' + '---|' * len(head))
for run in sorted(os.listdir(RUNS)):
    if os.path.exists(os.path.join(RUNS, run, 'server_metrics.csv')):
        print('| ' + ' | '.join(one(run)) + ' |')
