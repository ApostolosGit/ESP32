# Display2.8 — Energy Monitor ver0.06

**Hardware:** ESP32-2432S028 dual-USB, ESP32-D0WD-V3 (classic ESP32), 4 MB flash, 320×240 landscape ST7789, XPT2046 resistive touch.

**Latest firmware:** `Display2.8-ver0.06.bin` — complete **MERGED** image (bootloader + partition table + app), write at **`0x000000`**, Flash 4 MB, DIO. `Display2.8.bin` is identical to the current versioned merged build. **Back up the entire factory 4 MB flash before flashing.** Writing the merged image can also erase stored Wi-Fi/MQTT credentials.

The GitHub Actions build has succeeded. **ver0.06 has not yet been validated on the physical display.**

## What's new in ver0.06

- **Persistent touch settings gear** in the top-right of every measurement tab, including setup/recovery screen. Tap the gear to open settings and tap it again to return.
- **Four settings sub-tabs:** `NET` (Network Debug), `MQTT` (broker debug and REFRESH), `DEV` (one diagnostic row per device) and `SYS` (memory/reset/NTP/firmware).
- The `DEV` screen shows `S` = MQTT **status** frames, `H` = **health** frames, `D` = actual measurement **state/data** frames. `LIVE` requires a recent usable `/state`; `OFF` is a received offline status; `WAIT` means no recent measurement. The actual discovered MQTT ID is also displayed. **Tap a device row** to return to that measurement tab.
- The `MQTT` screen shows broker connection, subscription status, last received topic, number of received MQTT packets, JSON parsing failures, and sent update requests. Tap **REFRESH** to request measurements from all identified devices.
- Device discovery now reads **both topic ID and the JSON fields** `meter` and `device_type`. On `/status` and `/health`, it discovers actual device IDs and requests missing/stale `/state` samples from those same IDs. It retries requests at a bounded interval rather than displaying `--` indefinitely for a non-retained `/state`.
- When an incoming `/state` message is malformed, it records the error and source rather than silently discarding it. MQTT subscriptions are checked and retried if a subscribe call fails. Detailed messages appear in Serial at 115200.
- **Web diagnostics**, when the display is on your network: `http://<display-ip>/network-debug` and `http://<display-ip>/mqtt-status`. The second returns per-device source IDs, status/health/state counts, packet ages and request failures. Neither endpoint reveals credentials. While the setup AP is active, a valid PIN session is required.

### Diagnosing the currently missing DDS/JSY/VENT readings

Your report was: **BOILER temperature readings are live; other tabs show `--`**. This proves the display can subscribe to at least some broker data; it does **not** prove that the remaining devices are online or publishing to the same broker. We compared the upstream firmware schemas:

| Tab | Firmware's MQTT ID prefix | State fields |
| --- | --- | --- |
| ΑΡΤΕΜΙΔΑ | `DDS-` | `power`, `z_diff`, `z_now`, `z_ref` |
| ΝΕΣΤΟΥ 5Α | `jsy_house-` | `power_total`, `p1`, `p2`, `p3`, `z1_*`, `z2_*` |
| VENT | `vent-` | `temp_outside`, `temp_inside`, `temp_middle`, DHT11 values |
| BOILER | `boiler-` | `temp_1`, `temp_2`, `temp_3`, `temp_4` |

Each source publishes retained MQTT **`/status` and `/health`** and non-retained measurement **`/state`** to `home/energy/<full-id>/...`. The display subscribes to all three suffixes.

After flashing ver0.06, use **gear → DEV** and look at the numbers for DDS / JSY / VENT:
- **S=0, H=0, D=0:** the display is not receiving any of the device's subscribed frames. Check the device's actual broker and whether it is online.
- **S/H greater than 0, but D=0:** discovery works but measurement `/state` hasn't arrived. Select **gear → MQTT → REFRESH**, then check again and inspect `/mqtt-status`.
- **D greater than 0, but `WAIT`:** the most recent sample is stale or MQTT transport is disconnected; inspect the age and broker state.
- **BAD JSON greater than 0:** inspect the latest error in MQTT settings or USB Serial.

This change adds diagnostics and active requests, **not fabricated values**; a successful firmware compilation cannot guarantee DDS/JSY/VENT are live until the actual device is tested.

## Tabs and colors

Four touchscreen tabs retain the black background and prior value colors:

