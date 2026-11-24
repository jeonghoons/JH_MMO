# A7.2 벤치마크 보고서(Docs/BENCHMARK.md)의 그래프와 표를 원자료에서 다시 만든다.
# 원자료: Server/PerfData/<날짜>_<ID>/runs/<실행>/ (server_metrics.csv·server_cpu.csv·loadtest.csv·loadtest.summary.csv)
#         Tools/BotPack/results/<실행>/ (노트북 봇 — A5.4c·A5.4d 단계 램프)
# 집계 규칙(A5.4c·A5.4d summarize.py와 같음): 서버 값 = players >= 목표 인원 × 0.98인 5초 구간 값의 중앙값(최대),
#   CPU = server_cpu.csv 누적(사용자+커널) ms의 1초 차분(코어 1개 = 100%), 메모리 = server_cpu.csv ws_mb(작업 집합).
# 사용: python -I -X utf8 Tools/Benchmark/make_figures.py   (저장소 루트에서) → Docs/benchmark/*.png + 표(markdown)를 표준 출력
# 의존: matplotlib·numpy(pandas 없음). 한글 글꼴은 맑은 고딕.
import csv, os, re, statistics, sys

import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
import numpy as np

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
PERF = os.path.join(ROOT, 'Server', 'PerfData')
LAPTOP = os.path.join(ROOT, 'Tools', 'BotPack', 'results')
OUT = os.path.join(ROOT, 'Docs', 'benchmark')
# --demo(A7.4 시연 영상 카드, D-311): 그림 4·5만 시연용 표기(내부 항목 ID·날짜·괄호 설명 없음)로 Docs/demo/fig/에 그린다. 보고서 그림은 건드리지 않음
DEMO = '--demo' in sys.argv
if DEMO:
    OUT = os.path.join(ROOT, 'Docs', 'demo', 'fig')
os.makedirs(OUT, exist_ok=True)


def T(normal, demo):
    return demo if DEMO else normal


def save(fig, name):
    if DEMO and not name.startswith(('4_', '5_')):
        return
    fig.savefig(os.path.join(OUT, name))

plt.rcParams['font.family'] = 'Malgun Gothic'
plt.rcParams['axes.unicode_minus'] = False
plt.rcParams['figure.dpi'] = 110
plt.rcParams['savefig.dpi'] = 130
plt.rcParams['axes.grid'] = True
plt.rcParams['grid.alpha'] = 0.3
BEFORE, AFTER, BAL = '#c0504d', '#2e75b6', '#9a9a9a'   # P2 전(빨강) / P2 뒤·개선 후(파랑) / 균형 조정·참고(회색)


def read_csv(path):
    if not os.path.exists(path):
        return []
    with open(path, encoding='utf-8-sig', errors='replace') as f:
        return list(csv.DictReader(f))


def fnum(v):
    try:
        return float(v)
    except (TypeError, ValueError):
        return None


def runs_dir(setname):
    return os.path.join(PERF, setname, 'runs')


def server_part(setname, run, target):
    """A5.4c/d summarize.py의 server_part와 같은 규칙."""
    rows = [r for r in read_csv(os.path.join(runs_dir(setname), run, 'server_metrics.csv')) if fnum(r.get('players')) is not None and float(r['players']) >= target * 0.98]
    out = {'n': len(rows)}
    for key, col in [('late', 'tick_late_p99_ms'), ('body', 'tick_body_p99_ms'), ('wait', 'job_wait_p99_ms'), ('out', 'messages_out_ps'),
                     ('calls', 'send_calls_ps'), ('bufs', 'bufs_per_send'), ('in', 'packets_in_ps')]:
        out[key] = [float(r[col]) for r in rows if fnum(r.get(col)) is not None]
    cpu = read_csv(os.path.join(runs_dir(setname), run, 'server_cpu.csv'))
    out['cpu'], out['mem'] = [], []
    if rows and len(cpu) > 2:
        t0 = min(int(r['time']) for r in rows) * 1000 - 5000
        t1 = max(int(r['time']) for r in rows) * 1000
        pts = [(int(c['epoch_ms']), int(c['user_ms']) + int(c['kernel_ms']), fnum(c.get('ws_mb'))) for c in cpu if (c.get('epoch_ms') or '').isdigit()]
        out['cpu'] = [(b[1] - a[1]) / max(1, b[0] - a[0]) * 100 for a, b in zip(pts, pts[1:]) if t0 <= b[0] <= t1]
        out['mem'] = [p[2] for p in pts if t0 <= p[0] <= t1 and p[2] is not None]
    return out


