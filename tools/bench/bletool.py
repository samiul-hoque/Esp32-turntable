"""BLE client for the turntable firmware, mirroring what app/app.js does.

usage:
  bletool.py info                 print info reply
  bletool.py cmd '<json>' [secs]  send a command, print reply and events for secs
  bletool.py suite                run the automated test suite
"""
import asyncio, json, sys, time
from bleak import BleakClient, BleakScanner

SERVICE = "6e7a0001-9b3c-4f1e-8a2d-5c1f0e7b2a10"
COMMAND = "6e7a0002-9b3c-4f1e-8a2d-5c1f0e7b2a10"
STATE = "6e7a0003-9b3c-4f1e-8a2d-5c1f0e7b2a10"
EVENTS = "6e7a0004-9b3c-4f1e-8a2d-5c1f0e7b2a10"


class Turntable:
    def __init__(self):
        self.client = None
        self.buf = ""
        self.events = []          # all parsed events (dicts), in order
        self.bad_lines = 0
        self.chunks = 0
        self.max_chunk = 0
        self.next_id = 1
        self.waiters = {}
        self.device = None
        self.adv = None

    async def find(self, timeout=15):
        found = {}

        def cb(dev, adv):
            if SERVICE in [u.lower() for u in adv.service_uuids]:
                found[dev.address] = (dev, adv)

        async with BleakScanner(cb):
            end = time.time() + timeout
            while time.time() < end and not found:
                await asyncio.sleep(0.3)
            await asyncio.sleep(1.5)  # let the scan response (name) arrive
        if not found:
            raise RuntimeError("turntable not found")
        self.device, self.adv = next(iter(found.values()))
        return self.device, self.adv

    async def connect(self):
        if not self.device:
            await self.find()
        self.client = BleakClient(self.device, winrt={"use_cached_services": False})
        await self.client.connect()
        await self.client.start_notify(EVENTS, self._on_chunk)

    async def disconnect(self):
        if self.client and self.client.is_connected:
            await self.client.disconnect()

    def _on_chunk(self, _, data: bytearray):
        self.chunks += 1
        self.max_chunk = max(self.max_chunk, len(data))
        self.buf += data.decode("utf-8", "replace")
        while "\n" in self.buf:
            line, self.buf = self.buf.split("\n", 1)
            if not line:
                continue
            try:
                msg = json.loads(line)
            except json.JSONDecodeError:
                self.bad_lines += 1
                continue
            msg["_t"] = time.time()
            self.events.append(msg)
            if msg.get("type") == "reply" and msg.get("id") in self.waiters:
                self.waiters.pop(msg["id"]).set_result(msg)

    async def send(self, cmd: dict, timeout=6.0):
        cid = self.next_id
        self.next_id += 1
        fut = asyncio.get_running_loop().create_future()
        self.waiters[cid] = fut
        payload = json.dumps({**cmd, "id": cid}).encode()
        await self.client.write_gatt_char(COMMAND, payload, response=True)
        return await asyncio.wait_for(fut, timeout)

    async def write_raw(self, payload: bytes):
        await self.client.write_gatt_char(COMMAND, payload, response=True)

    async def state(self):
        raw = await self.client.read_gatt_char(STATE)
        return json.loads(raw.decode())

    async def wait_event(self, pred, timeout, since=None):
        since = since if since is not None else 0
        end = time.time() + timeout
        while time.time() < end:
            for e in self.events[since:]:
                if pred(e):
                    return e
            await asyncio.sleep(0.05)
        return None


# ---------------------------------------------------------------- suite

results = []


def check(name, ok, detail=""):
    results.append((name, bool(ok), detail))
    print(("PASS " if ok else "FAIL ") + name + (f"  [{detail}]" if detail else ""), flush=True)


async def run_rotate(tt, cmd, timeout):
    mark = len(tt.events)
    reply = await tt.send({"cmd": "rotate", **cmd})
    if not reply.get("ok"):
        return reply, None
    run = reply["run"]
    done = await tt.wait_event(lambda e: e.get("type") == "motion" and e.get("run") == run
                               and e.get("state") in ("done", "stopped"), timeout, mark)
    return reply, done


