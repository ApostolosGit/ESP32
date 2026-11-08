# ESP32 Display2.8 — Energy Monitor (MQTT)

Working **compile-tested**, not yet verified on physical hardware. For the **ESP32-2432S028 dual-USB**, ST7789, XPT2046, 4 MB flash, 320×240 landscape.

**The Arduino sketch and full-flash BIN are automatically built and committed in this folder.**

## Four touchscreen tabs

- **ΑΡΤΕΜΙΔΑ** — «ΜΕΤΡΗΤΗΣ ΔΕΗ ΑΡΤΕΜΙΔΑ»: live `power` (W), `z_diff` (kWh plus estimated cost), `z_now`, `z_ref`, date `utility_date`.
- **ΝΕΣΤΟΥ 5Α** — three phase powers `p1`/`p2`/`p3` and `power_total`, zones Z1 (ΑΚΡΙΒΟ) and Z2 (ΟΙΚΟΝΟΜΙΚΟ), `z1_diff`, `z2_diff`, `z1_now`, `z2_now`, `z1_ref`, `z2_ref`.
- **VENT** — three DS18B20 values, outside and inside DHT11 readings (temperature/humidity/absolute humidity/feels-like), mode/fan/speed/season.
- **BOILER** — four DS18B20 readings `temp_1` through `temp_4` (GPIO 5/6/7/21). **Read-only**, no boiler control.

The style uses a black background, yellow for negative power, red for positive power/energy difference, white readings and cyan headers. Numbers use Greek decimal comma and thousands dot. All data are from the MQTT JSON; `--` means unavailable or stale, not an artificial measurement.

## First boot / private credentials

**No passwords or Wi-Fi credentials are stored in this public repository.**

1. Before flashing, save the original **4 MiB factory flash backup**.
2. Flash the **complete merged** `Display2.8.bin` to address **`0x000000`** with a compatible Web ESP flasher. **Do not** use the app-only BIN at that address.
3. On first boot, the screen shows **SETUP WIFI + MQTT**.
4. From a phone or PC, connect to the temporary Wi-Fi access point `ESP28-SETUP-XXXX` shown on the screen. AP password: **`Display28!`**.
5. Open **`http://192.168.4.1`** and enter your home's Wi-Fi SSID/password and the **HiveMQ MQTT username/password**; the broker host is prefilled from the companion MQTT.app. Set the electricity price in €/kWh (default **0.25**). Save; ESP32 restarts into the tabs.
6. The device connects to HiveMQ Cloud using **TLS 8883 with trusted ISRG Root X1**, subscribes to `home/energy/+/state`, `+/status`, `+/health` and sends only read/update requests `home/energy/<device-id>/request` → `update` about once per minute, **never sends a control or OTA command**.

If the configured Wi-Fi is unreachable at boot, setup mode starts again. To clear saved on-device credentials deliberately, open a USB Serial Monitor at **115200 baud**, enter `CONFIG` and press Return (then reboot). MQTT requires that network time/NTP is available for TLS validation.

The Wi-Fi AP uses WPA2 and shuts down after setup. Credentials are saved in ESP32 Preferences (not in GitHub); as with normal ESP32 NVS, physical flash access can potentially recover them unless flash encryption is enabled.

## File roles

| File | Description |
| --- | --- |
| `Display2.8.ino` | Arduino sketch with display, touch, provisioning and MQTT |
| `TrustedCA.h` | Public ISRG Root X1 CA (not a secret) |
| `Display2.8.bin` | Full merged image: bootloader + partitions + application, flash at `0x000000` |
| `Display2.8-app-only.bin` | Application only, offset `0x10000` **only if existing partition table is compatible** |
| `.github/workflows/build-display28.yml` | Automatic compile and commit of binaries |

## Hardware wiring (on-board)

- TFT: CLK 14, MOSI 13, MISO 12, CS 15, DC 2, backlight 21.
- Touch: CLK 25, MOSI 32, MISO 39, CS 33, IRQ 36.
- ST7789 and rotation calibrated against first tested tab firmware, but the live MQTT build still needs a real hardware test.
- Code uses GFX Library for Arduino 1.4.9, XPT2046_Touchscreen, ArduinoJson 6.21.5 and PubSubClient 2.8.

### Important

Compilation success does **not** prove the screen, touch, Wi-Fi, NTP, or MQTT credentials work on your physical device. Report a photo of the first-boot setup and a later tab, or Serial Monitor output, to adjust safely.
