# ESP32 Display2.8 — Energy Monitor ver0.05

**Board:** ESP32-2432S028 dual USB, classic ESP32-D0WD-V3, 4 MB flash, ST7789 TFT 320×240 and XPT2046 touch.

The source is `Display2.8.ino` (with the public CA header `TrustedCA.h`). Github Actions automatically builds a complete merged `Display2.8.bin` and an immutable versioned `Display2.8-ver0.03.bin`. A separate `Display2.8-app-only.bin` is provided for compatible OTA/partition setups. **The requested current merged firmware is `Display2.8-ver0.05.bin`, flash offset `0x000000`.**

**Build status:** GitHub Actions/PlatformIO compiled and published ver0.05 successfully. **Physical validation of this new build remains outstanding.**

## Version history

- **ver0.05 — 08/11/2026:** Fixes the confirmed `AP TX power failed` initialization error. Previous firmware called Arduino `WiFi.setTxPower()` before `WiFi.softAP()` had started the AP, which is invalid for the underlying Wi-Fi driver. The AP is now started first, and only then the TX ceiling is programmed through the ESP-IDF `esp_wifi_set_max_tx_power()` API with short bounded retries and readback through `esp_wifi_get_max_tx_power()`. The station (router) path also starts Wi-Fi before configuring the TX limit. Detailed native errors are printed to USB Serial at 115200. If TX verification fails, the AP is stopped to avoid operating at an unknown power limit and retried later. The open AP with web PIN **12134** and 15-minute session remains unchanged. **Compiled successfully, physical validation pending.**


- **ver0.04 — 08/11/2026 (diagnostic):** Addresses a screen-startup failure path reported as **random multicolored pixels**. Initializes and draws the TFT/DDS tab **before initiating Wi-Fi or the setup AP**. Checks `lcd->begin()` and logs resets, RAM and LCD initialization on Serial at 115200. If setup AP radio mode, TX power enforcement, or AP startup fails, it shows an explicit **NETWORK ERROR** screen and schedules a retry instead of returning with the TFT framebuffer uninitialized. This is a **plausible failure path, not a hardware-confirmed root cause**; a physical test and/or Serial Monitor logs are required to diagnose remaining TFT/SPI/power issues.
- **ver0.03 — 08/11/2026:** Adjusted to CommonPlatform v1.06 **open access point, protected web login with PIN `12134` and 15-minute session cookie (`EMSESSION`, HttpOnly, SameSite=Strict)**. Added PIN login, expiry/revocation, attempt lockout, local DNS captive portal, authenticated provisioning and OTA pages/actions, AP health checks/retry, `192.168.2.XX` style AP addressing, adaptive Wi-Fi reconnect TX steps up to **15 dBm**, MQTT transport cleanup on router loss, and NTP initialization after STA association. The four existing measurement tabs remain unchanged.
- **ver0.02 — 08/11/2026:** Classic-ESP32 adaptation of CommonPlatform's network and service approach: Wi-Fi TX cap, AP+STA recovery, MQTT backoff, `/health` diagnostics, local OTA and provisioning; used a different **WPA-protected AP** and temporary setup code.
- **ver0.01 — 08/11/2026:** Initial four-tab live MQTT dashboard with first-boot setup.

Versioned full-flash BINs `Display2.8-ver0.01.bin`, `Display2.8-ver0.02.bin` and `Display2.8-ver0.03.bin` are retained beside `Display2.8.ino`. The unversioned `Display2.8.bin` always points to the latest successful build. Previous `Display2.8-ver0.04.bin` remains available; new `Display2.8-ver0.05.bin` is the current merged build.

## AP access — CommonPlatform v1.06 behavior

1. When Wi-Fi/HiveMQ credentials are missing, the display creates a visible **open Wi-Fi** network called `ESP28-SETUP-XXXX`. **There is no Wi-Fi password.**
2. Connect to the AP and browse to the **IP shown on the display**. By default it is `http://192.168.2.88` without a known STA address; while in recovery it may use `192.168.2.<STA-octet>`.
3. The web page prompts for the five-digit **PIN `12134`**. An accepted PIN creates a browser session with a random token and a **15-minute expiry**. After expiry, log in again. Invalid PIN attempts are rate-limited (5 tries → 30 s cooldown).
4. After successful login, configure local Wi-Fi SSID/password and HiveMQ Cloud username/password. **No Wi-Fi or MQTT credentials are committed to this GitHub repository.** They remain in ESP32 Preferences/NVS in namespace `energy28`, compatible with ver0.01–0.02.
5. The credentials save endpoint, `/health`, `/ota`, and the upload endpoint all require the browser session **while the AP is active**. The same local web server serves read-only `/health` over the normal LAN when AP mode is off.
6. If the router is unavailable for about 20 s, an **AP+STA recovery** network opens while the ESP32 continues to retry its router connection. Automatic recovery AP closes after the router returns; a manually forced AP remains open until restart.

**Important security tradeoff:** The open AP and a fixed, publicly documented PIN match CommonPlatform, as requested, but **HTTP on open Wi-Fi is unencrypted**. Another nearby Wi-Fi client may be able to observe network traffic or attempt access, including setup credentials. Do initial configuration in a trusted physical environment; prefer password-protected setup for stronger protection.

The firmware uses `192.168.2.XX`-style AP addresses. The generic CommonPlatform C3 uses a configured static STA octet; this Display2.8 board has no reserved static address configured, so the AP uses **88** if no STA IP is known.

