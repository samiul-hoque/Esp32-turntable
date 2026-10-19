# Legacy firmware (v1.0.0, as delivered)

This is the firmware delivered to the client. **It is frozen. Do not edit.**
All new development happens in [`../firmware`](../firmware) (PlatformIO).

There are three ways back to this exact code:
- this folder
- git tag `v1.0.0-legacy`
- git branch `legacy`

## Build with the Arduino IDE
1. **File → Preferences → Sketchbook location:** set it to this repo's `legacy/arduino` folder, so the vendored AccelStepper 1.64 is used.
2. Copy `esp32_turntable/secret_example.h` to `esp32_turntable/secrets.h` and fill in the Wi-Fi SSID and password.
3. In **Tools**, set:
   - Board: **ESP32 Dev Module**
   - Partition Scheme: **Default**
   - Upload speed: 921600
4. Open `esp32_turntable/esp32_turntable.ino` and upload.

## Restore an exact device image
If a full flash backup was taken before upgrading:

```sh
# take the backup (4 MB WROOM-32)
esptool.py --chip esp32 read_flash 0 0x400000 backup_v1.bin
# restore it
esptool.py --chip esp32 write_flash 0 backup_v1.bin
```
