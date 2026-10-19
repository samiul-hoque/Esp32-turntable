"""Watchdog test (dev build only, needs TT_TEST_HOOKS): freezes the network task during a
75 s move and checks the device restarts after the move and reports lastFault.
usage: wdtest.py IP"""
import time, sys
from http_test import req, jget
_, r = jget("/api/rotate?angle=360&time=75000&direction=forward")
print(time.strftime("%H:%M:%S"), "rotate 75 s:", r.get("ok"), r.get("run"), flush=True)
time.sleep(1)
t0 = time.time()
print(time.strftime("%H:%M:%S"), "stall:", req("/debug/stall", method="POST", timeout=5)[1], flush=True)
while time.time() - t0 < 160:
    try:
        code, info = jget("/api/info", timeout=3)
        if info["uptime"] < 60:
            print(time.strftime("%H:%M:%S"), f"back after {time.time()-t0:.0f} s: uptime {info['uptime']} s, "
                  f"reset {info['resetReason']}, lastFault: {info.get('lastFault')}", flush=True)
            break
    except Exception:
        pass
    time.sleep(2)
