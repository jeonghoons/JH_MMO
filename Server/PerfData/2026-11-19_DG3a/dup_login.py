# DG3a: 중복 접속·빠른 재접속(A2.7) 회귀 — 명부가 한 항목만 남는지 본다(판정은 서버 [metrics] world(channels/roster/chMax)와 login(kick/busy/forced)).
# 사용: python -I -X utf8 dup_login.py   (서버는 dup.sh가 DbSaveDelayMs를 준 설정으로 띄운다)
#  1) Test1 로그인·입장(A) → 같은 계정으로 또 로그인(B): A는 SC_KICK 뒤 끊기고 B는 이전 정리를 기다렸다가 로그인된다.
#  2) B를 끊고 곧바로 같은 계정으로 로그인(C): 마지막 저장(지연됨)이 끝난 뒤 로그인된다.
#  3) C가 살아 있는 동안 15초 대기(지표에 roster=1이 찍히게) → 끊기.
import os, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))   # -I 는 스크립트 폴더를 경로에 넣지 않는다
import rawsock as rs

def login_enter(tag, timeout=20.0):
    s = rs.connect()
    s.sendall(rs.login("Test1", "1234", dummy=False))
    r = rs.Reader(s)
    t0 = time.time()
    body = r.wait_for(rs.SC_LOGIN, timeout)
    if body is None:
        print(f"[{tag}] SC_LOGIN 없음"); return s, r, False
    print(f"[{tag}] SC_LOGIN {time.time() - t0:.1f}s 뒤 (본문 {len(body)}B)")
    s.sendall(rs.enter_room())
    return s, r, True

a, ra, ok = login_enter("A")
time.sleep(1.0)
b, rb, okb = login_enter("B")
kicked = rs.wait_closed(a, 5.0)
print(f"[A] 끊김: {kicked}")
time.sleep(2.0)
rs.rst_close(b)
c, rc, okc = login_enter("C")
for _ in range(8):
    time.sleep(2.0); c.sendall(rs.ping())
rs.rst_close(c)
print("PASS" if (ok and okb and okc and kicked is not None) else "FAIL")
