# DG3c-2 회귀: PIE의 Test1이 던전 안에 있을 때 같은 계정으로 로그인 → PIE 쪽이 킥되어 로그인 화면으로 돌아가는지
import sys, time, os
D = r"C:\Users\kjhkjh\KJH\JH_MMO\Server\PerfData\2026-11-20_DG3c"
sys.path.insert(0, D)
import dungeon_test as dt
c = dt.Client("Test1")
dt.pump_all([c], 4.0)
print("logged in id", c.id, "level", c.level, "kind", c.kind, "closed", c.closed)
c.close()
