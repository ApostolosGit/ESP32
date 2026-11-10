# Display2.8 — Energy Monitor ver0.15

**Hardware:** ESP32-2432S028 dual-USB, ESP32-D0WD-V3 (classic ESP32), 4 MB flash, 320×240 landscape ST7789, XPT2046 resistive touch.

**Latest firmware:** `Display2.8-ver0.15.bin` — complete **MERGED** image (bootloader + partition table + app), write at **`0x000000`**, Flash 4 MB, DIO. `Display2.8.bin` is identical to the current versioned merged build. **Back up the entire factory 4 MB flash before flashing.** Writing the merged image can also erase stored Wi-Fi/MQTT credentials.

The GitHub Actions build has succeeded. **ver0.15 compiles in CI, but Wi-Fi scanning, resistive-touch keyboard accuracy and router switching still require physical testing on the actual display.**

## New in ver0.15 (10/11/2026): NET SETTINGS / SSID selection / on-screen Wi-Fi keyboard

From the screen, tap the permanent gear icon and open **NET**. The previous lower-right **OPEN AP** button has been renamed **SETTINGS**. Tapping it opens the following **local TFT screens**, without requiring a browser:

1. **WIFI SETTINGS:** Displays a fresh, asynchronous 2.4 GHz scan of nearby network names and their actual **RSSI in dBm**, with right-hand buttons **OPEN ACCESS POINT**, **CHANGE ROUTER**, and **SCAN**. The scanning page is read-only until an explicit action is selected.
2. **CHANGE ROUTER:** A second screen lists discovered networks (SSID / signal level / OPEN or LOCK). Tap a row to choose it; use PREV/NEXT to move through multiple results or RESCAN to refresh them.
3. **ROUTER PASSWORD:** A full-width resistive-touch on-screen keyboard with alphanumeric/symbol layouts, SHIFT, BACKSPACE (DEL), SPACE and CLEAR, plus SAVE and BACK. The password field is **always masked**. For the currently configured router or a previously remembered one, it displays a **fixed, non-length-revealing eight-star mask** and a **KEEP SAVED** action. The user can keep that password or type a replacement; never disclose the stored password in the TFT or MQTT.
4. **Saving:** WPA password is validated, and SAVE requires a **second confirming touch within eight seconds** before the new SSID/credentials are persisted to the same `energy28` Preferences namespace as earlier versions. A scheduled restart then connects to the newly selected router. If unavailable, the previous **ESP-Display28 AP+STA recovery** continues to work.
5. **OPEN ACCESS POINT:** Requires an explicit second touch to prevent accidental AP exposure. Opens or returns to the existing `ESP-Display28` maintenance AP without clearing settings.

**Saved Wi-Fi history:** Up to **four routers** explicitly configured from ver0.15 onward are cached locally in a separate `wifi28` Preferences namespace. On the first ver0.15 boot, the currently configured router is also imported. Routers used only in older firmware versions cannot be restored automatically if the device no longer has their credentials. The `CONFIG` serial command and web **Reset Credentials** erase both saved active settings and this history.

**Security:** Masking the password on screen prevents shoulder-surfing, but it does **not** encrypt on-device NVS: the ESP32 is not configured for flash encryption. Treat physical device/flash access as privileged. Wi-Fi scans may briefly affect networking because classic ESP32 has one shared AP/STA radio; scanning is deferred while a phone is attached to the recovery AP. Enterprise Wi-Fi authentication and hidden SSIDs are not managed by this UI; use the existing browser setup where appropriate.

These changes are intended for hardware testing. Build success alone does not establish that the touch calibration, every special keyboard symbol or AP+STA radio behavior works on this particular CYD board.

## New in ver0.14 (10/11/2026): AP auto-restart and native 48-hour history graphs

**AP credentials saved but not restarting:** Fixed a real control-flow defect in the main loop. Previously `/save` wrote Preferences and scheduled a restart for 1.4 seconds later, but the loop exited early when **any** credential was missing. The restart timer was therefore never checked during first-time AP provisioning. The new `restartAt` check runs immediately after servicing HTTP, before the empty-credentials early return. The page now confirms save and schedules reboot after 1.7 seconds; the serial monitor prints `[WEB] Saved configuration: restarting ESP32`. Confirm physically that the AP closes and STA boots with the newly saved data.

**Touch actions on each normal tab:**

