# Bench tests

These scripts run against a real board on your desk. They do the same things the app and browser do, but automatically, so you can repeat them after every firmware change. They were used to find and verify the v2 fixes.

**Setup.** Python 3.10+ and a PC with Bluetooth:

```sh
python -m venv .venv
.venv\Scripts\pip install -r requirements.txt   # Windows (use .venv/bin/pip elsewhere)
```

**Typical run.** The turntable and the PC need to be on the same Wi-Fi. Flash the **dev** build (`pio run -e esp32dev -t upload`).

```sh
python serial_log.py COM3 3000 --out serial.log     # optional: keep a log running in another terminal
python bletool.py suite                             # BLE: ~30 checks (motion, stop, validation, scan, reconnect, heap)
python http_test.py <ip> --ota ../../firmware/.pio/build/esp32dev/firmware.bin
                                                    # HTTP + OTA: ~31 checks, ends with a real OTA update
python wdtest.py <ip>                               # watchdog restart (dev build only)
python locktest.py <ip>                             # Wi-Fi setup window (waits for 10 min uptime)
python bench.py <ip> [--ble]                        # throughput / latency
python wifitest.py "<ssid>" "<password>" 40         # provision over BLE and watch the result
```

| Script | What it checks |
|---|---|
| `bletool.py` | BLE client library plus `suite`. `info`, `state` and `cmd '<json>'` are useful for poking at the device by hand. |
| `http_test.py` | Every endpoint, the v1 `/rotate` contract and report text, BLE Stop during a blocking `/rotate`, mDNS. With `--ota`: junk, truncated, abandoned and while-moving uploads, then a real update. |
| `wdtest.py` | Freezes the network task through `/debug/stall`. The device must restart only after the current move ends, and report `lastFault`. |
| `locktest.py` | After 10 min uptime, Wi-Fi set/forget must be refused over both BLE and HTTP, and the connection must stay up. |
| `bench.py` | Ping latency and upload throughput, with or without a BLE client connected. |
| `serial_log.py` | Timestamped serial capture that survives USB hiccups. `--reset` reboots the board first. |

**Notes:**
- Close the companion app before running BLE tests on the same PC. While Chrome is connected, Windows hides the turntable's service from other programs.
- The OTA password defaults to the firmware default. Set the `TT_OTA_PASSWORD` environment variable if you changed it.
- `/debug/stall` exists only in the `esp32dev` build (`TT_TEST_HOOKS`), never in `release`.
- Expected v1 behaviour, not a failure: 90° in 1000 ms at the default acceleration is cut off at 2 s (≈85°). The control page warns about this case.
