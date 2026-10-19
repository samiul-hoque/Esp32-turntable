# 🎛️ Wi-Fi + Bluetooth Controlled Turntable

A compact, heavy-duty turntable powered by an **ESP32** and a **NEMA 17 stepper motor**, built for tasks that need precise, repeatable rotation. It can be controlled three ways:
- from any browser on the same Wi-Fi network
- from the device's own setup hotspot
- over **Bluetooth** with the companion app

---

## 🔧 Features

- 🎚️ **Configurable Rotation:** angle, direction, speed (via duration), repetition count, delay between repetitions, acceleration.
- ⏹️ **Stop:** a sequence can be stopped at any time. It decelerates smoothly, then the motor driver is released.
- 🌐 **Web Control Panel:** open `http://turntable.local` or the device IP in any browser on the same network.
- 📱 **Bluetooth Companion App:** control the turntable and set up its Wi-Fi from Chrome/Edge on Android, Windows or Mac. Nothing to install.
- 📶 **Robust Wi-Fi:** scans every channel (1–13), joins the strongest access point, reconnects automatically, and reports *why* a connection failed.
- 🛟 **Setup Hotspot:** if Wi-Fi isn't available, the turntable opens its own hotspot with a setup page.
- ⬆️ **Over-the-air updates** through a password-protected page.
- 📊 **Movement Summary Report** after every run.

---