async def suite():
    tt = Turntable()
    t0 = time.time()
    dev, adv = await tt.find()
    check("advertises service UUID", True, dev.address)
    check("name in scan response", (adv.local_name or "").startswith("Auleek-Turntable-"), adv.local_name)

    await tt.connect()
    check("connect", tt.client.is_connected, f"MTU {tt.client.mtu_size}")

    st = await tt.state()
    check("state characteristic readable JSON", "motion" in st and "wifi" in st)
    check("protocol version 1", st.get("proto") == 1)

    r = await tt.send({"cmd": "info"})
    info = r.get("info", {})
    check("info reply", r.get("ok") and info.get("fw") and info.get("limits"), f"fw {info.get('fw')}, reset {info.get('resetReason')}, heap {info.get('freeHeap')}")
    check("info chunked + reassembled", tt.bad_lines == 0, f"{tt.chunks} chunks, max {tt.max_chunk} B")
    heap_start = info.get("freeHeap", 0)

    # --- motion
    reply, done = await run_rotate(tt, {"angle": 90, "time": 1000, "direction": "forward"}, 8)
    check("rotate 90deg/1000ms accepted", reply.get("ok"), str(reply))
    check("rotate completes", done and done["state"] == "done", f"elapsed {done and done.get('elapsed')} ms")
    if done:
        check("v1 parity: 90deg/1000ms is cut off at 2x time (accel-limited)", done.get("steps") < 3200 and done.get("elapsed") <= 2100, f"steps {done.get('steps')}, angle {done.get('actualAngle')}")
        check("speed 15 RPM", abs(float(done.get("speedRpm")) - 15.0) < 0.01, str(done.get("speedRpm")))

    # README scenario
    reply, done = await run_rotate(tt, {"angle": 90, "time": 2000, "direction": "reverse", "repetitions": 3, "repDelay": 500}, 20)
    check("README scenario completes", done and done["state"] == "done" and done.get("rep") == 3,
          f"steps {done and done.get('steps')}, elapsed {done and done.get('elapsed')} ms")
    if done:
        check("README scenario: 9600 steps, 270 deg", done.get("steps") == 9600 and float(done["actualAngle"]) == 270.0)
        check("README scenario: no warnings", "warnings" not in done)

    # progress events during a move
    mark = len(tt.events)
    reply = await tt.send({"cmd": "rotate", "angle": 90, "time": 3000})
    await asyncio.sleep(1.2)
    busy = await tt.send({"cmd": "rotate", "angle": 10, "time": 1000})
    check("second rotate while moving -> busy", busy.get("error") == "busy", busy.get("error"))
    done = await tt.wait_event(lambda e: e.get("type") == "motion" and e.get("run") == reply.get("run") and e.get("state") == "done", 10, mark)
    progress = [e for e in tt.events[mark:] if e.get("type") == "motion" and e.get("state") == "moving"]
    check("progress events while moving", len(progress) >= 8, f"{len(progress)} events in ~3 s")

    # stop mid-move
    mark = len(tt.events)
    reply = await tt.send({"cmd": "rotate", "angle": 360, "time": 6000})
    await asyncio.sleep(2.0)
    t_stop = time.time()
    s = await tt.send({"cmd": "stop"})
    stopped = await tt.wait_event(lambda e: e.get("type") == "motion" and e.get("run") == reply.get("run") and e.get("state") == "stopped", 10, mark)
    check("stop mid-move", s.get("ok") and stopped is not None,
          f"stopped after {stopped and round(stopped['_t'] - t_stop, 2)} s, {stopped and stopped.get('steps')} of 12800 steps")

    # stop during repetition delay
    mark = len(tt.events)
    reply = await tt.send({"cmd": "rotate", "angle": 10, "time": 300, "repetitions": 3, "repDelay": 4000})
    delay = await tt.wait_event(lambda e: e.get("type") == "motion" and e.get("run") == reply.get("run") and e.get("state") == "delay", 5, mark)
    t_stop = time.time()
    await tt.send({"cmd": "stop"})
    stopped = await tt.wait_event(lambda e: e.get("type") == "motion" and e.get("run") == reply.get("run") and e.get("state") == "stopped", 5, mark)
    check("stop during rep delay ends immediately", delay and stopped and stopped["_t"] - t_stop < 1.0,
          f"{stopped and round(stopped['_t'] - t_stop, 2)} s, rep {stopped and stopped.get('rep')}")

    # speed clamp (v1 behaviour: warning + clamp + 2x time safety cut-off)
    reply, done = await run_rotate(tt, {"angle": 360, "time": 100}, 5)
    check("over-speed request clamped with v1 warning", done and "Required speed (600.0 RPM)" in done.get("warnings", ""),
          f"speed {done and done.get('speedRpm')}, steps {done and done.get('steps')}")

    # validation
    r = await tt.send({"cmd": "rotate", "angle": 90})
    check("rotate without time -> invalid", r.get("error") == "invalid", r.get("message"))
    r = await tt.send({"cmd": "rotate", "angle": 90, "time": 0})
    check("rotate time 0 -> invalid", r.get("error") == "invalid", r.get("message"))
    r = await tt.send({"cmd": "rotate", "angle": 0, "time": 1000})
    check("rotate angle 0 -> no_movement", r.get("error") == "no_movement")
    r = await tt.send({"cmd": "frobnicate"})
    check("unknown command", r.get("error") == "unknown_command")
    mark = len(tt.events)
    await tt.write_raw(b"{not json")
    bad = await tt.wait_event(lambda e: e.get("type") == "reply" and e.get("error") == "bad_json", 3, mark)
    check("malformed JSON -> bad_json reply", bad is not None)
    r = await tt.send({"cmd": "wifi_set", "ssid": "", "pass": "x"})
    check("wifi_set empty ssid -> invalid", r.get("error") == "invalid", r.get("message"))
    r = await tt.send({"cmd": "wifi_set", "ssid": "abc", "pass": "123"})
    check("wifi_set short password -> invalid", r.get("error") == "invalid", r.get("message"))

    # Wi-Fi scan
    mark = len(tt.events)
    r = await tt.send({"cmd": "wifi_scan"})
    done = await tt.wait_event(lambda e: e.get("type") == "scan_done", 20, mark)
    nets = [e for e in tt.events[mark:] if e.get("type") == "net"]
    check("wifi scan", r.get("ok") and done is not None and len(nets) == done.get("count"),
          f"{len(nets)} networks: " + ", ".join(f"{n['ssid']}(ch{n['channel']},{n['rssi']})" for n in nets[:12]))

    # reconnect
    await tt.disconnect()
    await asyncio.sleep(1.5)
    tt2 = Turntable()
    tt2.device = tt.device
    await tt2.connect()
    st = await tt2.state()
    check("reconnect after disconnect", tt2.client.is_connected and "motion" in st)
    r = await tt2.send({"cmd": "info"})
    heap_end = r["info"]["freeHeap"]
    check("heap stable", heap_end > heap_start - 4096, f"start {heap_start}, end {heap_end}, min {r['info']['minFreeHeap']}")
    check("no reset during suite", r["info"]["uptime"] >= (time.time() - t0) - 2, f"uptime {r['info']['uptime']} s")
    await tt2.disconnect()

    failed = [n for n, ok, _ in results if not ok]
    print(f"\n{len(results) - len(failed)}/{len(results)} passed")
    return 1 if failed else 0


async def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    if sys.argv[1] == "suite":
        return await suite()
    tt = Turntable()
    await tt.connect()
    if sys.argv[1] == "info":
        print(json.dumps((await tt.send({"cmd": "info"}))["info"], indent=2))
    elif sys.argv[1] == "state":
        print(json.dumps(await tt.state(), indent=2))
    elif sys.argv[1] == "cmd":
        secs = float(sys.argv[3]) if len(sys.argv) > 3 else 3
        print("reply:", json.dumps(await tt.send(json.loads(sys.argv[2]))))
        mark = len(tt.events)
        await asyncio.sleep(secs)
        for e in tt.events[mark:]:
            e.pop("_t", None)
            print("event:", json.dumps(e))
    await tt.disconnect()
    return 0


if __name__ == "__main__":
    sys.exit(asyncio.run(main()))