- Tap the **currently selected tab header** again (ΑΡΤΕΜΙΔΑ, ΝΕΣΤΟΥ 5Α, VENT or BOILER): send an immediate non-actuating MQTT `update` to the discovered `home/energy/<source-id>/request` topic, whether measurements were previously LIVE or stale.
- Tap the **measurement area** (below the headers) anywhere: open the native 320×240 TFT **48-hour history** for the selected device. Tap inside the chart again to return; the top tabs remain available for navigation, and the settings gear still works.
- **DDS/JSY:** request existing source firmware `history_get|token|0|offset` pages and parse `/admin/history` checkpoint frames `B/P/E`. Plot net (import−export) energy in kWh per hour across 48 hourly slots. Only valid, monotonic, same-generation intervals of up to two hours are used; data are allocated proportionally to overlapping hours. Missing time ranges are blank.
- **VENT:** use existing `vent_history|current` and `vent_history|previous` commands and `/vent/history` responses to draw inside/outside temperatures for two consecutive 24-hour calendar periods at 15-minute resolution (up to 192 points).
- **BOILER:** use existing `boiler_history|current` and `boiler_history|previous` commands and `/boiler/history` responses for the four temperatures at the same resolution.
- History responses are received over wildcard subscriptions, with bounded requests and timeouts; historical samples are **not fabricated** from current readings. If the source firmware is too old or has no stored records, the TFT displays the corresponding empty/timeout message. Graphs are **native on the TFT**, not images downloaded from the APP.
- These graphs depend on the firmware and stored history on each source device and require testing with the real meter's MQTT history stream. No physical interaction tests have been performed.

**OTA test in MQTT.app:** The published `manifest-display28.txt` now describes **`Display2.8-ver0.14-app-only.bin`** in `ApostolosGit/ESP32-OTA.bin`. On a display already running compatible remote-OTA firmware, use the APP's last **Display 2.8** card → **Έλεγχος OTA** → **OTA update** and confirm with the device PIN. The firmware verifies HTTPS, expected size, hardware profile and MD5 before reboot. If outbound HTTPS still fails, use APP's **Τοπικό OTA** to upload the source repository's **unversioned** `Display2.8-app-only.bin` via the device's local PIN-protected page. Do **not** send the MERGED image to OTA.

**Always retain** the complete `Display2.8-ver0.14.bin` merged image for USB fallback at flash address `0x000000`. Keep a full flash backup before a merged rewrite.

## New in ver0.13 (09/11/2026): Trusted OTA HTTPS fix and setup PIN UI

- **Root-certificate fix:** Previous Display firmware carried only DigiCert Global Root G2 for GitHub HTTPS. Recent `raw.githubusercontent.com` certificate chains have used Let's Encrypt ISRG Root X1. The new `GithubCA.h` includes **both public roots** in a verified-TLS trust bundle; `setInsecure()` is never used. This is a plausible explanation for `https_http_-1`, not yet hardware-confirmed.
- **Improved OTA diagnostics:** Explicit GitHub DNS resolution, heap statistics, TLS handshake error reporting and up to three HTTPS GET attempts before any firmware flash writes. Still enforces GitHub firmware URL allowlist, expected app image size, hardware profile and expected MD5.
- **No PIN-length hints:** Display's PIN web form simply asks for the device PIN, without advertising the number of digits, a minimum length, or a fixed count in its placeholder. The TFT setup screen no longer prints the PIN itself. The device continues validating the configured PIN in firmware with throttling.
- **MQTT.app v5.98:** Display card has a new **Τοπικό OTA** button, opening the device's existing local `/ota` page at the recorded private LAN IP. The Display28 PIN prompt does not reveal its length, and the Display28 device validates the PIN rather than trusting client-side comparison. Existing legacy-device OTA controls are unchanged. Other measurement cards remain intact.
- **First-install recovery:** Firmware already running ver0.11/ver0.12 cannot acquire new CA roots until its app image has been upgraded. Download the latest unversioned `Display2.8-app-only.bin` from the source repo (correct upload filename), open the Display's `http://<display-lan-ip>/ota` via its local web interface, authenticate with the device PIN, select that **app-only** file and upload. The older web upload handler accepts only the literal filename `Display2.8-app-only.bin`, so do not select the versioned OTA file without renaming it. This action does **not** require remote GitHub access from the old ESP32 firmware.
- **Fallback:** `Display2.8-ver0.13.bin` is the **MERGED** USB-flash firmware at address `0x000000`. Keep a complete 4 MiB flash backup before writing merged firmware.

## New in ver0.12 (09/11/2026): Fix display flickering

The previous firmware called `lcd->fillScreen(UI_BG)` each time an MQTT frame led to a refresh, clearing the entire display before drawing the whole interface again. This caused an obvious **full-screen blinking/repainting effect**, even when only one temperature or power number changed.

