"""HTTP + OTA test suite.
usage: http_test.py IP [--ota firmware.bin] [--no-ble] [--skip-basic]
  --ota      also test bad/truncated/abandoned uploads and a real update with this image
  --no-ble   skip the step that needs a BLE adapter
OTA password: env TT_OTA_PASSWORD (default: the firmware default)."""
import os, asyncio, base64, json, sys, threading, time, urllib.error, urllib.request
from bletool import Turntable

IP = sys.argv[1]
BASE = f"http://{IP}"
OTA_PW = os.environ.get("TT_OTA_PASSWORD", "auleek-update")
results = []


def check(name, ok, detail=""):
    results.append((name, bool(ok)))
    print(("PASS " if ok else "FAIL ") + name + (f"  [{detail}]" if detail else ""), flush=True)


def req(path, method="GET", data=None, headers=None, timeout=10, base=BASE):
    r = urllib.request.Request(base + path, data=data, method=method, headers=headers or {})
    try:
        with urllib.request.urlopen(r, timeout=timeout) as resp:
            return resp.status, resp.read().decode("utf-8", "replace"), dict(resp.headers)
    except urllib.error.HTTPError as e:
        return e.code, e.read().decode("utf-8", "replace"), dict(e.headers)


def jget(path, **kw):
    code, body, _ = req(path, **kw)
    return code, json.loads(body)


def wait_idle(timeout=30):
    end = time.time() + timeout
    while time.time() < end:
        _, s = jget("/api/status")
        if s["motion"]["state"] not in ("moving", "delay"):
            return s
        time.sleep(0.3)


def auth(user, pw):
    return {"Authorization": "Basic " + base64.b64encode(f"{user}:{pw}".encode()).decode()}


def rawbody(payload):
    return payload, {"Content-Type": "application/octet-stream"}


def multipart(filename, payload):
    boundary = "----tt" + str(int(time.time()))
    body = (f"--{boundary}\r\nContent-Disposition: form-data; name=\"firmware\"; filename=\"{filename}\"\r\n"
            f"Content-Type: application/octet-stream\r\n\r\n").encode() + payload + f"\r\n--{boundary}--\r\n".encode()
    return body, {"Content-Type": f"multipart/form-data; boundary={boundary}"}


