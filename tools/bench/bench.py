"""Wi-Fi throughput/latency benchmark, optionally with a subscribed BLE client.
usage: bench.py IP [--ble]"""
import asyncio, statistics, subprocess, sys, threading, time
from http_test import req, auth, OTA_PW

IP = sys.argv[1]


def ping_ms(n=10):
    out = subprocess.run(["ping", "-n", str(n), IP], capture_output=True, text=True).stdout
    vals = [int(x.split("time=")[1].split("ms")[0]) for x in out.splitlines() if "time=" in x]
    lost = n - len(vals)
    return (statistics.median(vals) if vals else None, max(vals) if vals else None, lost)


def upload_kbps(size=300_000):
    body = b"\x00" * size  # rejected (wrong magic) at the first chunk but fully drained
    t = time.time()
    code, msg, _ = req("/update", method="POST", data=body,
                       headers={"Content-Type": "application/octet-stream", **auth("admin", OTA_PW)}, timeout=120)
    dt = time.time() - t
    return size / 1024 / dt, dt, code, msg[:70]


def run(label):
    med, mx, lost = ping_ms()
    rates = [upload_kbps() for _ in range(2)]
    print(f"{label:<22} ping median {med} ms (max {mx}, lost {lost}/10) | upload " +
          ", ".join(f"{r[0]:.0f} KB/s ({r[1]:.1f}s, {r[2]})" for r in rates), flush=True)
    return rates


if "--ble" in sys.argv:
    from bletool import Turntable
    stop = threading.Event()
    ready = threading.Event()

    async def hold():
        tt = Turntable()
        await tt.connect()
        ready.set()
        while not stop.is_set():
            await asyncio.sleep(0.2)
        await tt.disconnect()

    th = threading.Thread(target=lambda: asyncio.run(hold()))
    th.start()
    ready.wait(30)
    run("BLE connected")
    stop.set()
    th.join(20)
else:
    r = run("no BLE")
    print("last response:", r[-1][3])