- **No full-screen clearing on an ordinary MQTT update**. Full redraw now happens only on actual screen-layout changes (switching measurement tabs or entering/exiting/changing settings panels) or explicit boot/AP/recovery screens.
- Static navigation tabs, headings, labels and card backgrounds stay in place; each measurement uses an independent **last-value + color cache**. Only modified values erase and redraw their small bounding rectangles. Unchanged fields are skipped.
- MQTT receive callback schedules a lightweight UI update **outside the network callback**, throttled to at most 4 updates per second; this reduces SPI traffic and protects MQTT responsiveness.
- The MQTT connection indicator and footer remain functional. `/health` includes `display_full_redraws`, `display_partial_redraws` and `display_unchanged_cells_skipped` counters for diagnostics.
- The layout and Greek labels of DDS / JSY / VENT / BOILER remain the same; settings, AP recovery, the PIN-gated web page, remote OTA and Restart are not deliberately changed.
- The build includes a complete **MERGED `Display2.8-ver0.12.bin`**, flash at **`0x000000`**; `Display2.8.bin` points to the same latest merged image. Always keep a full 4 MiB backup before writing.
- This fixes the **verified software full-screen clear path**; if the physical screen continues blacking out, check the 5V power, LCD/backlight and serial reset reason, which cannot be validated by CI.

## New in ver0.11 (09/11/2026): Local CommonPlatform-style web administration

The Display2.8 runs a local HTTP server on **both the home LAN and the recovery AP**. If its **Wi-Fi works but MQTT credentials are wrong**, it is **not necessary to erase Wi-Fi settings, wait for router failure, or use USB**.

1. Tap the permanent settings gear on the TFT, open **NET** and read **IP**. On a phone/computer using the same router Wi-Fi, browse to `http://<display-IP>/` (for example `http://192.168.1.82/` if NET shows that IP).
2. Authenticate using the platform PIN shown on the device. A 15-minute cookie session (HttpOnly, SameSite=Strict) is required for every local maintenance action, **including over the LAN**. Wrong PIN attempts are temporarily locked out.
3. In **Wi-Fi / MQTT configuration**, correct HiveMQ host or username; enter the new MQTT password. Existing SSID, MQTT username and broker host are prefilled after HTML escaping. **Passwords are never displayed/prepopulated**: leave a password blank to keep the stored one when not switching Wi-Fi networks. If the Wi-Fi SSID changes, a blank Wi-Fi password configures an open network.
4. Choose **Save settings & restart**. The new values are stored in the existing `energy28` Preferences namespace, so no full erase is required. The device restarts after the HTTP response.
5. Maintenance routes: `/` / `/setup` (admin page), `/network-debug` (JSON), `/mqtt-status` (per-device MQTT JSON), `/debug.txt` (copyable diagnostics), `/restart-esp` (GET confirmation and POST reboot), `/forget-credentials-confirm` (explicit destructive confirmation), and `/ota` (upload only `Display2.8-app-only.bin`, never merged BIN).
6. If a browser cannot reach the LAN IP, tap **gear → NET → OPEN AP** or **gear → MQTT → OPEN AP** **twice within 8 seconds**. This intentionally opens the separate maintenance Wi-Fi `ESP-Display28`; join it and use the AP IP shown on the TFT, typically `http://192.168.4.1`. This manual AP remains until restart, while the router connection may continue.
7. **Security boundary:** This is an adaptation of CommonPlatform web setup/restart/diagnostics, **not the complete ESP32-C3 platform** (no C3 RTC crash-trace recovery/IDE ArduinoOTA protocol equivalence). Open AP/HTTP are unencrypted; configure only in trusted surroundings. The public MQTT.app **does not display the setup PIN, AP instructions, or firmware repository help paragraph**.

## New in ver0.10 (09/11/2026): Hybrid AP+STA auto-recovery

- Normal operation: ESP32 connects to the configured router in **STA-only**, keeping its open `ESP-Display28` AP off.
- If that router remains unavailable for roughly **20 seconds**, a recovery AP appears as `ESP-Display28` with **no Wi-Fi password**, browser PIN **(device PIN)**, and the AP address shown on the screen (usually `http://192.168.4.1`).
- The recovery radio stays in **WIFI_AP_STA mode**; unlike ver0.09, it checks the **saved router every 60 seconds** with `esp_wifi_connect()`, without resetting the SoftAP.
- **While a phone is connected to the AP, router reconnection attempts pause.** Web activity also provides a 20-second quiet period to protect settings submissions. ESP32 has one radio, so an active STA scan can nevertheless briefly disturb SoftAP beacons.
- When router association and IP are valid for at least **15 continuous seconds**, recovery AP automatically closes only **if no AP client is connected and no recent web configuration is in progress**. Normal MQTT reconnects then resume.
- If the old router does not return (for example, its SSID or password changed), the AP stays available until a person supplies the new Wi-Fi credentials. The manually forced `PORTAL` mode stays open until reboot.
- The earlier **requested 20 dBm TX** behavior, four sensor tabs and MQTT.app management/OTA support are retained. Actual radio power is hardware- and regional-regulation-dependent; 20 dBm is **not** the original CommonPlatform 1.06 15 dBm ceiling.

