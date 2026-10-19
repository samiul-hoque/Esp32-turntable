"""Provision Wi-Fi over BLE and print the state transitions.
usage: wifitest.py SSID PASSWORD SECONDS_TO_WATCH"""
import asyncio, json, sys, time
from bletool import Turntable
async def main(ssid, pw, wait):
    tt = Turntable(); await tt.connect()
    t0 = time.time()
    r = await tt.send({"cmd": "wifi_set", "ssid": ssid, "pass": pw})
    print("reply:", {k: v for k, v in r.items() if k != "_t"})
    end = time.time() + wait
    seen = 0
    while time.time() < end:
        for e in tt.events[seen:]:
            if e.get("type") == "wifi":
                w, t = e["wifi"], e["test"]
                print(f"+{e['_t']-t0:5.1f}s wifi={w['state']:<14} ssid={w.get('ssid')!s:<18} ip={w.get('ip','-'):<15} "
                      f"reason={w.get('reasonText','-')} | test={t['state']} {t.get('reasonText','')}{t.get('ip','')}")
        seen = len(tt.events)
        await asyncio.sleep(0.2)
    await tt.disconnect()
asyncio.run(main(sys.argv[1], sys.argv[2], float(sys.argv[3])))
