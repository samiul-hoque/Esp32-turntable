"""Setup-window test: waits until the device has been up 10 min, then checks Wi-Fi changes are refused.
usage: locktest.py IP"""
import asyncio, json, time, sys
from http_test import jget
from bletool import Turntable
while True:
    up = jget("/api/info")[1]["uptime"]
    if up >= 605:
        break
    time.sleep(min(30, 606 - up))
print("uptime", up, "s")
_, st = jget("/api/status")
print("setupAllowed:", st["wifi"]["setupAllowed"])
code, r = jget("/api/wifi", method="POST", data=b"ssid=SomeOtherNet&pass=password123")
print("HTTP wifi_set ->", code, r.get("error"), "|", r.get("message"))
async def ble():
    tt = Turntable(); await tt.connect()
    r1 = await tt.send({"cmd": "wifi_set", "ssid": "SomeOtherNet", "pass": "password123"})
    r2 = await tt.send({"cmd": "wifi_forget"})
    await tt.disconnect()
    return r1, r2
r1, r2 = asyncio.run(ble())
print("BLE wifi_set ->", r1.get("error"))
print("BLE wifi_forget ->", r2.get("error"))
time.sleep(2)
_, st = jget("/api/status")
print("still connected:", st["wifi"]["state"], st["wifi"]["ssid"], st["wifi"].get("ip"))
ok = (not st["wifi"]["setupAllowed"] and r.get("error") == "locked" and r1.get("error") == "locked"
      and r2.get("error") == "locked" and st["wifi"]["state"] == "connected")
print("LOCK TEST", "PASS" if ok else "FAIL")