**Important:** This is a new hardware behavior. GitHub Actions compiling it does not constitute a verified test of radio visibility, reconnect timing or forced AP lifetime.

## What's new in ver0.09 (09/11/2026)

- **Fixed Access Point name:** `ESP-Display28`, open 2.4 GHz Wi-Fi, no connection password. Web setup PIN **(device PIN)**, 15-minute cookie.
- **Stable AP-only router recovery:** on repeated failed router association, stops STA association and leaves Wi-Fi in **AP-only mode**. This avoids the ESP32 shared radio's STA scans/channels hiding the AP from phones. Old router credentials are replaced after using the AP page and saving/rebooting.
- **TX power in latest ver0.09:** explicit **requested maximum 20.0 dBm** for AP/STA (the earlier v0.07 used 15.0 dBm, and original CommonPlatform v1.06 is capped at 15.0 dBm). Actual calibrated/radiated TX depends on chip, country, antenna and SDK; software request is not a measurement. This is a deliberate divergence from CommonPlatform's TX cap, and must comply with local radio rules.
- **Management device identity:** `home/energy/display28-<chip>/health`, `/state` and retained `/status`. Announces the hardware profile `esp32-display28-st7789` and OTA protocol `2`. The original four measurements are unchanged.
- **Remote OTA from MQTT.app v5.97:** `home/energy/display28-<chip>/admin/request` accepts a narrowly allowed `ota_https_v2` request with size, MD5, HTTPS GitHub URL, exact hardware profile and web PIN. Downloads **only** versioned **app-only BIN** from `ApostolosGit/ESP32-OTA.bin`; verifies expected size and MD5 with Arduino Update before reboot. Rejects merged images and arbitrary domains. Remote Restart also requires PIN. Both operations are available **only when the display is already online over MQTT**.
- **Public OTA release:** `ESP32-OTA.bin/Display2.8-ver0.08-app-only.bin` with `manifest-display28.txt` (file/size/MD5/profile), generated by a dedicated GitHub Actions workflow. Source and versioned merged image are kept in the `ESP32` repository.
- **First remote OTA setup:** A display still running `ver0.06` or earlier does **not** support admin OTA. Install ver0.08 **MERGED at 0x000000 once using USB**; then later OTA updates may be made from the APP after a compatible newer version is published.

## ver0.07 (09/11/2026)

- Introduced persistent open `ESP-Display28` setup and initial CommonPlatform-style MQTT identity, remote reboot and app-only OTA implementation. Superseded by ver0.08 AP-only recovery changes.

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

- **AP:** visible open Wi-Fi `ESP-Display28`, **no Wi-Fi password**.
- **Web PIN:** `(device PIN)` ; session cookie lasts **15 minutes**, with basic bad-PIN rate limiting.
- **URL:** shown on the TFT, initially often `http://192.168.4.1`, possibly `192.168.2.<last_STA_octet>` during recovery.
- Enter router Wi-Fi and HiveMQ credentials on the setup webpage. No private credentials are stored in this public GitHub repository.
- **AP-only recovery (ver0.08):** when router Wi-Fi is unavailable, the display pauses STA reconnect attempts to keep the AP visible until new Wi-Fi settings are saved.
- **Wi-Fi TX:** latest ver0.08 requests up to 20 dBm after radio startup with ESP-IDF readback. This is **not** the original CommonPlatform 15 dBm ceiling.
- **MQTT:** authenticated HiveMQ TLS port 8883 (trusted public CA in `TrustedCA.h`), NTP time required.
- **Serial Monitor 115200:** `STATUS` gives summary JSON, `MQTT` gives detailed per-device MQTT JSON, `PORTAL` opens maintenance AP, `CONFIG` clears only saved `energy28` credentials and restarts.
- Caution: open Wi-Fi plus HTTP and a fixed shared PIN are less secure than encrypted provisioning. Configure in a trusted physical environment.

## Firmware files and flashing

