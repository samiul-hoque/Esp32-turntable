# 🎛️ Wi-Fi Controlled Turntable

A compact, heavy duty, Wi-Fi-enabled turntable powered by an **ESP32** and **NEMA 17 stepper motor**, designed for tasks that require precise and repeatable rotation. Control the device through a clean and intuitive web interface—no additional software or apps required.

---

## 🔧 Features

- 🎚️ **Configurable Rotation**  
  Set custom angle, direction, speed (via duration), repetition count, and delay between repetitions.

- 🌐 **Web-Based Control Panel**  
  Accessible from any browser on the same Wi-Fi network. Just power it on, connect, and go.

- 📊 **Movement Summary Report**  
  After each run, the device returns a clear report: total angle, duration, speed, and technical data.

- ⚡ **Simple Power & Connectivity**  
  Uses a 12V adapter and connects automatically to Wi-Fi.

---

## 🧰 Bill of Materials
[Spreadsheet](https://docs.google.com/spreadsheets/d/1zYSbkpIXjivRS2lwDfEnt78dRmq1XfWxhLyjPy-dEaU/edit?usp=sharing)


---

## 🌐 How to Use

1. **Power on** the device using the toggle switch.
2. The ESP32 will connect to the Wi-Fi network specified in `secrets.h`.
3. Open the Serial Monitor (or router’s device list) to find the ESP32's IP address.
4. Visit the IP in your browser to access the control panel.
5. Fill in the form to rotate the platform as needed.

---

## 🖥️ Web Control Panel

The built-in control panel lets you configure:

- **Repetitions**
- **Angle per Rep**
- **Movement Time per Rep** (in milliseconds)
- **Delay Between Reps**
- **Direction**: Forward or Reverse

Once submitted, the platform rotates accordingly and returns a summary of the operation.

---

## 📂 File Structure

```
TurntableProject/
├── arduino/
│   └── esp32_turntable/
│       ├── esp32_turntable.ino          // Main firmware sketch
│       ├── secret_example.h             // Template for Wi-Fi credentials
│       └── secret.h                     // Actual Wi-Fi credentials (excluded from Git)
│
├── libraries/
│   └── AccelStepper/                    // Stepper motor control library
│
├── design_files/                        // 3D models, laser-cut files, schematics, etc.
├── images/                              // Device photos, UI screenshots, wiring diagrams
├── LICENSE                              // License information
└── README.md                            // Project documentation (this file)
```




---

## 📝 Example Scenario

Want the turntable to rotate 90° counterclockwise, 3 times, with 0.5s pause between each?

Set:
- `Angle`: 90
- `Time`: 2000 ms
- `Direction`: Reverse
- `Repetitions`: 3
- `Delay`: 500 ms

The device will take care of the rest and show a detailed report.

---

## 🚀 Future Plans

- Offline mode via **ESP32 SoftAP**
- BLE support for phone apps
- Flutter-based mobile interface
- EEPROM-based Wi-Fi credential setup
- Limit detection and emergency stop support

---

## 🧑‍🔧 Developer Info

**Samiul Hoque**  
Embedded Systems & Product Engineer  
📬 [s(zero)26hoque@gmail.com] 

---

## ⚠️ License

This project is provided for personal, educational, or client-specific use. Contact the author for redistribution, commercial deployment, or bulk device licensing.

