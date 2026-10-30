# ESP32-C3 Super Mini — firmware .ino

## Επιλογή OLED για τις δικές μας συνδεσμολογίες (128×64, I²C)

| `OLED_TYPE` | Πάνελ | Πραγματικός οδηγός U8g2 |
|---|---|---|
| **3** | **CH1116, 1,54″** | `U8G2_SSD1309_128X64_NONAME0_F_HW_I2C` (δοκιμάστηκε στην οθόνη χρήστη) |
| **2** | **SSD1309, 1,3″** | `U8G2_SSD1309_128X64_NONAME0_F_HW_I2C` |
| 1 | SH1106 (παλαιά/συμβατότητα) | `U8G2_SH1106_128X64_NONAME_F_HW_I2C` |
| 0 | χωρίς OLED | κανένας |

Στο ESP32-C3: SDA=GPIO3, SCL=GPIO4, συνήθως I²C 0x3C. Η διάσταση από μόνη της **δεν προσδιορίζει γενικά** τον controller· η παραπάνω αντιστοίχιση αφορά τα συγκεκριμένα πάνελ.

- [CommonPlatform v1.05](./CommonPlatform/ESP32-C3-supermiini-CommonPlatoform.ino) — επιλογές `0/1/2/3`.
- [Boiler v4.02 CH1116](./Boiler/Boiler_ESP32C3_V4_02/Boiler_ESP32C3_V4_02.ino) — `OLED_TYPE 3`, οθόνη 1,54″, SSD1309 initialization.
- [Boiler v4.01 (παλιό SH1106)](./Boiler/Boiler_ESP32C3_V4_01/Boiler_ESP32C3_V4_01.ino) — αρχική επιλογή `1` διατηρείται.
- [Boiler v4.00 (παλιό SSD1309)](./Boiler/Boiler_ESP32C3_V4_00/Boiler_ESP32C3_V4_00.ino) — αρχική επιλογή `2` διατηρείται.

**Οι αριθμοί εκδόσεων, οι προεπιλεγμένες τιμές OLED_TYPE και τα ήδη δημοσιευμένα OTA BIN/manifest δεν τροποποιήθηκαν.** Οι παλαιότερες ετικέτες μεγέθους αφορούν την ιστορική έκδοση· για νέο firmware ακολούθησε την παραπάνω αντιστοίχιση.
