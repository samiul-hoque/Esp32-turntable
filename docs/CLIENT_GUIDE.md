# Auleek Turntable: Quick Guide

## What you need
- The turntable, plugged in and switched on.
- A phone or laptop with Bluetooth, using **Google Chrome** or **Microsoft Edge**. Android, Windows and Mac all work. On iPhone/iPad, use the free **Bluefy** browser.
- Your Wi-Fi network name and password. The turntable uses **2.4 GHz** Wi-Fi.

## Open the app
Go to: **https://samiulmakes.com/Esp32-turntable/**

On Android you can tap ⋮ → *Add to Home screen* to keep it like an app.

## Connect
1. Press **Connect**.
2. Choose **Auleek-Turntable-…** from the list and press *Pair*.

If the turntable isn't listed:
- Check it's switched on and within a few metres.
- **Android:** turn on *Location* (Android needs it for Bluetooth scanning).
- **Mac:** System Settings → Privacy & Security → Bluetooth → allow Chrome.

## Set up or change Wi-Fi
1. Open the **Wi-Fi** tab and press **Scan for networks**.
2. Tap your network, enter the password, and press **Connect turntable to this network**.
3. Within about 30 seconds you'll see either:
   - ✅ **Connected**, with the turntable's web address (for example `http://192.168.1.42`). Bookmark it.
   - ❌ **The reason it failed**, for example *Wrong password* or *Network not found*. Your previous settings are kept.

> 🔒 **Wi-Fi changes are locked while the turntable is connected.** To change network, switch the turntable off and on again, then make the change within **10 minutes**.

Hidden network? Tap *Enter a network name manually*.

## Control the turntable
Use the **Control** tab in the app, or the web address from your browser. You can also try `http://turntable.local` on a computer.

| Field | Meaning |
|---|---|
| Repetitions | how many times to repeat the move |
| Angle per Rep | degrees per move |
| Movement Time per Rep | how long each move takes (ms; 1000 ms = 1 second) |
| Delay Between Reps | pause between moves (ms) |
| Direction | Forward / Reverse |
| Acceleration / Deceleration | optional; how gently to speed up and slow down |

**Stop** halts the turntable smoothly at any time.

## No Wi-Fi available?
If the turntable can't reach Wi-Fi for 30 seconds, it opens its own hotspot.
1. Join the Wi-Fi network **Auleek-Turntable-…** with password **`turntable`**.
2. A setup page opens. If it doesn't, browse to **http://192.168.4.1**.
3. Use the turntable from that page, or choose a Wi-Fi network for it.

## Something not working?
- In the app, open **Device** → **Copy diagnostics**, and send the copied text to your support contact.
- Switching the turntable off and on is always safe.
