# 로그인 + 입장 연속 반복(감사 F2·F3 재현 절차): [CS_LOGIN][CS_ENTER_ROOM]을 한 send()에 담아 보내고 끊기를 되풀이한다.
#   --close rst    : 보낸 직후 RST로 끊는다(응답을 기다리지 않음) — 로그인 결과와 Disconnect가 겹치는 창
#   --close after  : SC_LOGIN을 받은 뒤 RST로 끊는다(입장 처리와 Disconnect가 겹치는 창)
#   --close entered: SC_LOGIN을 받은 뒤 CS_ENTER_ROOM을 다시 보내고(로그인 완료 전에 도착한 입장 요청은 서버가 무시한다) 방에서 오는
#                     패킷을 하나 받은 다음 RST로 끊는다 — 방 안에서 끊기는 경로(퇴장·저장·접속 목록 정리)
#   --close mixed  : 위 셋을 번갈아
#   --accounts A,B : 개별 계정을 돌아가며 쓴다(중복 접속 차단 A2.7 경로 포함, 비밀번호 --pw). 없으면 dummy 로그인
#   --errors       : 오류 입력을 섞는다(헤더만 보내고 끊기, size 0, size 8193, 본문이 깨진 로그인, 로그인 두 번)
# 끝난 뒤 잔류는 서버 [metrics]의 sessions·players·sessionObjs로 판정한다.
# 사용: python -I login_loop.py --count 500 --close mixed
import argparse, struct, sys, time, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rawsock as rs

ap = argparse.ArgumentParser()
ap.add_argument("--port", type=int, default=8888)
ap.add_argument("--count", type=int, default=500)
ap.add_argument("--close", choices=["rst", "after", "entered", "mixed"], default="mixed")
ap.add_argument("--accounts", default="")
ap.add_argument("--pw", default="1234")
ap.add_argument("--errors", action="store_true")
a = ap.parse_args()
accounts = [x for x in a.accounts.split(",") if x]

ERRORS = [
    ("header-only", lambda: struct.pack("<HH", 30, rs.CS_LOGIN)),                       # 본문 없이 끊김
    ("size-0", lambda: struct.pack("<HH", 0, rs.CS_LOGIN)),                             # Invalid Packet Size
    ("size-8193", lambda: struct.pack("<HH", 8193, rs.CS_LOGIN)),                       # Invalid Packet Size
    ("bad-body", lambda: rs.packet(rs.CS_LOGIN, b"\xff\xff\xff\xff\xff\xff\xff")),      # 파싱 실패
    ("bad-then-enter", lambda: rs.packet(rs.CS_MOVE, b"\xff\xff\xff") + rs.enter_room()),  # F1: 깨진 패킷 뒤 입장
    ("login-twice", lambda: rs.login("twice") + rs.login("twice") + rs.enter_room()),
    ("enter-only", lambda: rs.enter_room()),                                            # 로그인 없이 입장
    ("unknown-id", lambda: rs.packet(999, b"")),
]

t0 = time.time()
fail = 0
entered = 0
for i in range(a.count):
    try:
        s = rs.connect(port=a.port)
        if a.errors:
            name, make = ERRORS[i % len(ERRORS)]
            s.sendall(make())
            if i % 2: time.sleep(0.02)
            rs.rst_close(s)
            continue
        if accounts:
            data = rs.login(accounts[i % len(accounts)], a.pw, dummy=False)
        else:
            data = rs.login("loop_%d" % i)
        s.sendall(data + rs.enter_room())
        # mixed: 계정 수(4)와 서로소인 3으로 돌려 계정마다 세 방식이 모두 걸리게 한다
        mode = a.close if a.close != "mixed" else ("rst", "after", "entered")[i % 3]
        if mode != "rst":
            try:
                r = rs.Reader(s)
                if r.wait_for(rs.SC_LOGIN, 15) is not None and mode == "entered":
                    s.sendall(rs.enter_room())
                    if r.read_packet(3) is not None:
                        entered += 1
            except TimeoutError: fail += 1
        rs.rst_close(s)
    except OSError as e:
        fail += 1
        print("[loop] #%d 실패: %s" % (i, e), flush=True)
print("[loop] %d회(입장 뒤 끊기 %d), 연결·응답 실패 %d, %.1f초" % (a.count, entered, fail, time.time() - t0))