## Display and MQTT

All four 320×240 landscape tabs preserve the black-background visual style:

- **ΑΡΤΕΜΙΔΑ:** `power`, `z_diff`, `z_now`, `z_ref` and `utility_date` for `Μετρητής ΔΕΗ Αρτέμιδα`.
- **ΝΕΣΤΟΥ 5Α:** JSY three phase powers `p1`/`p2`/`p3`, signed total power, `z1_*` and `z2_*` estimated/meter differences.
- **VENT:** DS18B20 temps, inside/outside DHT11 readings and ventilation state.
- **BOILER:** Four DS18B20 temperatures `temp_1..temp_4` (GPIO 5/6/7/21). **Read-only**, never controls an actuator.

MQTT subscribes to `home/energy/+/state`, `/status`, `/health` and publishes **only** `home/energy/<id>/request → update` as a read-request. TLS 8883 uses the trusted CA from `TrustedCA.h`. NTP is needed for certificate validation.

## Firmware flashing

**Back up the original 4 MiB flash before changing firmware.** The newly built code has not yet been tested on the actual display.

| File | Use |
| --- | --- |
| `Display2.8-ver0.05.bin` | **Full merged firmware**, flash **0x000000** in Web ESP Tool |
| `Display2.8.bin` | Same full merged image, latest version, flash **0x000000** |
| `Display2.8-app-only.bin` | Application-only update (when partition table is compatible), never at offset 0 |
| `Display2.8.ino` | Arduino sketch; compile with `TrustedCA.h` |

Local OTA is available only while the maintenance/setup AP is open. Sign in with the web PIN and select **exactly** `Display2.8-app-only.bin` on the OTA form. **Do not upload the merged BIN to local OTA.** OTA uses a standard ESP32 OTA partition and is **not** the full CommonPlatform remote MQTT OTA v2 protocol.

## USB Serial commands (115200 baud)

- `STATUS`: prints version/connection/heap/age diagnostics.
- `PORTAL`: forces a temporary AP+STA maintenance portal, preserving credentials.
- `CONFIG`: clears `energy28` local preferences and restarts into first-boot setup.

`/health` provides device ID, firmware version, Wi-Fi RSSI, TX power cap, IP, MQTT, heap and reconnect attempts. It does not reveal passwords.

## CommonPlatform compatibility boundary

The upstream CommonPlatform v1.06 was written for **ESP32-C3 Super Mini**. This is a **functional adaptation** for classic ESP32-D0WD, not a line-by-line port:

- Implemented: open-AP/PIN web session, captive portal DNS, network retries and AP+STA recovery, capped adaptive TX steps, MQTT TLS/backoff, on-device diagnostics, versioned firmware and local OTA.
- Not implemented: full C3 RTC crash breadcrumbs and recovery log, CommonPlatform LittleFS credential-record migration/verification, its remote MQTT/admin OTA v2 profile, all C3 hardware/flash-button monitoring, complete recovery rollback and identical static IP provisioning.

For this display **do not flash the C3 CommonPlatform binary**; it targets a different ESP32 chip.

## Firmware build environment

- LCD SPI: CLK 14, MOSI 13, MISO 12, CS 15, DC 2, BL 21.
- Touch SPI: CLK 25, MOSI 32, MISO 39, CS 33, IRQ 36.
- PlatformIO `espressif32@6.10.0`; Arduino_GFX 1.4.9, XPT2046_Touchscreen, PubSubClient 2.8, ArduinoJson 6.21.5.
- CI: `.github/workflows/build-display28.yml`.

Always verify the first boot, the AP login, touch handling, reconnect and live MQTT measurements on the physical device before treating firmware as production-ready.

## Troubleshooting random colored pixels

The known-working **offline four-tab test** (no Wi-Fi/MQTT) preceded all MQTT firmware versions. The archived full-flash binary is in commit `0caa268bce6c3de5a96943e169b86caa11dfd002` at `Display2.8/Display2.8.bin`. It can help distinguish a power/display/flash issue from the network-enabled firmware. Because it is a **full-flash image**, back up the current flash/credentials first; do not use it for OTA.

When testing ver0.04, connect USB Serial Monitor at **115200 baud**. A healthy boot prints `[BOOT] TFT init OK`, then either `[BOOT] No credentials -> open AP setup` and a setup page, or the usual four tabs. If colored pixels persist before any message, check the actual flashed **merged BIN**, offset `0x000000`, correct ESP32/4MB/DIO settings, and a reliable 5V USB supply. If the boot repeatedly reports reset/brownout, fix power first. Do **not** repeatedly flash unverified firmware without preserving the factory backup.

## Diagnostic after ver0.05

If the display previously showed `AP TX power failed`, the cause is known: firmware tried to set Wi-Fi output power before starting the radio. The corrected boot should show the open AP's SSID and its address. When selecting it on a phone, **do not enter a Wi-Fi password**, because this AP is open. Open the displayed `http://192.168.2.XX` address and authenticate to the setup webpage with PIN **12134**. The exact IP address is shown on the TFT. Credentials are entered only through the local setup page.

If a new TX error occurs, USB Serial Monitor at **115200 baud** now reports the ESP-IDF error name and code, along with the requested and measured TX power. Send those lines for diagnosis. A compiled BIN does not establish that the physical board's networking is functioning; please test it on the actual panel.