def med(v):
    return statistics.median(v) if v else float('nan')


def mm(v, f='{:g}'):
    return '-' if not v else f'{f.format(statistics.median(v))}({f.format(max(v))})'


def summary(folder, name='loadtest.summary.csv'):
    s = read_csv(os.path.join(folder, name))
    return s[0] if s else None


# ---------------------------------------------------------------- A5.4c(P2 전) vs A5.4d(P2 뒤) 단계 램프
STAGES = [(1000, 's1000_1', 's1000_2'), (2500, 's2500_1', 's2500_2'), (5000, 's5000_1', 's5000_2')]
stage = {}   # (묶음 'c'|'d', 인원) -> [실행별 결과]
for target, r1, r2 in STAGES:
    for tag, setname, prefix in [('c', '2026-11-24_A5.4c', ''), ('d', '2026-11-24_A5.4d', 'd_')]:
        res = []
        for run in (prefix + r1, prefix + r2):
            sv = server_part(setname, run, target)
            sv['run'] = run
            sv['laptop'] = summary(os.path.join(LAPTOP, run))
            sv['pc'] = summary(os.path.join(runs_dir(setname), run), 'pc_loadtest.summary.csv')
            res.append(sv)
        stage[(tag, target)] = res

print('## 단계 램프 (A5.4c = P2 전, A5.4d = P2 뒤) — 5초 구간 값 중앙값(최대)\n')
hdr = ['실행', '구간수', '틱지연p99', '잡대기p99', '틱본문p99', '송신메시지/s', 'WSASend/s', '서버CPU%', '서버메모리MB', '노트북 핑p50/p99', 'PC 핑p50/p99', '접속 성공(노트북+PC)']
print('| ' + ' | '.join(hdr) + ' |')
print('|' + '---|' * len(hdr))
for target, _, _ in STAGES:
    for tag in ('c', 'd'):
        for sv in stage[(tag, target)]:
            lp, pc = sv['laptop'], sv['pc']
            ok = (int(lp['connect_ok']) if lp else 0) + (int(pc['connect_ok']) if pc else 0)
            wait = mm(sv['wait'])
            if sv['wait'] and max(sv['wait']) >= 127:
                wait = '≥127(칸 상한)'
            print('| ' + ' | '.join([sv['run'], str(sv['n']), mm(sv['late']), wait, mm(sv['body'], '{:.2f}'), mm(sv['out'], '{:.0f}'), mm(sv['calls'], '{:.0f}'),
                                      mm(sv['cpu'], '{:.0f}'), mm(sv['mem'], '{:.0f}'),
                                      f"{lp['ping_p50_ms']}/{lp['ping_p99_ms']}" if lp else '-', f"{pc['ping_p50_ms']}/{pc['ping_p99_ms']}" if pc else '-',
                                      f'{ok}/{target}']) + ' |')
print()

xs = np.arange(len(STAGES))
labels = [f'{t:,}명' for t, _, _ in STAGES]


def bar_pair(ax, key, agg=med, log=False, ylabel=''):
    w = 0.36
    for i, (tag, color, name) in enumerate([('c', BEFORE, 'P2 전 (A5.4c)'), ('d', AFTER, 'P2 뒤 (A5.4d)')]):
        vals = [[agg(sv[key]) for sv in stage[(tag, t)]] for t, _, _ in STAGES]
        mid = [np.mean(v) for v in vals]
        lo = [m - min(v) for m, v in zip(mid, vals)]
        hi = [max(v) - m for m, v in zip(mid, vals)]
        bars = ax.bar(xs + (i - 0.5) * w, mid, w, color=color, label=name, yerr=[lo, hi], capsize=3, ecolor='#444')
        for b, v in zip(bars, vals):
            txt = f'{min(v):,.0f}~{max(v):,.0f}' if round(min(v)) != round(max(v)) else f'{v[0]:,.0f}'
            ax.annotate(txt, (b.get_x() + b.get_width() / 2, b.get_height()), ha='center', va='bottom', fontsize=8, xytext=(0, 3), textcoords='offset points')
    ax.set_xticks(xs, labels)
    if log:
        ax.set_yscale('log')
    ax.set_ylabel(ylabel)
    ax.legend(loc='upper left')


# 그림 1: 인원별 틱 지연 p99
fig, ax = plt.subplots(figsize=(7.5, 4.4))
bar_pair(ax, 'late', log=True, ylabel='틱 지연 p99 (ms, 로그 축)')
ax.axhline(20, color='#333', ls='--', lw=1)
ax.annotate('합격선 20ms', (2.45, 20), fontsize=8, va='bottom', ha='right')
ax.set_title('동접 인원별 서버 틱 지연 p99 — 2회 실행의 범위')
fig.tight_layout()
save(fig, ('1_tick_late_by_players.png'))
plt.close(fig)

