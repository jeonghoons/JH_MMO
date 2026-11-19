# 유휴 타임아웃 시험: 연결한 뒤 아무것도 보내지 않고 서버가 끊을 때까지 걸린 시간을 잰다.
#   --mode connect : 연결만 하고 침묵(로그인 전)
#   --mode login   : 로그인까지 하고 침묵(방에 들어가기 전)
#   --mode ingame  : 로그인·입장 뒤 침묵
#   --mode ping    : 로그인·입장 뒤 --ping-interval 초마다 CS_PING(하트비트) — 끊기지 않아야 한다
#   --account/--pw : 개별 계정(예: Test1 1234). 없으면 dummy 로그인
# 출력: 연결별 "closed after N s" 또는 "alive". 종료 코드 0 = 기대(--expect closed|alive)와 같음.
# 사용: python -I idle.py --mode ingame --count 5 --limit 40 --expect closed
import argparse, sys, time, os, threading
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rawsock as rs

ap = argparse.ArgumentParser()
ap.add_argument("--port", type=int, default=8888)
ap.add_argument("--mode", choices=["connect", "login", "ingame", "ping"], default="ingame")
ap.add_argument("--count", type=int, default=1)
ap.add_argument("--limit", type=float, default=40, help="이 시간(초)까지 지켜본다")
ap.add_argument("--ping-interval", type=float, default=5)
ap.add_argument("--account", default="")
ap.add_argument("--pw", default="1234")
ap.add_argument("--expect", choices=["closed", "alive"], default="closed")
a = ap.parse_args()

results = [None] * a.count

def one(i):
    s = rs.connect(port=a.port)
    r = rs.Reader(s)
    if a.mode != "connect":
        if a.account:
            s.sendall(rs.login(a.account, a.pw, dummy=False))
        else:
            s.sendall(rs.login("idle_%d" % i))
        if r.wait_for(rs.SC_LOGIN, 15) is None:
            results[i] = ("login-closed", 0.0); return
        if a.mode in ("ingame", "ping"):
            s.sendall(rs.enter_room())
    start = time.time()
    if a.mode == "ping":
        closed = None
        while time.time() - start < a.limit:
            try: s.sendall(rs.ping())
            except OSError: closed = time.time() - start; break
            got = rs.wait_closed(s, a.ping_interval)
            if got is not None: closed = time.time() - start; break
    else:
        closed = rs.wait_closed(s, a.limit)
    results[i] = ("closed", closed) if closed is not None else ("alive", a.limit)
    s.close()

threads = [threading.Thread(target=one, args=(i,)) for i in range(a.count)]
for t in threads: t.start()
for t in threads: t.join()

ok = True
for i, (state, sec) in enumerate(results):
    print("[idle %s #%d] %s after %.1f s" % (a.mode, i, state, sec))
    if state != a.expect: ok = False
print("[idle %s] %s (expect %s)" % (a.mode, "PASS" if ok else "FAIL", a.expect))
sys.exit(0 if ok else 1)
