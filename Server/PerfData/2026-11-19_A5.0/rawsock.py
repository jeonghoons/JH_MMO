# A5.0 원시 소켓 시험 공용 코드. 표준 라이브러리만 쓴다(protobuf는 필요한 필드만 손으로 인코딩).
# 패킷: 헤더 4바이트 {uint16 size(헤더 포함), uint16 id} 리틀 엔디언 + protobuf 본문 (Server/ServerCore/Network/Session.h)
import socket, struct, time

CS_LOGIN, CS_MOVE, CS_ENTER_ROOM, CS_PING = 1000, 1004, 1006, 1010
SC_LOGIN, SC_PONG, SC_KICK = 2000, 2012, 2027


def varint(n):
    out = bytearray()
    while True:
        b = n & 0x7F
        n >>= 7
        if n:
            out.append(b | 0x80)
        else:
            out.append(b)
            return bytes(out)


def f_str(field, s):
    data = s.encode("utf-8")
    return varint(field << 3 | 2) + varint(len(data)) + data


def f_int(field, v):
    return varint(field << 3 | 0) + varint(v)


def packet(pid, body=b""):
    return struct.pack("<HH", 4 + len(body), pid) + body


def login(account="bot_raw", pw="1234", dummy=True):
    body = f_str(1, account) + f_str(2, pw)
    if dummy:
        body += f_int(3, 1)
    return packet(CS_LOGIN, body)


def enter_room():
    return packet(CS_ENTER_ROOM, f_int(1, 1))


def ping():
    return packet(CS_PING, f_int(1, int(time.time() * 1000)))


def connect(host="127.0.0.1", port=8888, rcvbuf=None, timeout=5.0):
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    if rcvbuf is not None:
        s.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, rcvbuf)  # 연결 전에 줄여야 수신 창이 작게 잡힌다
    s.settimeout(timeout)
    s.connect((host, port))
    return s


def rst_close(s):
    # SO_LINGER(on, 0초) + close = RST로 끊는다(FIN 없이)
    s.setsockopt(socket.SOL_SOCKET, socket.SO_LINGER, struct.pack("hh", 1, 0))
    s.close()


class Reader:
    """수신 바이트를 패킷으로 나눈다."""

    def __init__(self, sock):
        self.sock = sock
        self.buf = b""

    def read_packet(self, timeout=5.0):
        """(id, body)를 돌려준다. 서버가 끊었으면 None, 시간 안에 없으면 TimeoutError."""
        self.sock.settimeout(timeout)
        while True:
            if len(self.buf) >= 4:
                size, pid = struct.unpack("<HH", self.buf[:4])
                if len(self.buf) >= size:
                    body = self.buf[4:size]
                    self.buf = self.buf[size:]
                    return pid, body
            try:
                data = self.sock.recv(65536)
            except socket.timeout:
                raise TimeoutError()
            except OSError:
                return None
            if not data:
                return None
            self.buf += data

    def wait_for(self, want, timeout=5.0):
        """want id의 패킷이 올 때까지 읽는다. 끊기면 None."""
        end = time.time() + timeout
        while True:
            left = end - time.time()
            if left <= 0:
                raise TimeoutError()
            got = self.read_packet(left)
            if got is None:
                return None
            if got[0] == want:
                return got[1]


def wait_closed(sock, limit):
    """서버가 연결을 끊을 때까지 기다린다(받은 것은 버림). 끊기면 걸린 초, limit 안에 안 끊기면 None."""
    start = time.time()
    while True:
        left = limit - (time.time() - start)
        if left <= 0:
            return None
        sock.settimeout(left)
        try:
            data = sock.recv(65536)
        except socket.timeout:
            return None
        except OSError:
            return time.time() - start
        if not data:
            return time.time() - start