# 그림 2: 인원별 송신 메시지/s
fig, ax = plt.subplots(figsize=(7.5, 4.4))
bar_pair(ax, 'out', ylabel='서버 송신 메시지/s')
ax.yaxis.set_major_formatter(matplotlib.ticker.FuncFormatter(lambda v, _: f'{v / 10000:g}만'))
ax.set_title('동접 인원별 서버 송신 메시지 수 (Session::Send 호출 수)')
fig.tight_layout()
save(fig, ('2_messages_out_by_players.png'))
plt.close(fig)

# 그림 3: 인원별 서버 CPU·메모리
fig, (a1, a2) = plt.subplots(1, 2, figsize=(11, 4.2))
bar_pair(a1, 'cpu', ylabel='서버 CPU % (코어 1개 = 100%)')
a1.axhline(1200, color='#333', ls=':', lw=1)
a1.annotate('12 논리 코어 전부 = 1,200%', (2.45, 1200), fontsize=8, va='bottom', ha='right')
a1.set_ylim(0, 1350)
a1.set_title('서버 CPU (구간 중앙값)')
bar_pair(a2, 'mem', ylabel='서버 작업 집합 MB')
a2.set_title('서버 메모리 (구간 중앙값)')
fig.tight_layout()
save(fig, ('3_cpu_memory_by_players.png'))
plt.close(fig)


# 그림 4: 5,000명 시간축
def series(setname, run):
    rows = read_csv(os.path.join(runs_dir(setname), run, 'server_metrics.csv'))
    if not rows:
        return None
    t0 = int(rows[0]['time'])
    t = np.array([int(r['time']) - t0 for r in rows])
    return t, np.array([float(r['players']) for r in rows]), np.array([float(r['tick_late_p99_ms']) for r in rows]), np.array([float(r['job_wait_p99_ms']) for r in rows])


fig, (a1, a2) = plt.subplots(2, 1, figsize=(9, 6.6), sharex=True)
for setname, run, color, ls, name in [('2026-11-24_A5.4c', 's5000_1', BEFORE, '-', T('P2 전 1회', '묶기 전 1회')), ('2026-11-24_A5.4c', 's5000_2', BEFORE, '--', T('P2 전 2회', '묶기 전 2회')),
                                      ('2026-11-24_A5.4d', 'd_s5000_1', AFTER, '-', T('P2 뒤 1회', '묶기 후 1회')), ('2026-11-24_A5.4d', 'd_s5000_2', AFTER, '--', T('P2 뒤 2회', '묶기 후 2회'))]:
    s = series(setname, run)
    if s is None:
        continue
    t, players, late, wait = s
    a1.plot(t, np.maximum(late, 0.5), color=color, ls=ls, lw=1.4, label=name)
    a2.plot(t, np.maximum(wait, 0.5), color=color, ls=ls, lw=1.4, label=name)
    if run == 'd_s5000_1':
        ax2 = a1.twinx()
        ax2.fill_between(t, players, color='#bbbbbb', alpha=0.25, step='mid')
        ax2.set_ylabel(T('접속 인원 (회색, P2 뒤 1회)', '접속 인원 (회색)'))
        ax2.set_ylim(0, 5500)
        ax2.grid(False)
a1.set_yscale('log')
a1.axhline(20, color='#333', ls='--', lw=1)
a1.set_ylabel('틱 지연 p99 (ms, 로그)')
a1.annotate('합격선 20ms', (2, 20), fontsize=8, va='bottom')
a1.set_title(T('동접 5,000명 — 서버 시작부터의 시간축 (5초 구간)', '동접 5,000명 — 서버 시작부터의 시간축'))
a1.legend(loc='upper left', fontsize=8)
a2.set_yscale('log')
a2.axhline(127, color='#333', ls=':', lw=1)
a2.annotate('지표 칸 상한 127ms (이보다 큰 값도 127로 기록)', (2, 127), fontsize=8, va='bottom')
a2.set_ylabel('Room 잡 대기 p99 (ms, 로그)')
for ax in (a1, a2):
    ax.axvspan(143, 158, color='#f2c14e', alpha=0.25, lw=0)
