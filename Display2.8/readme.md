# ESP32 2.8-inch TFT — 4-tab touchscreen test

This is an **offline test firmware** for the ESP32-2432S028 dual-USB CYD board (4 MB flash).
It starts immediately on the **DDS** tab, with **JSY**, **VENT** and **BOILER** touch tabs.
There is no HELLO splash screen, MQTT, Wi-Fi, or control command in this test.
All measurements show `--` until actual MQTT firmware is added.

## Files

- `Display2.8.ino`: Arduino sketch.
- `Display2.8.bin`: **Full bootable merged image** (when GitHub Actions build succeeds). In the Web ESP Tool, use **Flash**, address `0x000000`.
- `Display2.8-app-only.bin`: Application-only binary (when build succeeds). Use address `0x10000` **only** with a compatible existing bootloader and partition table.
- `.github/workflows/build-display28.yml`: GitHub Actions build script.

## Hardware

- TFT SPI: SCK=14, MOSI=13, MISO=12, CS=15, DC=2, backlight=21.
- XPT2046 touch SPI: SCK=25, MOSI=32, MISO=39, CS=33, IRQ=36.
- Sketch defaults to **ST7789** (usual for dual USB). Some batches use **ILI9341**.
  If the screen stays white, set `#define LCD_ST7789 0` and recompile.
- Touch calibration may need adjusting: touch values are logged to Serial at 115200.

## Arduino IDE

Board: **ESP32 Dev Module**, flash 4 MB. Install:
- `GFX Library for Arduino` (moononournation)
- `XPT2046_Touchscreen` (Paul Stoffregen)

## IMPORTANT

1. Save and validate your original **4 MiB full-flash factory backup** first.
2. **Do not flash until the .bin exists and the Actions build has succeeded.**
3. This is an untested-on-device diagnostic build, not a production release.
4. Changing firmware replaces the factory demo; use your validated full-flash backup for recovery.
