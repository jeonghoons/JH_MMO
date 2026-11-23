# A5.1 로그인 관문 시험(D-291): [Debug] AllowDummyLogin 값에 따라
#  1) is_dummy 요청, 2) dummy 계정 일반 로그인(id/pw dummy), 3) 일반 계정(Test1) 로그인의 결과를 본다.
# 사용: python login_gate.py <expect: off|on>
#   off(기본 설정) — 1·2는 LOGIN_RESULT_NO_ACCOUNT(2), 3은 SUCCESS(1)
#   on(부하 시험 설정) — 셋 다 SUCCESS
import sys
from rawsock import connect, login, Reader, SC_LOGIN

NAMES = {1: "SUCCESS", 2: "NO_ACCOUNT", 3: "WRONG_PASSWORD", 4: "ALREADY_LOGGED_IN", 5: "SERVER_ERROR", 6: "BUSY"}


def top_varint(body, field):
    """protobuf 본문 최상위에서 varint 필드 값을 찾는다(길이 필드는 건너뜀). 없으면 0."""
    i = 0
    while i < len(body):
        key = 0; shift = 0
        while True:
            b = body[i]; i += 1
            key |= (b & 0x7F) << shift; shift += 7
            if not b & 0x80: break
        f, wt = key >> 3, key & 7
        if wt == 0:
            v = 0; shift = 0
            while True:
                b = body[i]; i += 1
                v |= (b & 0x7F) << shift; shift += 7
                if not b & 0x80: break
            if f == field: return v
        elif wt == 2:
            n = 0; shift = 0
            while True:
                b = body[i]; i += 1
                n |= (b & 0x7F) << shift; shift += 7
                if not b & 0x80: break
            i += n
        else:
            raise ValueError("unexpected wire type %d" % wt)
    return 0


def try_login(account, pw, dummy):
    s = connect()
    try:
        s.sendall(login(account, pw, dummy))
        body = Reader(s).wait_for(SC_LOGIN, 5.0)
        return None if body is None else top_varint(body, 2)
    finally:
        s.close()


def main():
    expect = sys.argv[1] if len(sys.argv) > 1 else "off"
    want_dummy = 1 if expect == "on" else 2
    cases = [
        ("is_dummy 요청", "bot_gate", "1234", True, want_dummy),
        ("dummy 계정 일반 로그인", "dummy", "dummy", False, want_dummy),
        ("일반 계정 Test1", "Test1", "1234", False, 1),
    ]
    ok = True
    for name, acc, pw, dummy, want in cases:
        got = try_login(acc, pw, dummy)
        passed = got == want
        ok &= passed
        print("%-24s 결과=%s 기대=%s %s" % (name, NAMES.get(got, got), NAMES[want], "통과" if passed else "실패"))
    print("전체:", "통과" if ok else "실패")
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
