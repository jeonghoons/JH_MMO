# F4 재현(감사 2026-11-06): 로그인·입장한 뒤 수신을 멈춘 클라이언트.
# 수신 버퍼를 작게 잡고 읽지 않으면 서버의 WSASend가 완료되지 않은 채 걸려 있고 송신 큐가 쌓인다.
#   --break-after S : S초 뒤 깨진 패킷(헤더 size = 2)을 보내 서버 쪽 Disconnect를 유도한다(수정 전 코드의 재현 절차).
#                     0이면 보내지 않는다(송신 큐 상한이 스스로 끊는지 볼 때).
#   --ping          : 멈춘 동안에도 1초마다 CS_PING을 보낸다(유휴 타임아웃에 걸리지 않는 느린 수신자).
#   --hold S        : 그 뒤 소켓을 닫지 않고 S초 더 붙잡고 있다가 끝낸다(읽지 않으므로 서버가 끊었는지는 서버 지표로 본다).
# 사용: python -I stall_recv.py [--count N] [--break-after 20] [--ping] [--hold 60]
import argparse, struct, sys, time, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rawsock as rs

ap = argparse.ArgumentParser()
ap.add_argument("--port", type=int, default=8888)
ap.add_argument("--count", type=int, default=1)
ap.add_argument("--break-after", type=float, default=20)
ap.add_argument("--ping", action="store_true")
ap.add_argument("--hold", type=float, default=60)
a = ap.parse_args()

t0 = time.time()
def log(msg): print("[stall %6.1fs] %s" % (time.time() - t0, msg), flush=True)

socks = []
for i in range(a.count):
    s = rs.connect(port=a.port, rcvbuf=1024)
    r = rs.Reader(s)
    s.sendall(rs.login("stall_%d" % i))
    if r.wait_for(rs.SC_LOGIN, 10) is None:
        log("로그인 응답 전에 끊김"); sys.exit(1)
    s.sendall(rs.enter_room())
    socks.append(s)
log("%d개 연결 로그인·입장, 이제 읽지 않음" % len(socks))

def sleep_with_ping(sec):
    end = time.time() + sec
    while time.time() < end:
        if a.ping:
            for s in socks:
                try: s.sendall(rs.ping())
                except OSError: pass
        time.sleep(min(1.0, max(0.0, end - time.time())))

if a.break_after > 0:
    sleep_with_ping(a.break_after)
    for s in socks:
        try: s.sendall(struct.pack("<HH", 2, rs.CS_MOVE))   # size가 헤더보다 작음 → Invalid Packet Size
        except OSError as e: log("깨진 패킷 전송 실패: %s" % e)
    log("깨진 패킷 전송")
sleep_with_ping(a.hold)
log("끝(소켓 닫음)")
for s in socks:
    s.close()