| Filename | Function |
| --- | --- |
| `Display2.8-ver0.15.bin` | **MERGED full firmware**, write at **`0x000000`** |
| `Display2.8.bin` | Latest **MERGED** firmware, same offset |
| `Display2.8-app-only.bin` | App-only image for compatible existing partitions/OTA; **not** a merged image |
| `Display2.8.ino` | Main source |
| `TrustedCA.h` | Public root certificate; **not** a credential |

**Local OTA**, if desired and compatible: open the portal, authenticate with the PIN and use exactly the `Display2.8-app-only.bin` file. Do not send the merged full image to the app-only OTA handler. The OTA partition layout must be compatible; keep a backup.

### Version history

- **ver0.15 (10/11/2026):** NET SETTINGS scan list with RSSI, AP controls, Change Router screen, masked on-screen credentials keyboard, four-router NVS cache, confirmed restart.
- **ver0.14 (10/11/2026):** AP-save restart fix; active-tab MQTT refresh; 48h native TFT history graphs via existing energy/temperature MQTT protocols.
- **ver0.13 (09/11/2026):** Dual-root GitHub TLS certificate bundle and HTTPS preflight/retries; local OTA fallback; PIN-length hints removed.
- **ver0.12 (09/11/2026):** Delta-based TFT drawing, unchanged-value caching, no full-screen clear on MQTT updates, limited redraw rate.
- **ver0.11 (09/11/2026):** Local PIN-gated management website on LAN and AP; edit saved WiFi/MQTT credentials, view diagnostics, restart, clear credentials or upload app-only OTA; two-tap TFT OPEN AP.
- **ver0.10 (09/11/2026):** AP+STA recovery, 60s saved-router retry, pause when AP client active, auto-close after stable router recovery.
- **ver0.09 (09/11/2026):** Stable AP-only fallback with STA reconnections suspended while the AP is open, fixed AP name `ESP-Display28`, maximum 20 dBm requested TX (regulatory/hardware limited); correct on-screen AP-only label; verified matching app-only OTA file published to ESP32-OTA.bin. Compile successful, physical network and OTA testing outstanding.
- **ver0.09 (09/11/2026):** Accurate AP-only status text; merged + OTA release verified by CI.
- **ver0.08 (09/11/2026):** AP-only reliable recovery, 20 dBm requested TX, Wi-Fi name ESP-Display28; remote MQTT OTA and restart management.
- **ver0.07 (09/11/2026):** Initial Display management and OTA protocol.
- **ver0.06 (08/11/2026):** Settings gear, NET/MQTT/DEV/SYS screens, per-device MQTT traffic diagnostics, missing-state refresh and LAN debug endpoints.
- **ver0.05 (08/11/2026):** Correct Wi-Fi TX power initialization order and verify the limit after radio start. Confirmed by physical user test that firmware boots and BOILER MQTT temperatures appear.
- **ver0.04 (08/11/2026):** TFT first-draw before Wi-Fi, visible setup/AP error screen and boot diagnostics.
- **ver0.03 (08/11/2026):** CommonPlatform open AP and PIN (device PIN) with 15-minute cookie session, captive portal and network retries.
- **ver0.02 (08/11/2026):** Initial network and security adaptation to CommonPlatform 1.06.
- **ver0.01 (08/11/2026):** Initial four-tab live MQTT firmware.

All versioned merged BINs remain in this directory.

## Display hardware and build dependencies

- TFT SCK14, MOSI13, MISO12, CS15, DC2, backlight21.
- Touch SCK25, MOSI32, MISO39, CS33, IRQ36.
- PlatformIO `espressif32@6.10.0`; GFX Library for Arduino 1.4.9; XPT2046_Touchscreen; PubSubClient 2.8; ArduinoJson 6.21.5.
- Automatic CI: `.github/workflows/build-display28.yml`.

The physical test is still required to confirm the settings touch target, MQTT discovery and readings for the other three tabs.

## Published OTA manifest (ver0.09)

The remote manager in MQTT.app v5.97 reads [`manifest-display28.txt`](https://github.com/ApostolosGit/ESP32-OTA.bin/blob/main/manifest-display28.txt) and downloads only the **app-only** `Display2.8-ver0.09-app-only.bin`, never the merged flash image. The OTA file published for ver0.09 was verified to have the same Git blob SHA as `ESP32/Display2.8/Display2.8-app-only.bin`. Version, profile, byte count and MD5 are checked before the ESP32 applies the upgrade. On-device network/MQTT/PIN verification must pass. Recovery after loss of the router uses the AP and local setup page; remote OTA is not possible when the Display28 is offline.

The published v0.08 app-only image is archived from an earlier build and does **not** match the latest v0.08 main-branch app-only image; do not manually install it. Use v0.09 or later for OTA instead.