a1.annotate('측정 끝: 봇이 접속을 끊는 구간\n(집계 제외 — 접속 인원 98% 미만)', (146, 40), fontsize=8, ha='right', va='bottom')
a2.annotate('0은 그림에서 0.5로 표시', (186, 0.6), fontsize=8, ha='right', va='bottom', color='#555')
a2.set_xlabel('서버 시작 뒤 경과 (초)')
fig.tight_layout()
save(fig, ('4_5000_timeline.png'))
plt.close(fig)


# 그림 5: P1b 전/후 핫존 100 (2026-11-19, 같은 시간대 A/B — analyze.py와 같은 정상 구간: 램프 끝 + 10초 뒤)
def p1b(run):
    folder = os.path.join(runs_dir('2026-11-19_P1b_after'), run)
    args = open(os.path.join(folder, 'args.txt'), encoding='utf-8').read().split()
    bots = int(args[args.index('--bots') + 1])
    ramp = int(args[args.index('--ramp-rate') + 1])
    t_ok = bots / float(ramp) + 10.0
    rows = [r for r in read_csv(os.path.join(folder, 'loadtest.csv')) if float(r['t_sec']) > t_ok + 0.01]
    ping = float(np.mean([float(r['ping_p99_ms']) for r in rows]))
    lines = [ln for ln in open(os.path.join(folder, 'server_console.txt'), encoding='utf-8', errors='replace') if '[metrics]' in ln and f' players={bots} ' in ln][2:]
    bufs = [float(m.group(1)) for ln in lines for m in [re.search(r'bufsPerSend=([\d.]+)', ln)] if m]
    late = [float(m.group(4)) for ln in lines for m in [re.search(r'tickLate\(avg/p50/p95/p99/max ms\)=([\d.]+)/([\d.]+)/([\d.]+)/([\d.]+)/([\d.]+)', ln)] if m]
    return ping, float(np.mean(bufs)) if bufs else float('nan'), med(late)


p1 = {k: [p1b(f'hot100_w6_{k}_r{i}') for i in (1, 2)] for k in ('pre', 'after')}
print('## P1b 핫존 100 (워커 6, 2026-11-19) — 핑 p99 = 정상 구간 5초 값의 평균, 호출당 메시지 = 평균, 틱 지연 p99 = 중앙값\n')
print('| 실행 | 핑 p99 | 호출당 메시지 | 틱 지연 p99 |\n|---|---|---|---|')
for k in ('pre', 'after'):
    for i, (p, b, l) in enumerate(p1[k], 1):
        print(f'| hot100_w6_{k}_r{i} | {p:.1f} | {b:.2f} | {l:g} |')
print()
fig, (a1, a2) = plt.subplots(1, 2, figsize=(9, 3.8))
for ax, idx, ylabel, title in [(a1, 0, '핑 p99 (ms)', '핫존 100명 핑 p99'), (a2, 1, 'WSASend 1회당 메시지 수', '송신 묶임 정도')]:
    for j, (k, color, name) in enumerate([('pre', BEFORE, '수정 전'), ('after', AFTER, T('송신 묶기 후 (P1b)', '수정 후'))]):
        v = [x[idx] for x in p1[k]]
        b = ax.bar(j, np.mean(v), 0.55, color=color, yerr=[[np.mean(v) - min(v)], [max(v) - np.mean(v)]], capsize=4, ecolor='#444')
        fmt = '{:.2f}' if idx == 1 else '{:.1f}'
        ax.annotate(fmt.format(min(v)) + ('' if round(min(v), 2) == round(max(v), 2) else '~' + fmt.format(max(v))), (j, np.mean(v)), ha='center', va='bottom', fontsize=9, xytext=(0, 4), textcoords='offset points')
    ax.set_xticks([0, 1], ['수정 전', T('P1b 후', '수정 후')])
    ax.set_ylabel(ylabel)
    ax.set_title(title)
a1.axhline(50, color='#333', ls='--', lw=1)
a1.annotate('합격선 50ms', (1.3, 50), fontsize=8, va='bottom', ha='right')
fig.suptitle(T('P1b 송신 묶기 (2026-11-19, 같은 PC·같은 시간대 2회씩)', '세션별 송신 묶기'), fontsize=11)
fig.tight_layout()
save(fig, ('5_p1b_hotzone100.png'))
plt.close(fig)


# 그림 6: 전원 계획 A/B (A5.4a, 이동 봇, TcpNoDelay 꺼짐 = 기본)
def a54a(run, target):
    """A5.4a summarize.py와 같은 규칙: players >= 봇 수인 구간, 사냥터 Room 틱 지연 p99(tick_late_p99_field_ms)의 중앙값."""
    rows = [r for r in read_csv(os.path.join(runs_dir('2026-11-24_A5.4a'), run, 'server_metrics.csv')) if float(r['players']) >= target]
    return med([float(r['tick_late_p99_field_ms']) for r in rows])