def main():
    only_after = "--skip-basic" in sys.argv
    if only_after:
        return after()
    code, html, _ = req("/")
    check("GET / control page", code == 200 and "Auleek Turntable Control" in html and "{{" not in html,
          f"{len(html)} bytes")
    check("page has new defaults + summary", 'value="90"' in html and 'value="2000"' in html and "moveSummary" in html)

    code, info = jget("/api/info")
    check("GET /api/info", code == 200 and info["fw"] and info["wifi"]["ip"] == IP,
          f"fw {info['fw']}, uptime {info['uptime']} s, heap {info['freeHeap']}")
    up0, t0 = info["uptime"], time.time()

    code, st = jget("/api/status")
    check("GET /api/status", code == 200 and "motion" in st and st["wifi"]["ssid"])

    # mDNS
    try:
        code, body, _ = req("/api/info", base="http://turntable.local", timeout=8)
        check("mDNS http://turntable.local", code == 200)
    except Exception as e:
        check("mDNS http://turntable.local", False, str(e))

    # ---- legacy /rotate (v1 contract)
    code, body, _ = req("/rotate?angle=90")
    check("legacy /rotate missing params -> 400 'Missing parameters.'", code == 400 and body == "Missing parameters.")
    t = time.time()
    code, body, _ = req("/rotate?repetitions=3&angle=90&time=2000&repDelay=500&direction=reverse&acceleration=&deceleration=",
                        timeout=30)
    dt = time.time() - t
    check("legacy /rotate README scenario blocks until done", code == 200 and "Sequence Complete!" in body and 7 < dt < 12,
          f"{dt:.1f} s")
    check("legacy report matches v1 text", all(x in body for x in [
        "• 3 repetitions<br>", "• 90° per rep (Reverse)<br>", "• Total angle: 270.0°", "• Movement time per rep: 2000ms",
        "• Delay between reps: 500ms", "• Target speed: 7.50 RPM", "• Acceleration: 10.0 RPM/s", "• Steps moved: 9600",
        "• Actual angle: 270.0°"]))
    code, body2, _ = req("/rotate?angle=0&time=1000&direction=forward")
    check("legacy /rotate angle 0 returns previous report (v1)", code == 200 and body2 == body)

    # ---- JSON API
    code, r = jget("/api/rotate?angle=360&time=8000&direction=forward")
    check("/api/rotate returns immediately", code == 200 and r.get("ok"), str(r))
    code, r2 = jget("/api/rotate?angle=10&time=1000")
    check("/api/rotate while moving -> busy", r2.get("error") == "busy")
    code, body, _ = req("/rotate?angle=10&time=1000&direction=forward")
    check("legacy /rotate while moving -> 409", code == 409, body)
    time.sleep(1.5)
    _, s = jget("/api/status")
    check("status shows progress", s["motion"]["state"] == "moving" and s["motion"]["elapsed"] > 1000,
          f"elapsed {s['motion']['elapsed']} ms, steps {s['motion']['steps']}")
    code, _ = jget("/api/stop", method="POST")
    s = wait_idle(10)
    code, rep, _ = req("/api/report")
    check("/api/stop -> stopped + report", s["motion"]["state"] == "stopped" and "Sequence Stopped" in rep,
          f"{s['motion']['steps']} of 12800 steps")
    code, r = jget("/api/rotate?angle=90")
    check("/api/rotate missing time -> 400", code == 400 and r["error"] == "invalid")

    after(up0, t0)


def ble_stop_test():
    # ---- BLE stop during a blocking legacy /rotate (net task busy waiting)
    holder = {}

    def legacy():
        t = time.time()
        holder["resp"] = req("/rotate?angle=360&time=10000&direction=forward", timeout=40)
        holder["dt"] = time.time() - t

    th = threading.Thread(target=legacy)
    th.start()
    time.sleep(2)

    async def ble_stop():
        tt = Turntable()
        await tt.connect()
        r = await tt.send({"cmd": "stop"})
        await tt.disconnect()
        return r

    try:
        r = asyncio.run(ble_stop())
    except Exception as e:
        th.join(40)
        print("SKIP BLE stop during legacy /rotate  [" + type(e).__name__ + ": close the app tab]")
        return
    th.join(40)
    code, body, _ = holder.get("resp", (0, "", {}))
    check("BLE stop interrupts a blocking legacy /rotate", r.get("ok") and code == 200 and "Sequence Stopped" in body,
          f"legacy returned after {holder.get('dt', 0):.1f} s (10 s requested)")


