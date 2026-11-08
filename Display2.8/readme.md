# ESP32 Display2.8 — Energy Monitor ver0.02

**Hardware:** ESP32-2432S028 dual-USB, classic ESP32-D0WD-V3, 4 MB flash, ST7789 TFT, XPT2046 touch, 320×240 landscape.

**Source:** `Display2.8.ino`, supported public TLS certificate: `TrustedCA.h`.
**Compilation:** GitHub Actions/PlatformIO, succeeds for `esp32dev`; **physical runtime validation is still required**.

## Version history

- **ver0.02 — 08/11/2026:** CommonPlatform v1.06-compatible **classic-ESP32 adapter**: adaptive Wi-Fi TX ceiling (8.5, 11, 13, 15 dBm), nonblocking station reconnect, AP+STA recovery while retrying the router, MQTT boot/runtime backoff, local diagnostics (IP, Wi-Fi RSSI, TX ceiling, heap, NTP, reconnect attempts, stale sample ages), separate provisioning code, and locally authenticated OTA with an **app-only** binary. Retains all four touchscreen tabs and the original MQTT read-only behavior.
- **ver0.01 — 08/11/2026:** Initial live MQTT dashboard and Wi-Fi setup for the four tabs.

**Important compatibility boundary:** CommonPlatform v1.06 is designed for ESP32-C3 Super Mini; this display has a classic ESP32, so the code cannot be included verbatim. This version follows relevant networking and recovery principles, **not full source-level or feature-level parity**. It does not implement CommonPlatform's C3-specific RTC crash tracing, its remote MQTT/admin OTA protocol v2, all LittleFS credential migrations, or automatic OTA rollback. Its credentials remain in classic ESP32 Preferences/NVS.

## Four tabs

- **ΑΡΤΕΜΙΔΑ:** `power` (W), `z_diff` (kWh and estimated €), `z_now`, `z_ref`, `utility_date`.
- **ΝΕΣΤΟΥ 5Α:** three phases `p1`, `p2`, `p3`, total `power_total`; two billing zones (`z1_*` / `z2_*`).
- **VENT:** outside, inside and intermediate DS18B20; two DHT11 groups (temperature, relative and absolute humidity, feels-like); fan and AUTO/MANUAL, speed and season.
- **BOILER:** `temp_1` to `temp_4` on GPIO 5/6/7/21; **read-only**.

Black background, yellow for negative power, red for positive power/energy difference, white readings, cyan titles. Numbers use Greek decimal commas; `--` means unavailable/stale.

## Flash and OTA — make a backup first

The device's **original factory 4 MiB flash backup** must be saved separately before flashing.

- `Display2.8.bin` and `Display2.8-ver0.02.bin`: **merged bootloader + partition table + app**. Web flasher starting at **`0x000000`**. A merged image may overwrite NVS settings; provision credentials again if lost.
- `Display2.8-app-only.bin`: **app image only**. For upgrading this specific display **from ver0.01 built with the same default PlatformIO partition table**, flash at **`0x10000`** to keep the existing bootloader and partition table. This is typically less disruptive to NVS, but always keep your backup. **Do not use this offset if the existing partition layout is different.**
- After initial provisioning, in recovery/maintenance portal, go to `http://192.168.4.1/ota`. For OTA use **only** `Display2.8-app-only.bin`, not a merged `Display2.8.bin`. OTA requires the one-time displayed code as the HTTP Basic password with user `admin`. OTA needs an OTA-capable partition layout and does not provide CommonPlatform v2's complete recovery/rollback protocol.

## Credential setup and AP+STA recovery

Wi-Fi SSID/password and HiveMQ username/password **must never be committed to this public repository**. Saved local `Preferences` namespace remains `energy28`, compatible with ver0.01.

1. If the device has no saved credentials, it creates a temporary `ESP28-SETUP-XXXX` AP and shows the **random AP password and setup code on the screen**. The old fixed `Display28!` AP password from ver0.01 is **no longer used**.
2. Connect to that AP, open `http://192.168.4.1`, enter the code visible on the screen and configure Wi-Fi/HiveMQ.
3. If the router is disconnected for more than 20 s, **AP+STA recovery** becomes available while the ESP retries the configured router Wi-Fi; the device will close its recovery AP automatically after a successful reconnection (unless manually forced).
4. From USB Serial Monitor at 115200 baud, type `STATUS` for local JSON diagnostics, `PORTAL` to open a temporary maintenance AP (credentials remain stored), or `CONFIG` to clear only this device's saved settings and restart. The maintenance AP remains available until reboot.
5. When connected to the LAN, open `http://<device-IP>/health` to see non-secret diagnostics, including `fw: "ver0.02"`, IP/RSSI, heap and state ages. Configuration and OTA endpoints are closed outside portal mode.
6. MQTT uses trusted CA and TLS port **8883**, and needs NTP time synchronization. It subscribes to `home/energy/+/state`, `+/status`, `+/health` and sends only periodic `request/update` read requests; **no device actuation**.

**TX limits:** Requested limit is chosen from 8.5 / 11 / 13 / 15 dBm based on connected RSSI and never above 15 dBm. Initial radio use requests 8.5 dBm. The actual radiated power has not been measured. If firmware cannot install the initial limit, it does not initiate STA connection.

## Libraries and wiring

- LCD: SCK 14, MOSI 13, MISO 12, CS 15, DC 2, backlight 21.
- Touch XPT2046: SCK 25, MOSI 32, MISO 39, CS 33, IRQ 36.
- Library versions used by CI: `GFX Library for Arduino@1.4.9`, `XPT2046_Touchscreen`, `PubSubClient@2.8`, `ArduinoJson@6.21.5`, PlatformIO `espressif32@6.10.0`.

Binary build files are published beside the sketch by `.github/workflows/build-display28.yml` and old versioned `Display2.8-ver*.bin` files remain available.