## 🧰 Bill of Materials
[Spreadsheet](https://docs.google.com/spreadsheets/d/1zYSbkpIXjivRS2lwDfEnt78dRmq1XfWxhLyjPy-dEaU/edit?usp=sharing)

---

## 🌐 Connecting to the turntable

The client-facing version of this section is [docs/CLIENT_GUIDE.md](docs/CLIENT_GUIDE.md).

1. **Power on.** The turntable reconnects to the last Wi-Fi network it knew.
2. **Companion app (Bluetooth):** open the app URL in Chrome or Edge and press **Connect**.
   - The **Wi-Fi** tab shows the IP address, or the reason it can't connect.
   - From there you can scan for networks and switch to another one.
3. **Browser:** open `http://<ip>` or `http://turntable.local`.
   - `.local` names work on Windows, Mac and iOS, but not reliably on Android, so the app always shows the IP too.
4. **No Wi-Fi?** After 30 s offline (or right away if no network is set up), the turntable starts the hotspot `Auleek-Turntable-XXXX`.
   - Password: `turntable`.
   - Join it and the setup page should pop up. If it doesn't, open `http://192.168.4.1`.
   - You can pick a network there too.

**Wi-Fi changes are protected.** A new network is accepted only while the turntable is offline, or within **10 minutes of power-on**. New credentials are always tested first; if they don't work, the previous network is kept.

**Network requirements:** 2.4 GHz Wi-Fi with WPA/WPA2/WPA3-Personal, or an open network. These are **not** supported:
- 5 GHz-only networks
- WPA2-Enterprise (username + password)
- networks with a captive login page (hotels, some guest Wi-Fi)

---

## 🖥️ Web Control Panel

The control panel has these fields:
- **Repetitions**
- **Angle per Rep**
- **Movement Time per Rep** (ms)
- **Delay Between Reps**
- **Direction**
- **Acceleration / Deceleration** (optional)

Rotate starts the sequence; Stop ends it.

---

## 📂 File Structure

```
Esp32-turntable/
├── firmware/                    PlatformIO project (v2, current)
│   ├── platformio.ini
│   ├── include/                 headers; config.h holds pins, limits, timeouts, UUIDs
│   ├── src/                     main, motion, wifi_manager, ble_service, web_ui, api, ota
│   └── test/                    native unit tests (motion math)
├── app/                         Bluetooth companion app (plain HTML/JS, no build step)
├── legacy/                      v1 firmware as delivered (frozen; see legacy/README.md)
├── docs/CLIENT_GUIDE.md         one-page guide for the client
└── .github/workflows/           CI build + tests, app deploy to GitHub Pages
```

---

## 🛠️ Development

### Firmware (PlatformIO)
1. Install VS Code with the **PlatformIO IDE** extension. The repo recommends it automatically.
2. Open the `firmware/` folder.
3. *(Optional)* Copy `include/secrets_example.h` to `include/secrets.h`. This file is gitignored. It provides:
   - the first-boot Wi-Fi network
   - the hotspot password override
   - the OTA password override

   A device upgraded from v1 reads its existing `ssid`/`password` from here **once**, then keeps them in flash.
4. Build and flash:

```sh
pio run -t upload -t monitor     # build, flash over USB, open serial monitor
pio run -e release               # release build (less logging)
pio test -e native               # unit tests (needs a host C++ compiler; CI runs them)
```

**Before the first v2 flash on a v1 device**, back it up (see [legacy/README.md](legacy/README.md)):

```sh
esptool.py --chip esp32 read_flash 0 0x400000 backup_v1.bin
```

The v2 partition table is `min_spiffs` (two 1.9 MB app slots for OTA). It is written automatically on the first USB flash.

### Companion app

```sh
cd app && python -m http.server 8000     # then open http://localhost:8000 in Chrome
```

`localhost` counts as a secure context, so Web Bluetooth works without HTTPS. For the client, the app is published from `main` to GitHub Pages by `.github/workflows/pages.yml`.
- Enable it once: *Settings → Pages → Source: GitHub Actions*.
- On a private repo without a paid plan, deploy `app/` to Cloudflare Pages instead.

### OTA update
Go to `http://<ip>/update`, log in as `admin` with the OTA password, and upload `.pio/build/release/firmware.bin`. Or use curl:

```sh
curl -u admin:<password> -F firmware=@.pio/build/release/firmware.bin http://<ip>/update
```

- Uploads are refused while the turntable is moving.
- The image is verified before the device switches to it, so a failed or interrupted upload leaves the current firmware running.
- An image that boots but misbehaves is **not** rolled back automatically. Test releases on a bench unit first; USB is the recovery path.

### Architecture
- **Core 1:** `loop()` only runs the stepper (`motion::update()`), as tight as v1's blocking loop, so motion is just as smooth.
- **Core 0:** the `net` task runs the web server, the Wi-Fi manager and the BLE event pump.
- Commands from HTTP and BLE go through one dispatcher, `api::handleCommand`, so both transports behave the same.
- The v1 `/rotate` endpoint is unchanged: it blocks, then returns the HTML report. Anything scripted against v1 keeps working.

| HTTP endpoint | Purpose |
|---|---|
| `GET /rotate?angle&time&direction[&repetitions&repDelay&acceleration&deceleration]` | v1-compatible, blocking, returns the HTML report |
| `GET /api/rotate?...` | same parameters, returns JSON immediately (`busy` if moving) |
| `POST /api/stop` | stop the current sequence |
| `GET /api/status` | motion + Wi-Fi + credential-test state (JSON) |
| `GET /api/report` | last report (HTML) |
| `GET /api/info` | firmware, limits, uptime, reset reason, Wi-Fi details |
| `GET` / `POST /api/scan` | cached scan results / start a scan |
| `POST /api/wifi` (`ssid`, `pass`) | test and save new Wi-Fi credentials |
| `GET` / `POST /update` | OTA page / upload (Basic Auth) |

### Bluetooth protocol (service `6e7a0001-9b3c-4f1e-8a2d-5c1f0e7b2a10`)

| Characteristic | UUID suffix | Properties | Content |
|---|---|---|---|
| command | `…0002…` | write | one JSON command per write |
| state | `…0003…` | read | current state JSON (same as `/api/status`) |
| events | `…0004…` | notify | newline-delimited JSON, split into MTU-sized chunks |

- **Commands:** `rotate` (same fields as the HTTP API), `stop`, `info`, `state`, `wifi_scan`, `wifi_set` (`ssid`, `pass`), `wifi_forget`.
- **Replies:** add an `id` to a command and its reply echoes it: `{"type":"reply","id":…,"ok":…}`.
- **Event types:** `reply`, `motion`, `wifi`, `net` (one per scan result), `scan_done`.
- **Version check:** `PROTO_VERSION` in `config.h` must match `PROTO` in `app/app.js`. The app warns on a mismatch.

### Troubleshooting

| Symptom | Where to look |
|---|---|
| Won't join Wi-Fi | Serial monitor `[wifi]` lines, or the app's Wi-Fi tab. The readable reason comes from the disconnect code: *Network not found* (name, or a 5 GHz-only network), *Wrong password*, *Signal lost*, and so on. |
| App can't find the turntable | Bluetooth on? Android: Location on. Mac: Chrome needs Bluetooth permission. `chrome://bluetooth-internals` lists nearby devices. |
| Raw BLE testing | **nRF Connect** app: write JSON to the command characteristic, subscribe to events. |
| Unexplained restarts | `resetReason` in the app's *Copy diagnostics* or `/api/info` (`brownout` points to the power supply). |

---

## 🚀 Future Plans

- ~~Offline mode via ESP32 SoftAP~~ ✅ setup hotspot
- ~~BLE support for phone apps~~ ✅
- ~~Mobile interface~~ ✅ Web Bluetooth companion app
- ~~Stored Wi-Fi credential setup~~ ✅
- Limit detection and emergency stop input (hardware)

---

## 🧑‍🔧 Developer Info

**Samiul Hoque**
Embedded Systems & Product Engineer
📬 [s(zero)26hoque@gmail.com]

---

## ⚠️ License

This project is provided for personal, educational, or client-specific use. Contact the author for redistribution, commercial deployment, or bulk device licensing.