pw = {}
for plan in ('bal', 'hp'):
    for n in (250, 1000):
        runs = sorted(d for d in os.listdir(runs_dir('2026-11-24_A5.4a')) if d.startswith(f'{plan}_m{n}_ndfalse_'))
        pw[(plan, n)] = [a54a(r, n) for r in runs]
print('## 전원 계획 A/B (A5.4a, TcpNoDelay 꺼짐) — 틱 지연 p99 중앙값\n')
print('| 전원 계획 | 이동 250 | 이동 1,000 |\n|---|---|---|')
for plan, name in (('bal', '균형 조정'), ('hp', '고성능')):
    print(f"| {name} | {', '.join(f'{v:g}' for v in pw[(plan, 250)])} | {', '.join(f'{v:g}' for v in pw[(plan, 1000)])} |")
print()
fig, ax = plt.subplots(figsize=(6.5, 3.8))
w = 0.36
for i, (plan, color, name) in enumerate([('bal', BAL, '균형 조정 (Windows 기본)'), ('hp', AFTER, '고성능')]):
    for j, n in enumerate((250, 1000)):
        v = pw[(plan, n)]
        ax.bar(j + (i - 0.5) * w, np.mean(v), w, color=color, label=name if j == 0 else None,
               yerr=[[np.mean(v) - min(v)], [max(v) - np.mean(v)]] if len(v) > 1 else None, capsize=3, ecolor='#444')
        ax.annotate('~'.join(f'{x:g}' for x in sorted(set([min(v), max(v)]))) + ('' if len(v) > 1 else ' (1회)'),
                    (j + (i - 0.5) * w, np.mean(v)), ha='center', va='bottom', fontsize=8, xytext=(0, 3), textcoords='offset points')
ax.axhline(20, color='#333', ls='--', lw=1)
ax.annotate('합격선 20ms', (1.38, 20), fontsize=8, va='bottom', ha='right')
ax.set_ylim(0, 56)
ax.set_xticks([0, 1], ['이동 봇 250명', '이동 봇 1,000명'])
ax.set_ylabel('틱 지연 p99 (ms)')
ax.set_title('서버 PC 전원 계획에 따른 틱 지연 (A5.4a, 같은 exe)')
ax.legend()
fig.tight_layout()
save(fig, ('6_power_plan.png'))
plt.close(fig)

# 그림 7: 로그인 속도별 접속 시간 p99 (A5.4b, 계정 봇 idle, DB 연결 4)
rates = [50, 100, 200, 400, 800, 1600]
lg = []
for r in rates + ['5000']:
    s = summary(os.path.join(runs_dir('2026-11-24_A5.4b'), f'login_{r}'))
    lg.append((r, s))
print('## 로그인 처리량 (A5.4b) — 봇 접속 시간 = 첫 연결 → 방 입장\n')
print('| 요청 속도(명/초) | 봇 수 | 접속 시간 p50/p99/max | 실패 |\n|---|---|---|---|')
for r, s in lg:
    if s:
        print(f"| {r if r != '5000' else '한꺼번에 5,000'} | {s['bots']} | {s['enter_p50_ms']}/{s['enter_p99_ms']}/{s['enter_max_ms']} | {int(s['connect_fail']) + int(s['login_fail']) + int(s['login_give_up'])} |")
print()
fig, ax = plt.subplots(figsize=(7, 3.9))
xv = list(range(len(lg)))
p99 = [float(s['enter_p99_ms']) for _, s in lg]
p50 = [float(s['enter_p50_ms']) for _, s in lg]
ax.plot(xv, p99, 'o-', color=AFTER, label='p99')
ax.plot(xv, p50, 's--', color=BAL, label='p50')
for x, y in zip(xv, p99):
    ax.annotate(f'{y:,.0f}', (x, y), ha='center', va='bottom', fontsize=8, xytext=(0, 4), textcoords='offset points')
ax.set_yscale('log')
ax.set_xticks(xv, [str(r) if r != '5000' else '5,000\n한꺼번에' for r, _ in lg])
ax.set_xlabel('봇 접속 요청 속도 (명/초)')
ax.set_ylabel('접속 시간 (ms, 로그 축)')
ax.set_title('로그인 처리량 — 첫 연결부터 방 입장까지 (A5.4b, DB 연결 4)')
ax.legend()
fig.tight_layout()
save(fig, ('7_login_throughput.png'))
plt.close(fig)

print('figures written to', OUT, file=sys.stderr)