1. **ΑΡΤΕΜΙΔΑ** — Μετρητής ΔΕΗ Αρτέμιδα, single-zone live energy readings.
2. **ΝΕΣΤΟΥ 5Α** — JSY three-phase powers and Z1/Z2 tariffs.
3. **VENT** — three DS18B20, inside/outside DHT11 and ventilation state.
4. **BOILER** — four DS18B20 readings (GPIO 5, 6, 7 and 21); **monitoring only**.

Missing/stale measurements show `--`; no MQTT controls are sent. `REFRESH` only publishes `update` to the discovered device's `/request` topic.

## CommonPlatform v1.06 compatibility and AP setup

This firmware is a classic ESP32 adaptation of relevant CommonPlatform v1.06 behavior; the original CommonPlatform code targets the **ESP32-C3** and cannot be copied verbatim. The display uses ESP32 Preferences/NVS to preserve configuration compatibility with earlier versions and does not yet implement every C3-specific diagnostic/remote-OTA feature.

- **AP:** visible open Wi-Fi `ESP28-SETUP-XXXX`, **no Wi-Fi password**.
- **Web PIN:** `12134` (five digits); session cookie lasts **15 minutes**, with basic bad-PIN rate limiting.
- **URL:** shown on the TFT, initially often `http://192.168.2.88`, possibly `192.168.2.<last_STA_octet>` during recovery.
- Enter router Wi-Fi and HiveMQ credentials on the setup webpage. No private credentials are stored in this public GitHub repository.
- **AP+STA recovery:** if router Wi-Fi is lost, fallback AP starts while the display continues reconnecting.
- **Wi-Fi TX:** steps from 8.5 through 15.0 dBm; the maximum power is applied only **after** radio startup, then read back with ESP-IDF.
- **MQTT:** authenticated HiveMQ TLS port 8883 (trusted public CA in `TrustedCA.h`), NTP time required.
- **Serial Monitor 115200:** `STATUS` gives summary JSON, `MQTT` gives detailed per-device MQTT JSON, `PORTAL` opens maintenance AP, `CONFIG` clears only saved `energy28` credentials and restarts.
- Caution: open Wi-Fi plus HTTP and a fixed shared PIN are less secure than encrypted provisioning. Configure in a trusted physical environment.

## Firmware files and flashing

| Filename | Function |
| --- | --- |
| `Display2.8-ver0.06.bin` | **MERGED full firmware**, write at **`0x000000`** |
| `Display2.8.bin` | Latest **MERGED** firmware, same offset |
| `Display2.8-app-only.bin` | App-only image for compatible existing partitions/OTA; **not** a merged image |
| `Display2.8.ino` | Main source |
| `TrustedCA.h` | Public root certificate; **not** a credential |

**Local OTA**, if desired and compatible: open the portal, authenticate with the PIN and use exactly the `Display2.8-app-only.bin` file. Do not send the merged full image to the app-only OTA handler. The OTA partition layout must be compatible; keep a backup.

### Version history

- **ver0.06 (08/11/2026):** Settings gear, NET/MQTT/DEV/SYS screens, per-device MQTT traffic diagnostics, missing-state refresh and LAN debug endpoints.
- **ver0.05 (08/11/2026):** Correct Wi-Fi TX power initialization order and verify the limit after radio start. Confirmed by physical user test that firmware boots and BOILER MQTT temperatures appear.
- **ver0.04 (08/11/2026):** TFT first-draw before Wi-Fi, visible setup/AP error screen and boot diagnostics.
- **ver0.03 (08/11/2026):** CommonPlatform open AP and PIN 12134 with 15-minute cookie session, captive portal and network retries.
- **ver0.02 (08/11/2026):** Initial network and security adaptation to CommonPlatform 1.06.
- **ver0.01 (08/11/2026):** Initial four-tab live MQTT firmware.

All versioned merged BINs remain in this directory.

## Display hardware and build dependencies

- TFT SCK14, MOSI13, MISO12, CS15, DC2, backlight21.
- Touch SCK25, MOSI32, MISO39, CS33, IRQ36.
- PlatformIO `espressif32@6.10.0`; GFX Library for Arduino 1.4.9; XPT2046_Touchscreen; PubSubClient 2.8; ArduinoJson 6.21.5.
- Automatic CI: `.github/workflows/build-display28.yml`.

The physical test is still required to confirm the settings touch target, MQTT discovery and readings for the other three tabs.