def after(up0=None, t0=None):
    if up0 is None:
        _, info = jget("/api/info")
        up0, t0 = info["uptime"], time.time()
    if "--no-ble" not in sys.argv:
        ble_stop_test()
    # ---- scan + wifi validation
    code, r = jget("/api/scan", method="POST")
    time.sleep(6)
    code, sc = jget("/api/scan")
    check("scan via HTTP", code == 200 and not sc["inProgress"] and len(sc["networks"]) > 0, f"{len(sc['networks'])} networks")
    code, r = jget("/api/wifi", method="POST", data=b"ssid=&pass=")
    check("/api/wifi empty ssid -> invalid", r.get("error") == "invalid")

    # ---- captive portal (only redirects while the hotspot is up)
    code, body, hdr = req("/generate_204", headers={"Host": "connectivitycheck.gstatic.com"})
    check("unknown URL with hotspot off -> 404", code == 404)

    # ---- OTA
    code, _, hdr = req("/update")
    check("/update requires auth", code == 401 and "WWW-Authenticate" in hdr)
    code, _, _ = req("/update", headers=auth("admin", "wrong"))
    check("/update rejects wrong password", code == 401)
    code, page, _ = req("/update", headers=auth("admin", OTA_PW))
    check("/update page with password", code == 200 and "Firmware update" in page)

    # multipart (the hang-prone parser) must be refused fast, device must stay responsive
    body, h = multipart("junk.bin", b"\x00" * 300000)
    t = time.time()
    try:
        code, msg, _ = req("/update", method="POST", data=body, headers={**h, **auth("admin", OTA_PW)}, timeout=30)
        outcome = f"HTTP {code}"
    except Exception as e:
        outcome = type(e).__name__
    dt = time.time() - t
    code2, _ = jget("/api/status")
    check("multipart upload refused quickly, device responsive", dt < 10 and code2 == 200, f"{outcome} after {dt:.1f} s")

    body, h = rawbody(b"\x00" * 50000)
    code, msg, _ = req("/update", method="POST", data=body, headers={**h, **auth("admin", OTA_PW)}, timeout=60)
    check("OTA rejects a non-firmware file", code == 500 and "still installed" in msg, msg[:90])

    if "--ota" in sys.argv:
        good = open(sys.argv[sys.argv.index("--ota") + 1], "rb").read()
        body, h = rawbody(good[: len(good) // 2])
        code, msg, _ = req("/update", method="POST", data=body, headers={**h, **auth("admin", OTA_PW)}, timeout=120)
        check("OTA rejects a truncated image", code == 500, msg[:120])

        import socket
        CRLF = "\r\n"
        hdr = (f"POST /update HTTP/1.1{CRLF}Host: {IP}{CRLF}Content-Type: application/octet-stream{CRLF}"
               f"Content-Length: {len(good)}{CRLF}Authorization: {auth('admin', OTA_PW)['Authorization']}{CRLF}{CRLF}").encode()
        sk = socket.create_connection((IP, 80), timeout=10)
        sk.sendall(hdr + good[:200000])
        time.sleep(1)
        sk.close()  # abandon mid-upload
        t = time.time()
        ok = False
        while time.time() - t < 20:
            try:
                if jget("/api/status", timeout=3)[0] == 200:
                    ok = True
                    break
            except Exception:
                pass
            time.sleep(1)
        check("abandoned upload: device recovers by itself", ok, f"responsive again after {time.time() - t:.1f} s")
        _, s2 = jget("/api/rotate?angle=10&time=500")
        check("motion not left locked after abandoned upload", s2.get("ok"), str(s2.get("error")))
        wait_idle(10)

        jget("/api/rotate?angle=360&time=20000")
        time.sleep(0.5)
        body, h = rawbody(good)
        code, msg, _ = req("/update", method="POST", data=body, headers={**h, **auth("admin", OTA_PW)}, timeout=120)
        check("OTA refused while moving", code == 500 and "moving" in msg, msg[:100])
        jget("/api/stop", method="POST")
        wait_idle(15)

        _, info = jget("/api/info")
        check("still running after rejected uploads (no reboot)", info["uptime"] >= up0 + (time.time() - t0) - 3,
              f"uptime {info['uptime']} s")

        t = time.time()
        code, msg, _ = req("/update", method="POST", data=body, headers={**h, **auth("admin", OTA_PW)}, timeout=180)
        check("OTA good image accepted", code == 200 and "Update OK" in msg, f"upload {time.time() - t:.1f} s")
        back = None
        for _ in range(60):
            time.sleep(1)
            try:
                _, back = jget("/api/info", timeout=3)
                if back["uptime"] < 60:
                    break
            except Exception:
                pass
        check("device back online after OTA", back is not None and back["uptime"] < 60,
              f"fw {back and back['fw']}, uptime {back and back['uptime']} s, reset {back and back['resetReason']}")

    failed = [n for n, ok in results if not ok]
    print(f"\n{len(results) - len(failed)}/{len(results)} passed")


if __name__ == "__main__":
    main()
