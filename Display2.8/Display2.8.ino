/*
 * ESP32-2432S028 / CYD2USB 2.8" - direct 4 touch tabs, OFFLINE TEST
 * Repository: ApostolosGit/ESP32/Display2.8
 * Flash: ESP32 Dev Module, 4MB, DIO. No Wi-Fi or MQTT credentials.
 *
 * Requires Arduino libraries:
 *   GFX Library for Arduino (moononournation)
 *   XPT2046_Touchscreen (Paul Stoffregen)
 *
 * LCD pins: SCK 14, MOSI 13, MISO 12, CS 15, DC 2, BL 21
 * Touch pins: SCK 25, MOSI 32, MISO 39, CS 33, IRQ 36
 *
 * CYD2USB often uses ST7789 (default). If display stays white,
 * change LCD_ST7789 to 0 and rebuild for ILI9341.
 *
 * Flash backup FIRST. This sketch will replace the factory demo.
 */
#include <Arduino.h>
#include <SPI.h>
#include <Arduino_GFX_Library.h>
#include <XPT2046_Touchscreen.h>

#define LCD_ST7789 1
#define LCD_BL 21
#define TOUCH_CS 33
#define TOUCH_IRQ 36

Arduino_DataBus *lcdBus = new Arduino_ESP32SPI(2, 15, 14, 13, 12);
#if LCD_ST7789
Arduino_GFX *lcd = new Arduino_ST7789(lcdBus, GFX_NOT_DEFINED, 1, true, 240, 320);
#else
Arduino_GFX *lcd = new Arduino_ILI9341(lcdBus, GFX_NOT_DEFINED, 1);
#endif

XPT2046_Touchscreen touch(TOUCH_CS, TOUCH_IRQ);
const char *tabNames[] = {"DDS", "JSY", "VENT", "BOILER"};
int selectedTab = 0;
bool touchHeld = false;

// RGB565 theme
const uint16_t BG = 0x0882;
const uint16_t PANEL = 0x1265;
const uint16_t PANEL2 = 0x2368;
const uint16_t ACTIVE = 0x05B0;
const uint16_t CYAN2 = 0x67FD;
const uint16_t WHITE2 = 0xFFFF;
const uint16_t MUTED = 0xA5B5;

void printAt(int x, int y, const char *s, uint8_t size, uint16_t color) {
  lcd->setTextSize(size);
  lcd->setTextColor(color);
  lcd->setCursor(x, y);
  lcd->print(s);
}

void drawRow(int y, const char *label, const char *value) {
  printAt(19, y, label, 2, MUTED);
  printAt(225, y, value, 2, WHITE2);
}

void renderTab() {
  lcd->fillScreen(BG);
  lcd->fillRect(0, 0, 320, 35, PANEL);
  printAt(10, 9, "ENERGY MONITOR", 2, CYAN2);
  printAt(270, 13, "TEST", 1, WHITE2);

  for (int i = 0; i < 4; ++i) {
    int x = i * 80 + 2;
    lcd->fillRoundRect(x, 41, 76, 40, 5, i == selectedTab ? ACTIVE : PANEL2);
    printAt(x + (i == 3 ? 6 : 14), 54, tabNames[i], 2, WHITE2);
  }

  lcd->fillRoundRect(9, 92, 302, 112, 8, PANEL);
  switch (selectedTab) {
    case 0:
      printAt(19, 106, "DDS238", 2, CYAN2);
      printAt(19, 141, "Power: -- W", 2, WHITE2);
      printAt(19, 170, "Voltage: -- V", 2, WHITE2);
      break;
    case 1:
      printAt(19, 106, "JSY-MK333", 2, CYAN2);
      drawRow(133, "Phase R", "--");
      drawRow(158, "Phase S", "--");
      drawRow(183, "Phase T", "--");
      break;
    case 2:
      printAt(19, 106, "VENTILATION", 2, CYAN2);
      printAt(19, 141, "Outside: -- C", 2, WHITE2);
      printAt(19, 170, "Inside:  -- C", 2, WHITE2);
      break;
    case 3:
      printAt(19, 106, "BOILER", 2, CYAN2);
      printAt(19, 141, "T1: --  T2: --", 2, WHITE2);
      printAt(19, 170, "T3: --  T4: --", 2, WHITE2);
      break;
  }

  printAt(11, 215, "OFFLINE DEMO - NO MQTT", 1, MUTED);
  printAt(11, 227, "Tap a tab. Serial: 115200", 1, MUTED);
}

void setup() {
  Serial.begin(115200);
  pinMode(LCD_BL, OUTPUT);
  digitalWrite(LCD_BL, HIGH);

  if (!lcd->begin()) {
    Serial.println("LCD initialization failed");
    while (true) delay(1000);
  }
#if LCD_ST7789
  lcd->invertDisplay(true);  // Typical CYD2USB ST7789 inversion
#endif

  SPI.begin(25, 39, 32, TOUCH_CS);
  touch.begin();
  touch.setRotation(1);

  Serial.println("CYD 2.8-inch 4-tab test started (no splash screen)");
  Serial.println("Touch tab positions: x=0..319, y=41..81");
  renderTab();
}

void loop() {
  if (!touch.touched()) {
    touchHeld = false;
    delay(15);
    return;
  }
  if (touchHeld) {
    delay(15);
    return;
  }
  touchHeld = true;
  TS_Point p = touch.getPoint();
  if (p.z < 100) return;

  // Preliminary resistive touch calibration for rotation=1.
  // Check raw coordinates in Serial Monitor and adjust if necessary.
  int x = constrain((int)map(p.x, 250, 3800, 0, 319), 0, 319);
  int y = constrain((int)map(p.y, 250, 3800, 0, 239), 0, 239);
  Serial.printf("Touch raw=(%d,%d) z=%d -> screen=(%d,%d)\n",
                p.x, p.y, p.z, x, y);

  if (y >= 38 && y <= 85) {
    int newTab = constrain(x / 80, 0, 3);
    if (newTab != selectedTab) {
      selectedTab = newTab;
      renderTab();
    }
  }
  delay(25);
}
