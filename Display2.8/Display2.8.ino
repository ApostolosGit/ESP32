/*
  Energy Monitor ver0.15 - 2.8" CYD ESP32-2432S028 (dual USB) / 320x240 landscape.
  DDS: Metritis DEI Artemida; JSY: Nestou 5A; Ventilation; Boiler.
  Live read-only dashboard mirroring the MQTT.app state schema.
  Factory backup before flashing. Values are NEVER hard-coded samples.
  AP setup: OPEN SSID ESP-Display28, web PIN 12134, 15 min session.
  AP URL is shown on-screen (normally http://192.168.4.1).
  Sensitive Wi-Fi/MQTT settings stay in on-device Preferences, never in source.
  MQTT TLS 8883, trusted ISRG Root X1, no control commands; refresh requests only.
  Libraries: Arduino_GFX 1.4.9, XPT2046_Touchscreen, PubSubClient, ArduinoJson 6.
*/
#define FW_VERSION "ver0.15"
#define COMPAT_PLATFORM "CommonPlatform-1.06 / ESP32 classic adapter"
#define DEVICE_ID_PREFIX "display28-"
#define OTA_PROFILE_ID "esp32-display28-st7789"

#include <Arduino.h>
#include <SPI.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <Arduino_GFX_Library.h>
#include <XPT2046_Touchscreen.h>
#include <time.h>
#include <math.h>
#include <Update.h>
#include <esp_heap_caps.h>
#include <esp_wifi.h> // Native API: TX power limit after radio start
#include <HTTPClient.h> // HTTPS OTA file streaming from approved GitHub repo
#include "TrustedCA.h"
#include "GithubCA.h"

Arduino_DataBus *db = new Arduino_ESP32SPI(2, 15, 14, 13, 12);
Arduino_GFX *lcd = new Arduino_ST7789(db, GFX_NOT_DEFINED, 1, true, 240, 320);
XPT2046_Touchscreen touch(33, 36);
WiFiClientSecure tls;
PubSubClient mqtt(tls);
WebServer webServer(80);
DNSServer portalDns;
Preferences settings;

const uint16_t UI_BG = 0x0000, UI_PANEL = 0x0820, UI_BORDER = 0x21A5;
const uint16_t UI_WHITE = 0xFFFF, UI_MUTED = 0x9CF3, UI_CYAN = 0x07DB;
const uint16_t UI_YELLOW = 0xFEA0, UI_RED = 0xF9A6, UI_GREEN = 0x05E5;
const uint16_t UI_ACTIVE = 0x04B4, UI_TABOFF = 0x0844;

const char *BROKER_DEFAULT = "b7e93fa24c0c4c86a995afecd61e93f3.s1.eu.hivemq.cloud";
String wifiSsid, wifiPass, mqttUser, mqttPass, broker, apSsid;
float euroPerKwh = 0.25f;
bool setupMode = false;
uint8_t currentPage = 0;
uint32_t lastMqttTry = 0, lastWifiTry = 0, lastPoll = 0, lastDraw = 0;
uint32_t lastRx[4] = {0,0,0,0};
// Text-only delta redraw: no 320x240 framebuffer and no per-MQTT full clear.
struct ValueCell {
  char last[48];
  uint16_t color;
  bool valid;
};
ValueCell visibleCells[24]={};
int16_t visibleLayout=-1;
bool uiRefreshPending=false;
const uint32_t UI_REFRESH_MIN_MS=250UL;
uint32_t fullScreenDraws=0,partialScreenDraws=0,unchangedCells=0;
String ids[4];
bool hasState[4] = {false,false,false,false};
StaticJsonDocument<6144> state[4];
StaticJsonDocument<6144> incomingState; // Global: avoid 6 KB stack allocation in MQTT callback

// Live MQTT source diagnostics, including retained status/health topics.
// These are OBSERVED frames; no synthetic sensor values are generated.
struct SourceDebug {
  uint32_t statusAt=0,healthAt=0,stateAt=0,requestAt=0;
  uint32_t statusCount=0,healthCount=0,stateCount=0,requests=0,requestFails=0;
  bool statusKnown=false,statusOnline=false;
};
SourceDebug sourceDebug[4];
bool settingsOpen=false;
uint32_t setupConfirmUntil=0;
uint8_t settingsPane=0;
// Network manager: 1=WiFi settings, 2=choose router, 3=masked keyboard.
// Wi-Fi passwords never appear as plaintext on the TFT, serial or MQTT.
uint8_t wifiUiMode=0,wifiListOffset=0,wifiFoundCount=0;
String wifiFoundSsid[18],wifiSelectedSsid,wifiDraftPassword;
int32_t wifiFoundDbm[18]={};
bool wifiFoundSecured[18]={};
bool wifiScanBusy=false,wifiScanQueued=false;
uint32_t wifiScanAt=0,wifiUiConfirmUntil=0;
bool wifiPasswordChanged=false,wifiHasSavedPassword=false,wifiUppercase=false,wifiSymbols=false;
char wifiUiMessage[52]={};
void openWifiManager();
void handleWifiManagerTouch(int x,int y);
void drawWifiManager(bool full);
void serviceWifiManager();
void rememberRouterCredentials(const String &ssid,const String &password);
bool findRouterCredentials(const String &ssid,String &password);
void clearRememberedRouters();
uint32_t mqttPackets=0,mqttJsonErrors=0,mqttUnknown=0,mqttRequestTotal=0;
uint32_t lastMqttPacketAt=0;
char lastMqttTopic[100]={0},lastMqttError[55]={0},lastUnknownTopic[100]={0};
bool mqttSubs[3]={false,false,false};
uint32_t lastTouchRawX=0,lastTouchRawY=0;
uint32_t nextProbeAt[4]={0,0,0,0};
uint32_t nextSubRetryAt=0;
uint32_t nextDisplayHealthAt=0, restartAt=0;
bool displayStateRequested=false, displayOtaBusy=false;
struct DisplayOtaJob {bool pending=false;size_t size=0;String md5,url;} displayOtaJob;

// Existing source devices publish their own history via MQTT:
// Energy: admin/history with history_get|token|0|offset pages.
// VENT: vent/history; BOILER: boiler/history, current + previous 24h,
// eight quarter-hour samples per packet. No synthetic sensor history.
constexpr uint16_t CHART_ENERGY_CAPACITY=256;
struct EnergyHistoryPoint {
  uint32_t epoch=0,generation=0;
  uint64_t importWh=0,exportWh=0;
};
EnergyHistoryPoint chartEnergy[CHART_ENERGY_CAPACITY];
uint16_t chartEnergyCount=0;
float chartTemperatures[4][192]; // 48 hours, four channels, 15-minute steps.
bool chartTempBlocks[2][12]={{false}}; // current/previous: 12 x 8 points.
bool chartSubs[3]={false,false,false};
bool chartOpen=false,chartLoading=false,chartComplete=false,chartDirty=false;
uint8_t chartPage=0,chartPhase=0;
uint32_t chartToken=0,chartAnchorEpoch=0,chartRequestAt=0,chartLastDataAt=0;
uint32_t chartRequests=0,chartFrames=0;
long chartOffset=0,chartRecentFirst=-1;
uint16_t chartPageRows=0;
bool chartEnergyBegun=false;
char chartError[48]={0};
void startHistoryChart(uint8_t page);
void handleChartMessage(uint8_t deviceType,const String &suffix,
                        const byte *payload,unsigned int length);
void drawHistoryChart(bool full);
void serviceHistoryChart();

bool live(int pg); // Forward declaration: settings JSON is above rendering code
void requestAll(); // Settings UI and MQTT callback can schedule polling


// CommonPlatform 1.06-compatible network lifecycle for classic ESP32.
// Deliberately NOT the C3-only platform source (target-specific adapters differ).
bool portalActive=false, portalForced=false, routesReady=false, timeConfigured=false;
bool apDnsRunning=false, pendingPortalStop=false, portalRecoveryMode=false;
const char AP_WEB_PIN[] = "12134"; // CommonPlatform 1.06
const uint32_t AP_WEB_SESSION_MS = 15UL*60UL*1000UL;
const uint32_t AP_PIN_COOLDOWN_MS = 30000UL;
char apWebSessionToken[33] = {};
uint32_t apWebSessionStartedMs=0, apPinLockedUntil=0;
uint8_t apPinFailures=0;
uint32_t portalStartMs=0, portalHealthMs=0, portalRetryAt=0, portalAttempts=0, portalFailures=0;
uint32_t portalRadioRenewedAt=0,portalRadioRestarts=0;
uint32_t nextRecoveryStaAttempt=0,lastRecoveryStaAttempt=0,lastPortalAccessAt=0;
uint32_t recoveryRouterConnectedAt=0;
const uint32_t RECOVERY_ROUTER_STABLE_MS=15000UL;
uint32_t recoveryStaAttempts=0,recoveryStaFailures=0;
int32_t recoveryStaLastError=0;
const uint32_t RECOVERY_STA_RETRY_MS=60000UL; // Less scanning helps keep AP discoverable.
const uint32_t RECOVERY_WEB_GRACE_MS=20000UL; // Avoid interrupting portal form submissions.
bool wifiEverConnected=false, mqttEverConnected=false, otaImageComplete=false;
bool wifiCurrentlyConnected=false;
uint32_t wifiFailures=0, mqttFailures=0, wifiReconnects=0, mqttReconnects=0;
uint32_t disconnectedSince=0, nextWifiAttempt=0, nextMqttAttempt=0;
uint32_t lastTxTune=0, lastHealthLine=0, lastNtpAttempt=0;
uint8_t wifiRetryIdx=0, mqttRetryIdx=0, txLevel=0;
bool txPowerVerified=false;
int32_t lastTxError=0;

// ESP-IDF max requested TX 80/4=20 dBm; actual radio power may be lower.
const int8_t TX_MAX_QDBM=80;
const wifi_power_t TX_STEPS[] = {WIFI_POWER_8_5dBm, WIFI_POWER_11dBm,
  WIFI_POWER_13dBm, WIFI_POWER_15dBm, WIFI_POWER_19_5dBm};
const uint16_t TX_TENTHS[] = {85,110,130,150,200};
const uint32_t WIFI_RETRY_MS[] = {10000,15000,30000,60000};
const uint32_t MQTT_BOOT_RETRY_MS[] = {10000,30000,60000,120000,300000};
const uint32_t MQTT_RUN_RETRY_MS[] = {5000,10000,30000};
bool dueAt(uint32_t target) {return (int32_t)(millis()-target)>=0;}
String deviceId() {return String(DEVICE_ID_PREFIX)+String((uint32_t)ESP.getEfuseMac(),HEX);}
bool setTxLevel(uint8_t level,const char *reason) {
  if(level>4)level=4;
  const int8_t desired=level==4?TX_MAX_QDBM:(int8_t)TX_STEPS[level];
  esp_err_t err=ESP_FAIL;
  // IDF requires Wi-Fi to have *started*; mode() alone can race the START event.
  // Do not use Arduino WiFi.setTxPower() before a running STA/AP interface.
  for(uint8_t tryNo=0;tryNo<12;tryNo++) {
    err=esp_wifi_set_max_tx_power(desired);
    if(err==ESP_OK)break;
    if(tryNo<11)delay(50);
  }
  int8_t confirmed=0;
  esp_err_t readErr=(err==ESP_OK)?esp_wifi_get_max_tx_power(&confirmed):err;
  lastTxError=(err==ESP_OK)?(int32_t)readErr:(int32_t)err;
  if(err!=ESP_OK || readErr!=ESP_OK || confirmed>desired) {
    txPowerVerified=false;
    Serial.printf("[TX] FAILED %s: set=%s(0x%X) get=%s(0x%X) actual=%d desired=%d\n",
                  reason,esp_err_to_name(err),(unsigned)err,
                  esp_err_to_name(readErr),(unsigned)readErr,
                  (int)confirmed,(int)desired);
    return false;
  }
  txLevel=level;txPowerVerified=true;lastTxTune=millis();
  Serial.printf("[TX] verified limit %d.%d dBm (actual %d/4 dBm, %s)\n",
                TX_TENTHS[level]/10,TX_TENTHS[level]%10,(int)confirmed,reason);
  return true;
}
void retuneWifi() {
  // Always MAX for the Display; never reduce it after an RSSI reading.
  if(WiFi.status()!=WL_CONNECTED || portalActive || millis()-lastTxTune<60000UL)return;
  if(txLevel!=4 || !txPowerVerified)setTxLevel(4,"STA maximum");
  else lastTxTune=millis();
}
String localIp() {return WiFi.status()==WL_CONNECTED?WiFi.localIP().toString():String("--");}
String jsonStatus() {
  DynamicJsonDocument j(2048);
  j["fw"]=FW_VERSION;
  j["platform_compat"]="1.06";j["chip"]="ESP32-D0WD";j["device_id"]=deviceId();
  j["uptime_sec"]=millis()/1000UL;j["heap_free"]=ESP.getFreeHeap();
  j["largest_block"]=heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
  j["sta_connected"]=WiFi.status()==WL_CONNECTED;j["sta_ip"]=localIp();
  j["rssi"]=WiFi.status()==WL_CONNECTED?WiFi.RSSI():-127;
  j["tx_max_tenths_dbm"]=txPowerVerified?(int)TX_TENTHS[txLevel]:-1;
  j["tx_verified"]=txPowerVerified;
  j["tx_last_error"]=lastTxError;
  j["ap_active"]=portalActive;j["mqtt_connected"]=mqtt.connected();
  j["ap_radio_restarts"]=portalRadioRestarts;
  j["ap_ssid"]=apSsid;
  j["ap_mode"]=portalActive?(portalRecoveryMode?"AP+STA":"AP_ONLY"):"OFF";
  j["recovery_router_stable_sec"]=recoveryRouterConnectedAt && WiFi.status()==WL_CONNECTED?
    (millis()-recoveryRouterConnectedAt)/1000UL:0;
  j["ap_clients"]=portalActive?WiFi.softAPgetStationNum():0;
  j["recovery_sta_attempts"]=recoveryStaAttempts;
  j["recovery_sta_failures"]=recoveryStaFailures;
  j["recovery_sta_error"]=recoveryStaLastError;
  j["recovery_next_retry_sec"]=portalActive && portalRecoveryMode &&
    WiFi.status()!=WL_CONNECTED && !portalForced && nextRecoveryStaAttempt?
      (dueAt(nextRecoveryStaAttempt)?0:(nextRecoveryStaAttempt-millis())/1000UL):-1;
  j["display_ota_busy"]=displayOtaBusy;
  j["wifi_ui_mode"]=wifiUiMode;
  j["wifi_scan_busy"]=wifiScanBusy;
  j["wifi_scan_found"]=wifiFoundCount;
  j["ap_ip"]=portalActive?WiFi.softAPIP().toString():"";
  j["ap_pin_session_sec_left"]=portalActive && apWebSessionToken[0] &&
    millis()-apWebSessionStartedMs < AP_WEB_SESSION_MS ?
    (AP_WEB_SESSION_MS-(millis()-apWebSessionStartedMs))/1000UL : 0;
  j["portal_attempts"]=portalAttempts;j["portal_failures"]=portalFailures;
  j["ntp_ready"]=(time(nullptr)>1600000000);
  j["wifi_reconnect_attempts"]=wifiReconnects;
  j["mqtt_reconnect_attempts"]=mqttReconnects;
  j["wifi_failures"]=wifiFailures;j["mqtt_failures"]=mqttFailures;
  j["ota_profile"]=OTA_PROFILE_ID;
  j["mqtt_packets"]=mqttPackets;
  j["mqtt_json_errors"]=mqttJsonErrors;
  j["mqtt_unknown_topics"]=mqttUnknown;
  j["mqtt_requests"]=mqttRequestTotal;
  j["mqtt_last_topic"]=lastMqttTopic;
  j["mqtt_last_error"]=lastMqttError;
  j["mqtt_sub_state"]=mqttSubs[0];j["mqtt_sub_status"]=mqttSubs[1];
  j["mqtt_sub_health"]=mqttSubs[2];
  JsonArray ages=j.createNestedArray("measurement_age_sec");
  for(int i=0;i<4;i++)ages.add(hasState[i]?(millis()-lastRx[i])/1000UL:-1);
  String out;serializeJson(j,out);return out;
}
// Same PIN + 15-minute cookie session design as CommonPlatform 1.06.
// The fixed PIN is intentionally shared with that platform. Open Wi-Fi
// does not encrypt HTTP, so limit usage to trusted physical surroundings.
void resetApWebSession() {
  apWebSessionToken[0]='\0';
  apWebSessionStartedMs=0;
}
bool apWebSessionValid() {
  if(!apWebSessionToken[0])return false; // PIN session works on both LAN and recovery AP.
  if(millis()-apWebSessionStartedMs>=AP_WEB_SESSION_MS) {
    resetApWebSession();return false;
  }
  const String cookies=webServer.header("Cookie");
  const String expected="EMSESSION="+String(apWebSessionToken);
  int start=0;
  while(start<(int)cookies.length()) {
    int pos=cookies.indexOf(';',start);
    if(pos<0)pos=cookies.length();
    String part=cookies.substring(start,pos);part.trim();
    if(part==expected)return true;
    start=pos+1;
  }
  return false;
}
void sendPinPage(int status=200) {
  lastPortalAccessAt=millis();
  webServer.sendHeader("Cache-Control","no-store");
  webServer.send(status,"text/html; charset=utf-8",
    "<!doctype html><html><head><meta name='viewport' "
    "content='width=device-width,initial-scale=1'></head>"
    "<body style='background:#081522;color:#fff;font:20px Arial;padding:16px'>"
    "<h3>Energy Monitor PIN</h3><p>Enter your device PIN.</p>"
    "<form method='post' action='/ap-login'>"
    "<input name='pin' type='password' inputmode='numeric' "
    "autocomplete='off' required autofocus>"
    "<button type='submit'>Open</button></form></body></html>");
}
bool requireApWebPin() {
  if(!portalActive && WiFi.status()!=WL_CONNECTED)return false;
  if(portalActive)lastPortalAccessAt=millis();
  if(apWebSessionValid())return true;
  sendPinPage(webServer.method()==HTTP_GET?200:403);
  return false;
}
void handleApWebLogin() {
  if(portalActive)lastPortalAccessAt=millis();
  if(!portalActive && WiFi.status()!=WL_CONNECTED){webServer.send(404,"text/plain","Not found");return;}
  if(apPinLockedUntil && !dueAt(apPinLockedUntil)) {
    webServer.send(429,"text/plain","PIN temporarily locked. Try later.");
    return;
  }
  if(webServer.arg("pin")!=AP_WEB_PIN) {
    if(++apPinFailures>=5) {
      apPinFailures=0;apPinLockedUntil=millis()+AP_PIN_COOLDOWN_MS;
    }
    sendPinPage(403);return;
  }
  apPinFailures=0;apPinLockedUntil=0;
  snprintf(apWebSessionToken,sizeof(apWebSessionToken),
    "%08lX%08lX%08lX%08lX",
    (unsigned long)esp_random(),(unsigned long)esp_random(),
    (unsigned long)esp_random(),(unsigned long)esp_random());
  apWebSessionStartedMs=millis();
  String cookie=String("EMSESSION=")+apWebSessionToken+
    "; Path=/; HttpOnly; SameSite=Strict; Max-Age=900";
  webServer.sendHeader("Cache-Control","no-store");
  webServer.sendHeader("Set-Cookie",cookie);
  webServer.sendHeader("Location","/",true);
  webServer.send(303,"text/plain","");
}
IPAddress portalIp() {
  // Separate AP and STA subnets. Never reuse the home router IP range.
  if(WiFi.status()==WL_CONNECTED && WiFi.localIP()[0]==192 &&
     WiFi.localIP()[1]==168 && WiFi.localIP()[2]==4)
    return IPAddress(192,168,8,1);
  return IPAddress(192,168,4,1);
}
// Detailed non-secret diagnostic data for the settings web endpoints.
String mqttDebugJson() {
  DynamicJsonDocument j(4096);
  j["firmware"]=FW_VERSION;
  j["display_full_redraws"]=fullScreenDraws;
  j["display_partial_redraws"]=partialScreenDraws;
  j["display_unchanged_cells_skipped"]=unchangedCells;
  j["mqtt_connected"]=mqtt.connected();
  j["broker"]=broker;
  j["broker_port"]=8883;
  j["client_id"]=deviceId();
  j["state"]=mqtt.state();
  j["packets_received"]=mqttPackets;
  j["json_failures"]=mqttJsonErrors;
  j["unrecognized_topics"]=mqttUnknown;
  j["requests_sent"]=mqttRequestTotal;
  j["last_topic"]=lastMqttTopic;
  j["last_unknown_topic"]=lastUnknownTopic;
  j["last_error"]=lastMqttError;
  j["state_subscribed"]=mqttSubs[0];
  j["status_subscribed"]=mqttSubs[1];
  j["health_subscribed"]=mqttSubs[2];
  j["last_message_age_seconds"]=lastMqttPacketAt?(int)((millis()-lastMqttPacketAt)/1000UL):-1;
  JsonArray list=j.createNestedArray("devices");
  const char *names[4]={"DDS Artemida","JSY Nestou 5A","Ventilation","Boiler"};
  for(int i=0;i<4;i++) {
    JsonObject o=list.createNestedObject();
    const SourceDebug &d=sourceDebug[i];
    o["label"]=names[i];o["device_id"]=ids[i];
    o["status_known"]=d.statusKnown;o["status_online"]=d.statusOnline;
    o["status_messages"]=d.statusCount;o["health_messages"]=d.healthCount;
    o["state_messages"]=d.stateCount;o["request_messages"]=d.requests;
    o["request_failures"]=d.requestFails;
    o["last_status_age_sec"]=d.statusAt?(int)((millis()-d.statusAt)/1000UL):-1;
    o["last_health_age_sec"]=d.healthAt?(int)((millis()-d.healthAt)/1000UL):-1;
    o["last_state_age_sec"]=d.stateAt?(int)((millis()-d.stateAt)/1000UL):-1;
    o["state_usable_now"]=live(i);
  }
  String out;serializeJson(j,out);return out;
}
// ---------- Greek uppercase in the built-in 5x7 pixel typeface ----------
// Map visually identical Greek capitals to Latin, draw remaining glyphs.
uint32_t nextUtf8(const char *&s) {
  uint8_t c = (uint8_t)*s++; if (c < 128) return c;
  if ((c & 0xE0) == 0xC0 && *s) {
    uint8_t b = (uint8_t)*s++; return ((c & 31) << 6) | (b & 63);
  }
  if ((c & 0xF0) == 0xE0 && s[0] && s[1]) {
    uint8_t b = (uint8_t)*s++, d = (uint8_t)*s++;
    return ((c & 15) << 12) | ((b & 63) << 6) | (d & 63);
  }
  return '?';
}
uint32_t uppercaseGreek(uint32_t c) {
  if(c >= 0x3B1 && c <= 0x3C9) c -= 32;
  if(c == 0x3C2) c = 0x3A3;
  switch(c) {
    case 0x386: return 0x391; case 0x388: return 0x395;
    case 0x389: return 0x397; case 0x38A: return 0x399;
    case 0x38C: return 0x39F; case 0x38E: return 0x3A5;
    case 0x38F: return 0x3A9;
    case 0x3AC: return 0x391; case 0x3AD: return 0x395;
    case 0x3AE: return 0x397; case 0x3AF: return 0x399;
    case 0x3CC: return 0x39F; case 0x3CD: return 0x3A5;
    case 0x3CE: return 0x3A9;
  }
  return c;
}
char latinGreek(uint32_t c) {
  switch(c) {
    case 0x391: return 'A'; case 0x392: return 'B'; case 0x395: return 'E';
    case 0x396: return 'Z'; case 0x397: return 'H'; case 0x399: return 'I';
    case 0x39A: return 'K'; case 0x39C: return 'M'; case 0x39D: return 'N';
    case 0x39F: return 'O'; case 0x3A1: return 'P'; case 0x3A4: return 'T';
    case 0x3A5: return 'Y'; case 0x3A7: return 'X';
    default: return 0;
  }
}
const uint8_t *specialGlyph(uint32_t c) {
  static const uint8_t G[]={0x1F,0x10,0x10,0x10,0x10,0x10,0x10}; // Γ
  static const uint8_t D[]={0x04,0x0A,0x11,0x11,0x11,0x1F,0x00}; // Δ
  static const uint8_t TH[]={0x0E,0x11,0x15,0x15,0x15,0x11,0x0E}; // Θ
  static const uint8_t L[]={0x04,0x0A,0x0A,0x11,0x11,0x1F,0x00}; // Λ
  static const uint8_t XI[]={0x1F,0x00,0x0E,0x00,0x0E,0x00,0x1F}; // Ξ
  static const uint8_t GLYPH_PI[]={0x1F,0x11,0x11,0x11,0x11,0x11,0x11}; // Π
  static const uint8_t SG[]={0x1F,0x10,0x08,0x04,0x08,0x10,0x1F}; // Σ
  static const uint8_t PH[]={0x04,0x0E,0x15,0x15,0x15,0x0E,0x04}; // Φ
  static const uint8_t GLYPH_PSI[]={0x15,0x15,0x15,0x0E,0x04,0x04,0x04}; // Ψ
  static const uint8_t OM[]={0x0E,0x11,0x11,0x11,0x0A,0x1B,0x00}; // Ω
  static const uint8_t EURO[]={0x0E,0x11,0x1E,0x08,0x1E,0x11,0x0E};
  switch(c) {
    case 0x393: return G; case 0x394: return D;
    case 0x398: return TH; case 0x39B: return L;
    case 0x39E: return XI; case 0x3A0: return GLYPH_PI;
    case 0x3A3: return SG; case 0x3A6: return PH;
    case 0x3A8: return GLYPH_PSI; case 0x3A9: return OM;
    case 0x20AC: return EURO;
    default: return nullptr;
  }
}
void txt(int x,int y,const String &s,uint16_t color=UI_WHITE,uint8_t scale=1) {
  lcd->setTextColor(color);
  lcd->setTextSize(scale);
  const char *p=s.c_str();
  while(*p && x < 319) {
    uint32_t c=uppercaseGreek(nextUtf8(p));
    char a=latinGreek(c);
    if (c < 127) a=(char)c;
    if (a) {
      lcd->setCursor(x,y); lcd->write((uint8_t)a);
    } else if (c == 0xB0) {
      lcd->drawCircle(x+2*scale,y+2*scale,2*scale,color);
    } else {
      const uint8_t *rows=specialGlyph(c);
      if(rows) for(int row=0;row<7;row++)
        for(int col=0;col<5;col++)
          if(rows[row] & (1 << (4-col)))
            lcd->fillRect(x+col*scale,y+row*scale,scale,scale,color);
      else {lcd->setCursor(x,y);lcd->write((uint8_t)'?');}
    }
    x += 6*scale;
  }
}
String fmt(double v,int digits=1) {
  if(!isfinite(v)) return "--";
  // Greek numbers: 35.279,72  (thousands '.', decimals ',').
  bool neg=v < 0; if(neg) v=-v;
  double m=pow(10.0,digits);
  uint64_t total=(uint64_t)floor(v*m+0.500001);
  uint64_t whole=total/(uint64_t)m, fraction=total%(uint64_t)m;
  String n=String((unsigned long)whole), result="";
  for(size_t i=0;i<n.length();i++) {
    if(i>0 && (n.length()-i)%3==0) result+='.';
    result+=n[i];
  }
  if(digits) {
    result+=',';
    char b[8];snprintf(b,sizeof(b),"%0*lu",digits,(unsigned long)fraction);
    result+=b;
  }
  return (neg?"-":"")+result;
}
double num(JsonVariantConst v) {
  if(v.isNull()) return NAN;
  if(v.is<float>() || v.is<double>() || v.is<int>() || v.is<long>() ||
     v.is<unsigned int>() || v.is<unsigned long>()) return v.as<double>();
  return NAN;
}
double numberOf(int page,const char* field) { return num(state[page][field]); }
String valueOf(int page,const char* field,int digits=1) { return fmt(numberOf(page,field),digits); }
bool live(int pg) {
  return mqtt.connected() && hasState[pg] && millis()-lastRx[pg] < 120000;
}
String shown(int pg,const char *field,int digits=1) {
  return live(pg)?valueOf(pg,field,digits):"--";
}
String isoDay(int pg) {
  if(!live(pg)) return "--";
  String s=state[pg]["utility_date"] | "";
  if(s.length()<10) return "--";
  return s.substring(8,10)+"/"+s.substring(5,7);
}
uint16_t powerColor(double n) {
  return !isfinite(n) ? UI_MUTED : n<0 ? UI_YELLOW : n>0 ? UI_RED : UI_WHITE;
}
// A unique cell ID identifies a variable value on the currently displayed tab.
// Only the changed cell background is erased; labels/cards never blink.
void drawValue(uint8_t slot,int x,int y,int w,int h,const String &value,
               uint16_t fg,uint8_t size,uint16_t background=UI_PANEL) {
  if(slot>=24)return;
  ValueCell &cell=visibleCells[slot];
  if(cell.valid && cell.color==fg && strcmp(cell.last,value.c_str())==0) {
    unchangedCells++;
    return;
  }
  if(cell.valid)lcd->fillRect(x,y,w,h,background);
  txt(x,y,value,fg,size);
  snprintf(cell.last,sizeof(cell.last),"%s",value.c_str());
  cell.color=fg;
  cell.valid=true;
}
void card(int x,int y,int w,int h) {
  lcd->fillRoundRect(x,y,w,h,7,UI_PANEL);
  lcd->drawRoundRect(x,y,w,h,7,UI_BORDER);
}
void line(int x,int y,const String &label,const String &value,
          uint16_t valueColor=UI_WHITE) {
  txt(x,y,label,UI_MUTED,1);
  txt(x+103,y-3,value,valueColor,2);
}
// A small drawn gear is always present, including the AP setup screen.
void drawGear() {
  const int cx=305,cy=14;
  lcd->fillRoundRect(291,1,28,27,5,settingsOpen?UI_ACTIVE:UI_TABOFF);
  lcd->drawCircle(cx,cy,6,UI_WHITE);
  lcd->drawCircle(cx,cy,2,UI_CYAN);
  for(int i=0;i<8;i++) {
    const float a=i*0.785398163f;
    int x1=cx+(int)roundf(cosf(a)*8.0f),y1=cy+(int)roundf(sinf(a)*8.0f);
    int x2=cx+(int)roundf(cosf(a)*10.0f),y2=cy+(int)roundf(sinf(a)*10.0f);
    lcd->drawLine(x1,y1,x2,y2,UI_WHITE);
  }
}
void topbar() {
  lcd->fillScreen(UI_BG);
  txt(7,3,"ENERGY MONITOR",UI_CYAN,2);
  txt(232,4,FW_VERSION,UI_CYAN,1);
  txt(248,18,mqtt.connected()?"MQTT":"OFF",mqtt.connected()?UI_GREEN:UI_RED,1);
  const String tabNames[4]={"ΑΡΤΕΜΙΔΑ","ΝΕΣΤΟΥ 5Α","VENT","BOILER"};
  const String debugNames[4]={"NET","MQTT","DEV","SYS"};
  for(int i=0;i<4;i++) {
    int x=i*80+2;
    bool selected=settingsOpen?(settingsPane==i):(currentPage==i);
    lcd->fillRoundRect(x,29,76,31,5,selected?UI_ACTIVE:UI_TABOFF);
    txt(x+4,40,settingsOpen?debugNames[i]:tabNames[i],UI_WHITE,1);
  }
  drawGear();
}
void footer() {
  lcd->fillRect(0,226,320,14,UI_BG);
  if(chartOpen) {
    txt(7,231,"TAP ANYWHERE TO GO BACK",UI_CYAN,1);
    return;
  }
  if(settingsOpen) {
    txt(7,231,settingsPane==2?"TAP DEVICE ROW / GEAR BACK":"SETTINGS / GEAR TO RETURN",UI_MUTED,1);
    return;
  }
  uint16_t c=live(currentPage)?UI_GREEN:UI_MUTED;
  String m=live(currentPage)?"LIVE":(sourceDebug[currentPage].statusKnown &&
    !sourceDebug[currentPage].statusOnline?"OFFLINE":"ΑΝΑΜΟΝΗ ΔΕΔΟΜΕΝΩΝ");
  txt(7,231,m,c,1);
  if(live(currentPage)) txt(255,231,String((millis()-lastRx[currentPage])/1000)+"s",UI_MUTED,1);
}
void ddsPage(bool full) {
  if(full) {
    txt(8,65,"ΜΕΤΡΗΤΗΣ ΔΕΗ ΑΡΤΕΜΙΔΑ",UI_WHITE,1);
    card(5,79,310,140);
    txt(12,88,"ΙΣΧΥΣ",UI_MUTED,1);
    txt(275,92,"W",UI_MUTED,2);
    txt(12,120,"ΔΙΑΦΟΡΑ",UI_MUTED,1);
    txt(216,122,"kWh",UI_MUTED,1);
    txt(12,154,"ΕΚΤΙΜΗΣΗ",UI_MUTED,1);
    txt(254,155,"kWh",UI_MUTED,1);
    txt(12,190,"ΤΕΛ. ΔΕΗ",UI_MUTED,1);
    txt(242,189,"kWh",UI_MUTED,1);
  }
  const bool active=live(0);
  const double p=active?numberOf(0,"power"):NAN;
  drawValue(0,102,82,170,25,fmt(p,0),powerColor(p),3);
  const bool diffOk=active && state[0]["has_utility"]==true &&
                    state[0]["estimate_valid"]==true;
  drawValue(1,106,116,106,18,diffOk?shown(0,"z_diff",2):"--",UI_RED,2);
  const double d=diffOk?numberOf(0,"z_diff"):NAN;
  drawValue(2,256,122,58,9,isfinite(d)?"("+fmt(d*euroPerKwh,2)+"€)":"",UI_MUTED,1);
  drawValue(3,106,149,144,18,active && state[0]["estimate_valid"]==true?
    shown(0,"z_now",2):"--",UI_WHITE,2);
  drawValue(4,106,182,132,18,active && state[0]["has_utility"]==true?
    shown(0,"z_ref",0):"--",UI_WHITE,2);
  drawValue(5,268,206,45,10,isoDay(0),UI_CYAN,1);
}
void jsyPage(bool full) {
  if(full) {
    txt(7,64,"ΝΕΣΤΟΥ 5Α",UI_WHITE,1);
    txt(123,64,"ΣΥΝΟΛΙΚΗ ΙΣΧΥΣ",UI_MUTED,1);
    txt(108,80,"W",UI_MUTED,1);
  }
  double total=live(1)?numberOf(1,"power_total"):NAN;
  if(isfinite(total) && total>0 && live(1) &&
     (state[1]["power_direction"].as<int>() & 0x08))total=-total;
  drawValue(0,8,77,96,19,fmt(total,0),powerColor(total),2,UI_BG);
  for(int i=0;i<3;i++) {
    int x=4+i*106;
    if(full) {
      card(x,99,101,44);
      txt(x+6,105,"ΦΑΣΗ "+String("RST"[i]),UI_WHITE,1);
    }
    String f="p"+String(i+1),dir="dir"+String(i+1);
    double power=live(1)?numberOf(1,f.c_str()):NAN;
    if(isfinite(power) && power>0 &&
       (String(state[1][dir] | "")=="REV" ||
       (String(state[1][dir] | "").isEmpty() &&
        numberOf(1,("i"+String(i+1)).c_str())<0)))power=-power;
    drawValue(1+i,x+5,118,90,20,fmt(power,0),powerColor(power),2);
  }
  for(int z=1;z<=2;z++) {
    int x=z==1?4:162,w=154;
    if(full) {
      card(x,148,w,75);
      txt(x+5,152,z==1?"Ζ1 ΑΚΡΙΒΟ":"Ζ2 ΟΙΚΟΝΟΜΙΚΟ",
          z==1?UI_WHITE:UI_CYAN,1);
      txt(x+90,170,"kWh",UI_MUTED,1);
      txt(x+59,182,"ΕΚΤ.",UI_MUTED,1);
      txt(x+5,196,"ΔΕΗ",UI_MUTED,1);
    }
    bool ok=live(1) && state[1]["has_utility"]==true &&
            state[1]["estimate_valid"]==true;
    String prefix="z"+String(z)+"_";
    double diff=ok?numberOf(1,(prefix+"diff").c_str()):NAN;
    const int slot=4+(z-1)*5;
    drawValue(slot,x+5,163,82,18,fmt(diff,2),UI_RED,2);
    drawValue(slot+1,x+5,182,51,9,isfinite(diff)?
      fmt(diff*euroPerKwh,2)+"€":"",UI_MUTED,1);
    drawValue(slot+2,x+90,182,60,9,live(1) &&
      state[1]["estimate_valid"]==true?
      shown(1,(prefix+"now").c_str(),2):"--",UI_WHITE,1);
    drawValue(slot+3,x+41,196,67,9,live(1) &&
      state[1]["has_utility"]==true?
      shown(1,(prefix+"ref").c_str(),0):"--",UI_WHITE,1);
    drawValue(slot+4,x+109,210,43,9,isoDay(1),UI_MUTED,1);
  }
}
void ventPage(bool full) {
  if(full)txt(8,65,"ΕΞΑΕΡΙΣΜΟΣ / VENTILATION",UI_WHITE,1);
  const char *keys[3]={"temp_outside","temp_inside","temp_middle"};
  const char *titles[3]={"ΕΞΩ","ΜΕΣΑ","ΕΝΔΙΑΜΕΣΑ"};
  for(int i=0;i<3;i++) {
    int x=4+i*106;
    if(full) {
      card(x,79,101,47);
      txt(x+6,85,titles[i],UI_MUTED,1);
    }
    drawValue(i,x+6,102,89,19,shown(2,keys[i],1)+"°C",UI_WHITE,2);
  }
  if(full) {
    card(4,132,312,65);
    txt(10,136,"DHT11 OUTSIDE",UI_CYAN,1);
    lcd->drawFastHLine(10,161,300,UI_BORDER);
    txt(10,166,"DHT11 INSIDE",UI_CYAN,1);
    card(4,200,312,24);
  }
  drawValue(3,150,136,73,9,shown(2,"dht_temp_outside",1)+"°C",UI_WHITE,1);
  drawValue(4,224,136,87,9,"H "+shown(2,"humidity_outside",1)+"%",UI_WHITE,1);
  drawValue(5,10,148,170,9,"AH "+shown(2,"absolute_humidity_outside",1)+"g/m3",UI_MUTED,1);
  drawValue(6,182,148,128,9,"FEELS "+shown(2,"feels_like_outside",1)+"°C",UI_MUTED,1);
  drawValue(7,150,166,73,9,shown(2,"dht_temp_inside",1)+"°C",UI_WHITE,1);
  drawValue(8,224,166,87,9,"H "+shown(2,"humidity_inside",1)+"%",UI_WHITE,1);
  drawValue(9,10,179,170,9,"AH "+shown(2,"absolute_humidity_inside",1)+"g/m3",UI_MUTED,1);
  drawValue(10,182,179,128,9,"FEELS "+shown(2,"feels_like_inside",1)+"°C",UI_MUTED,1);
  String mode=live(2)?String(state[2]["mode"]|"--"):"--";
  String fan=live(2)?(state[2]["fan_on"].as<bool>()?"ON":"OFF"):"--";
  String speed=live(2)?String(state[2]["speed"]|"--"):"--";
  String season=live(2)?String(state[2]["season"]|"--"):"--";
  bool safe=live(2) && state[2]["safe_mode"]==true;
  drawValue(11,10,207,214,9,"MODE "+mode+"  FAN "+fan+"  "+speed,
            safe?UI_RED:UI_WHITE,1);
  drawValue(12,227,207,83,9,season,UI_MUTED,1);
}
void boilerPage(bool full) {
  if(full)txt(8,65,"BOILER / ΘΕΡΜΟΚΡΑΣΙΕΣ",UI_WHITE,1);
  const char* labels[4]={"Θ1  GPIO5","Θ2  GPIO6","Θ3  GPIO7","Θ4  GPIO21"};
  for(int i=0;i<4;i++) {
    int x=(i%2)*157+4,y=79+(i/2)*72;
    if(full) {
      card(x,y,152,68);
      txt(x+9,y+7,labels[i],UI_MUTED,1);
    }
    String field="temp_"+String(i+1),statusField=field+"_status";
    bool disconnected=live(3) &&
      String(state[3][statusField] | "")=="not connected";
    String value=disconnected?"NC":shown(3,field.c_str(),1)+"°C";
    drawValue(i,x+9,y+28,132,20,value,
              disconnected?UI_RED:UI_WHITE,2);
  }
}
const char *debugNames[4]={"ΑΡΤΕΜΙΔΑ","ΝΕΣΤΟΥ 5Α","VENT","BOILER"};
String ageOf(uint32_t stamp) {
  return stamp?String((millis()-stamp)/1000UL)+"s":"--";
}
void settingsPage() {
  if(settingsPane==0) {
    txt(8,67,"NETWORK DEBUG",UI_CYAN,2);
    card(4,87,312,137);
    txt(11,94,"STA:",UI_MUTED,1);
    txt(74,94,WiFi.status()==WL_CONNECTED?"CONNECTED":"DISCONNECTED",
        WiFi.status()==WL_CONNECTED?UI_GREEN:UI_RED,1);
    txt(11,110,"SSID:",UI_MUTED,1);
    txt(74,110,wifiSsid.substring(0,37),UI_WHITE,1);
    txt(11,126,"IP:",UI_MUTED,1);
    txt(74,126,localIp(),UI_WHITE,1);
    txt(11,142,"GATEWAY:",UI_MUTED,1);
    txt(74,142,WiFi.status()==WL_CONNECTED?WiFi.gatewayIP().toString():"--",UI_WHITE,1);
    txt(11,158,"RSSI:",UI_MUTED,1);
    txt(74,158,WiFi.status()==WL_CONNECTED?String(WiFi.RSSI())+" dBm":"--",UI_WHITE,1);
    txt(170,158,"TX:",UI_MUTED,1);
    txt(204,158,txPowerVerified?String(TX_TENTHS[txLevel]/10)+"."+
        String(TX_TENTHS[txLevel]%10)+" dBm":"ERR",txPowerVerified?UI_GREEN:UI_RED,1);
    txt(11,174,"AP:",UI_MUTED,1);
    txt(74,174,portalActive?(portalRecoveryMode?"AP+STA":"OPEN / PIN"):"OFF",portalActive?UI_YELLOW:UI_MUTED,1);
    txt(180,174,portalActive?WiFi.softAPIP().toString():"",UI_WHITE,1);
    txt(11,190,"WiFi RETRY:",UI_MUTED,1);
    txt(117,190,String(wifiReconnects),UI_WHITE,1);
    txt(168,190,"FAIL:",UI_MUTED,1);
    txt(218,190,String(wifiFailures),UI_RED,1);
    txt(11,206,"WEB: "+(portalActive?WiFi.softAPIP().toString():localIp()),UI_CYAN,1);
    lcd->fillRoundRect(206,198,105,24,5,UI_ACTIVE);
    txt(213,206,setupConfirmUntil && !dueAt(setupConfirmUntil)?"CONFIRM":"OPEN AP",UI_WHITE,1);
  } else if(settingsPane==1) {
    txt(8,67,"MQTT DEBUG",UI_CYAN,2);
    card(4,87,312,137);
    txt(11,94,"MQTT:",UI_MUTED,1);
    txt(70,94,mqtt.connected()?"CONNECTED":"DISCONNECTED",
        mqtt.connected()?UI_GREEN:UI_RED,1);
    txt(11,108,"HOST:",UI_MUTED,1);
    txt(58,108,broker.substring(0,43),UI_WHITE,1);
    txt(11,122,"CLIENT:",UI_MUTED,1);
    txt(65,122,deviceId().substring(0,34),UI_WHITE,1);
    txt(11,136,"SUB S/H/T:",UI_MUTED,1);
    txt(92,136,String(mqttSubs[0]?"Y":"N")+"/"+(mqttSubs[1]?"Y":"N")+
        "/"+(mqttSubs[2]?"Y":"N"),UI_WHITE,1);
    txt(165,136,"STATE:",UI_MUTED,1);
    txt(215,136,String(mqtt.state()),UI_WHITE,1);
    txt(11,150,"RX:",UI_MUTED,1);
    txt(50,150,String(mqttPackets),UI_WHITE,1);
    txt(115,150,"BAD:",UI_MUTED,1);
    txt(155,150,String(mqttJsonErrors),UI_RED,1);
    txt(205,150,"REQ:",UI_MUTED,1);
    txt(241,150,String(mqttRequestTotal),UI_WHITE,1);
    txt(11,165,"LAST:",UI_MUTED,1);
    txt(58,165,String(lastMqttTopic).substring(0,41),UI_WHITE,1);
    if(lastMqttError[0])txt(11,180,String(lastMqttError).substring(0,48),UI_RED,1);
    lcd->fillRoundRect(9,190,103,27,5,UI_ACTIVE);
    txt(18,199,setupConfirmUntil && !dueAt(setupConfirmUntil)?"CONFIRM":"OPEN AP",UI_WHITE,1);
    lcd->fillRoundRect(208,190,101,27,5,UI_ACTIVE);
    txt(218,199,"REFRESH",UI_WHITE,1);
  } else if(settingsPane==2) {
    txt(8,65,"DEVICE / MQTT FRAMES",UI_CYAN,2);
    for(int i=0;i<4;i++) {
      int y=84+i*35;
      card(4,y,312,32);
      const SourceDebug &d=sourceDebug[i];
      txt(9,y+3,debugNames[i],UI_CYAN,1);
      String condition=live(i)?"LIVE":(d.statusKnown&&!d.statusOnline?"OFF":"WAIT");
      txt(245,y+3,condition,live(i)?UI_GREEN:(d.statusKnown&&!d.statusOnline?UI_RED:UI_YELLOW),1);
      txt(9,y+16,ids[i].length()?ids[i].substring(0,25):"ID UNKNOWN",UI_WHITE,1);
      txt(177,y+16,"S"+String(d.statusCount)+" H"+String(d.healthCount)+
          " D"+String(d.stateCount),UI_MUTED,1);
    }
    // Each device card is touch-selectable; footer explains the action.
  } else {
    txt(8,67,"SYSTEM DEBUG",UI_CYAN,2);
    card(4,88,312,136);
    txt(11,95,"FIRMWARE:",UI_MUTED,1);txt(107,95,FW_VERSION,UI_WHITE,1);
    txt(11,111,"CHIP:",UI_MUTED,1);txt(107,111,"ESP32-D0WD",UI_WHITE,1);
    txt(11,127,"UPTIME:",UI_MUTED,1);txt(107,127,String(millis()/1000UL)+" sec",UI_WHITE,1);
    txt(11,143,"HEAP:",UI_MUTED,1);txt(107,143,String(ESP.getFreeHeap())+" bytes",UI_WHITE,1);
    txt(11,159,"BLOCK:",UI_MUTED,1);txt(107,159,String(heap_caps_get_largest_free_block(
        MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT))+" bytes",UI_WHITE,1);
    txt(11,175,"NTP:",UI_MUTED,1);txt(107,175,time(nullptr)>1600000000?"SYNCED":"WAIT",UI_WHITE,1);
    txt(11,191,"RESET:",UI_MUTED,1);txt(107,191,String((int)esp_reset_reason()),UI_WHITE,1);
    txt(11,207,"TOUCH:",UI_MUTED,1);
    txt(107,207,String(lastTouchRawX)+","+String(lastTouchRawY),UI_WHITE,1);
  }
}
// Header is static during MQTT updates; update only the small connection icon.
void updateMqttIndicator() {
  static bool drawn=false,lastConnected=false;
  bool connected=mqtt.connected();
  if(drawn && lastConnected==connected)return;
  lcd->fillRect(246,16,43,12,UI_BG);
  txt(248,18,connected?"MQTT":"OFF",connected?UI_GREEN:UI_RED,1);
  drawn=true;lastConnected=connected;
}
void render() {
  const int16_t layout=chartOpen?20+(int16_t)chartPage:
      settingsOpen?10+(int16_t)settingsPane:(int16_t)currentPage;
  const bool full=(layout!=visibleLayout);
  if(full) {
    visibleLayout=layout;
    memset(visibleCells,0,sizeof(visibleCells));
    topbar(); // Full clear ONLY when switching tabs/settings/screens.
    fullScreenDraws++;
  } else {
    partialScreenDraws++;
    updateMqttIndicator();
  }
  if(chartOpen) {
    if(full || chartDirty)drawHistoryChart(full);
  } else if(settingsOpen) {
    settingsPage(); // Diagnostics refresh less often than live sensor values.
  } else {
    switch(currentPage) {
      case 0:ddsPage(full);break;
      case 1:jsyPage(full);break;
      case 2:ventPage(full);break;
      case 3:boilerPage(full);break;
    }
  }
  footer();
  lastDraw=millis();
  uiRefreshPending=false;
}
// ---------- Native TFT history: exactly the existing 48-hour MQTT protocols ----------
int compareChartEnergy(const void *a,const void *b) {
  const auto &x=*static_cast<const EnergyHistoryPoint*>(a);
  const auto &y=*static_cast<const EnergyHistoryPoint*>(b);
  return x.epoch<y.epoch?-1:x.epoch>y.epoch?1:0;
}
bool chartTokenPart(const String &s,int &position,String &part) {
  if(position<0 || position>=(int)s.length())return false;
  int sep=s.indexOf('|',position);
  if(sep<0){part=s.substring(position);position=s.length();return true;}
  part=s.substring(position,sep);position=sep+1;return true;
}
bool chartUnsigned(const String &s,uint32_t &out) {
  if(!s.length()||s[0]=='-')return false;
  char *end=nullptr;
  unsigned long x=strtoul(s.c_str(),&end,10);
  if(!end || *end || end==s.c_str())return false;
  out=(uint32_t)x;return true;
}
bool chartUnsigned64(const String &s,uint64_t &out) {
  if(!s.length()||s[0]=='-')return false;
  char *end=nullptr;
  unsigned long long x=strtoull(s.c_str(),&end,10);
  if(!end || *end || end==s.c_str())return false;
  out=(uint64_t)x;return true;
}
float chartTemperature(const String &s) {
  if(s=="null" || !s.length())return NAN;
  char *end=nullptr;
  float v=strtof(s.c_str(),&end);
  if(!end || *end || end==s.c_str() || !isfinite(v) ||
     v<(-55.0f)||v>125.0f)return NAN;
  return v;
}
void chartFail(const char *reason) {
  snprintf(chartError,sizeof(chartError),"%s",reason);
  chartLoading=false;chartDirty=true;
  Serial.printf("[HISTORY] %s (page=%u)%s\n",reason,(unsigned)chartPage,
                mqtt.connected()?"":" MQTT OFF");
}
bool chartRequestEnergy(long offset) {
  if(!mqtt.connected() || chartPage>1 || ids[chartPage].isEmpty() ||
     offset<0 || offset>10000 || chartRequests>=30)return false;
  chartOffset=offset;chartToken=esp_random();
  if(!chartToken)chartToken=1;
  chartEnergyBegun=false;chartPageRows=0;chartRequestAt=millis();
  chartLastDataAt=millis();
  const String msg="history_get|"+String(chartToken)+"|0|"+String(offset);
  const String topic="home/energy/"+ids[chartPage]+"/admin/request";
  bool sent=mqtt.publish(topic.c_str(),msg.c_str());
  if(sent)chartRequests++;
  Serial.printf("[HISTORY] Energy page %ld request=%d\n",offset,sent?1:0);
  return sent;
}
bool chartRequestTemperatures(uint8_t phase) {
  if(!mqtt.connected() || chartPage<2 || chartPage>3 ||
     ids[chartPage].isEmpty())return false;
  chartPhase=phase;chartLastDataAt=millis();
  const String cmd=chartPage==3?"boiler_history|":"vent_history|";
  const String msg=cmd+(phase?"previous":"current");
  const String topic="home/energy/"+ids[chartPage]+"/admin/request";
  bool sent=mqtt.publish(topic.c_str(),msg.c_str());
  if(sent)chartRequests++;
  Serial.printf("[HISTORY] %s phase=%u sent=%d\n",
                ids[chartPage].c_str(),(unsigned)phase,sent?1:0);
  return sent;
}
void startHistoryChart(uint8_t page) {
  if(page>3)return;
  chartOpen=true;chartPage=page;chartLoading=true;chartComplete=false;
  chartDirty=true;chartEnergyCount=0;chartRequests=0;chartFrames=0;
  chartPhase=0;chartOffset=0;chartRecentFirst=-1;
  chartEnergyBegun=false;chartPageRows=0;
  chartRequestAt=millis();chartLastDataAt=millis();chartError[0]=0;
  memset(chartTempBlocks,0,sizeof(chartTempBlocks));
  for(int c=0;c<4;c++)
    for(int i=0;i<192;i++)chartTemperatures[c][i]=NAN;
  visibleLayout=-1; // Force a new chart layout, not a redraw of the live tab.
  if(!mqtt.connected())chartFail("MQTT DISCONNECTED");
  else if(ids[page].isEmpty())chartFail("DEVICE NOT DISCOVERED");
  else if(page<2) {
    if(!chartSubs[0])chartFail("HISTORY SUBSCRIPTION OFF");
    else if(!chartRequestEnergy(0))chartFail("REQUEST FAILED");
  } else {
    if(!chartSubs[page==2?1:2])chartFail("HISTORY SUBSCRIPTION OFF");
    else if(!chartRequestTemperatures(0))chartFail("REQUEST FAILED");
  }
  render();
}
void closeHistoryChart() {
  if(!chartOpen)return;
  chartOpen=false;chartLoading=false;chartDirty=false;visibleLayout=-1;
  render();
}
void chartTemperaturePacket(const String &msg) {
  if(!chartLoading || chartPage<2)return;
  int start=0;
  String part,type,range;
  if(!chartTokenPart(msg,start,type) ||
     !chartTokenPart(msg,start,range))return;
  if(range!=(chartPhase?"previous":"current"))return;
  if(type=="E") {
    bool complete=true;
    for(int k=0;k<12;k++)if(!chartTempBlocks[chartPhase][k])complete=false;
    if(!complete) {
      chartFail("HISTORY INCOMPLETE");return;
    }
    if(chartPhase==0) {
      if(!chartRequestTemperatures(1))chartFail("PREVIOUS REQUEST FAILED");
    } else {
      chartLoading=false;chartComplete=true;
      chartDirty=true;
      Serial.printf("[HISTORY] 48h temperature complete: page %u\n",
                    (unsigned)chartPage);
    }
    return;
  }
  if(type!="R")return;
  uint32_t index=0;
  if(!chartTokenPart(msg,start,part)||!chartUnsigned(part,index) ||
     index>88 || index%8!=0)return;
  uint8_t block=index/8;
  if(chartTempBlocks[chartPhase][block])return;
  const int channels=chartPage==3?4:2;
  float samples[4][8]; // small local decoded block, commit only on full validation.
  for(int i=0;i<8;i++) {
    if(!chartTokenPart(msg,start,part))return;
    int pos=0;
    for(int k=0;k<channels;k++) {
      int comma=part.indexOf(',',pos);
      if(k+1<channels && comma<0)return;
      String value=part.substring(pos,comma<0?part.length():comma);
      float number=chartTemperature(value);
      if(!isfinite(number) && value!="null")return;
      samples[k][i]=number;
      pos=comma<0?part.length():comma+1;
    }
    if(pos<(int)part.length())return;
  }
  const int base=(chartPhase?0:96)+(int)index;
  for(int i=0;i<8;i++)
    for(int k=0;k<channels;k++)
      chartTemperatures[k][base+i]=samples[k][i];
  chartTempBlocks[chartPhase][block]=true;
  chartLastDataAt=millis();chartFrames++;chartDirty=true;
}
void chartEnergyPacket(const String &msg) {
  if(!chartLoading || chartPage>1)return;
  int position=0;
  String part,type;
  if(!chartTokenPart(msg,position,type) ||
     !chartTokenPart(msg,position,part))return;
  uint32_t token=0;
  if(!chartUnsigned(part,token) || token!=chartToken)return;
  if(type=="B") {
    uint32_t mode=1;
    if(!chartTokenPart(msg,position,part)||
       !chartUnsigned(part,mode)||mode!=0)return;
    // The sender's B frame ends in its reference epoch.
    int at=msg.lastIndexOf('|');
    uint32_t epoch=0;
    if(at<0 || !chartUnsigned(msg.substring(at+1),epoch))return;
    chartAnchorEpoch=epoch;
    chartEnergyBegun=true;chartPageRows=0;
    chartLastDataAt=millis();
    return;
  }
  if(!chartEnergyBegun)return;
  if(type=="P") {
    EnergyHistoryPoint point;
    if(!chartTokenPart(msg,position,part) ||
       !chartUnsigned(part,point.epoch))return;
    if(!chartTokenPart(msg,position,part) ||
       !chartUnsigned(part,point.generation))return;
    if(!chartTokenPart(msg,position,part) ||
       !chartUnsigned64(part,point.importWh))return;
    if(!chartTokenPart(msg,position,part) ||
       !chartUnsigned64(part,point.exportWh))return;
    chartPageRows++;chartLastDataAt=millis();
    if(chartEnergyCount<CHART_ENERGY_CAPACITY) {
      chartEnergy[chartEnergyCount++]=point;chartFrames++;
    }
    chartDirty=true;
    return;
  }
  if(type=="E") {
    if(!chartTokenPart(msg,position,part))return;
    char *end=nullptr;
    long next=strtol(part.c_str(),&end,10);
    if(!end||*end)return;
    if(!chartTokenPart(msg,position,part))return;
    uint32_t declared=0;
    if(!chartUnsigned(part,declared)||declared!=chartPageRows) {
      chartFail("MISSING HISTORY PACKETS");return;
    }
    chartEnergyBegun=false;chartLastDataAt=millis();
    if(chartPhase==0) {
      if(chartOffset==0 && next>0 && chartPageRows)
        chartRecentFirst=max(0L,next-(long)chartPageRows);
      if(next>=0 && next>chartOffset && chartPageRows &&
         chartEnergyCount<CHART_ENERGY_CAPACITY) {
        if(!chartRequestEnergy(next))chartFail("MORE HISTORY FAILED");
        return;
      }
      chartPhase=1;
      if(chartRecentFirst>1 && chartEnergyCount<CHART_ENERGY_CAPACITY) {
        if(!chartRequestEnergy(max(1L,chartRecentFirst-16L)))
          chartFail("OLDER HISTORY FAILED");
        return;
      }
    } else {
      uint32_t oldest=UINT32_MAX;
      for(int i=0;i<(int)chartEnergyCount;i++)
        oldest=min(oldest,chartEnergy[i].epoch);
      uint32_t limit=(chartAnchorEpoch>48UL*3600UL)?
         chartAnchorEpoch-48UL*3600UL:0UL;
      if(chartOffset>1 && oldest>limit && chartPageRows &&
         chartEnergyCount<CHART_ENERGY_CAPACITY) {
        if(!chartRequestEnergy(max(1L,chartOffset-16L)))
          chartFail("OLDER HISTORY FAILED");
        return;
      }
    }
    chartComplete=true;chartLoading=false;chartDirty=true;
    Serial.printf("[HISTORY] 48h energy done, checkpoints=%u\n",
                  (unsigned)chartEnergyCount);
  }
}
void handleChartMessage(uint8_t deviceType,const String &suffix,
                        const byte *payload,unsigned int length) {
  if(!chartOpen || deviceType!=chartPage || length>1800)return;
  if(chartPage<2 && suffix!="admin/history")return;
  if(chartPage==2 && suffix!="vent/history")return;
  if(chartPage==3 && suffix!="boiler/history")return;
  String msg;msg.reserve(length+2);
  for(unsigned i=0;i<length;i++)msg+=(char)payload[i];
  if(chartPage<2)chartEnergyPacket(msg);
  else chartTemperaturePacket(msg);
}
void serviceHistoryChart() {
  if(!chartOpen)return;
  if(chartLoading && (!mqtt.connected() ||
      millis()-chartLastDataAt>23000UL))
    chartFail(mqtt.connected()?"HISTORY TIMEOUT":"MQTT DISCONNECTED");
}
void drawHistoryChart(bool full) {
  // The surrounding navigation stays intact; only graph content is redrawn.
  lcd->fillRect(0,64,320,162,UI_BG);
  const char *names[4]={"ARTEMIDA","NESTOU 5A","VENT","BOILER"};
  txt(8,67,String(names[chartPage])+" - 48H",UI_CYAN,2);
  txt(216,70,chartLoading?"LOADING":chartComplete?"COMPLETE":"PARTIAL",
      chartComplete?UI_GREEN:UI_YELLOW,1);
  if(chartError[0])txt(8,90,String(chartError).substring(0,45),UI_RED,1);
  else txt(8,90,"MQTT HISTORY / "+String(chartFrames)+" FRAMES",UI_MUTED,1);
  const int left=28,right=313,top=107,bottom=196;
  lcd->drawRect(left,top,right-left+1,bottom-top+1,UI_BORDER);
  lcd->drawFastHLine(left,top+(bottom-top)/2,right-left,UI_BORDER);
  lcd->drawFastVLine((left+right)/2,top,bottom-top,UI_BORDER);
  txt(left,200,"-48h",UI_MUTED,1);
  txt((left+right)/2-12,200,"-24h",UI_MUTED,1);
  txt(right-23,200,"now",UI_MUTED,1);

  bool any=false;
  if(chartPage<2) {
    float values[48]={0};
    bool occupied[48]={false};
    uint32_t anchor=chartAnchorEpoch;
    if(anchor<1600000000UL)anchor=(uint32_t)time(nullptr);
    if(anchor>=1600000000UL) {
      qsort(chartEnergy,chartEnergyCount,sizeof(chartEnergy[0]),compareChartEnergy);
      uint32_t endHour=(anchor/3600UL)*3600UL;
      uint32_t firstHour=endHour-47UL*3600UL;
      for(int i=1;i<(int)chartEnergyCount;i++) {
        const auto &a=chartEnergy[i-1],&b=chartEnergy[i];
        uint32_t span=b.epoch-a.epoch;
        if(b.epoch<=a.epoch || span>7200UL ||
           a.generation!=b.generation ||
           b.importWh<a.importWh || b.exportWh<a.exportWh ||
           b.epoch<=firstHour || a.epoch>=endHour+3600UL)continue;
        double delta=((double)(b.importWh-a.importWh)-
                      (double)(b.exportWh-a.exportWh))/1000.0;
        uint32_t begin=max(a.epoch,firstHour);
        const uint32_t windowEnd=endHour+3600U;
        uint32_t finish=b.epoch<windowEnd?b.epoch:windowEnd;
        for(uint32_t t=begin;t<finish;) {
          uint32_t slot=(t-firstHour)/3600UL;
          const uint32_t bucketEnd=firstHour+(slot+1U)*3600U;
          uint32_t stop=finish<bucketEnd?finish:bucketEnd;
          if(slot<48 && stop>t) {
            values[slot]+=(float)(delta*((double)(stop-t)/(double)span));
            occupied[slot]=true;any=true;
          }
          t=stop;
        }
      }
      float maxAbs=0.1f;
      for(int i=0;i<48;i++)if(occupied[i])
        maxAbs=max(maxAbs,fabsf(values[i]));
      int zeroY=(top+bottom)/2;
      for(int i=0;i<48;i++)if(occupied[i]) {
        int x=left+2+i*(right-left-4)/48;
        int height=(int)(fabsf(values[i])*((bottom-top-6)/2)/maxAbs);
        if(height==0 && values[i]!=0)height=1;
        if(height) {
          int y=values[i]>=0?zeroY-height:zeroY;
          lcd->fillRect(x,y,4,height,values[i]>=0?UI_GREEN:UI_YELLOW);
        }
      }
      txt(3,111,String(maxAbs,1),UI_MUTED,1);
      txt(3,184,"-"+String(maxAbs,1),UI_MUTED,1);
    }
    txt(10,213,"NET kWh/hr  (+ import / - export)",UI_WHITE,1);
  } else {
    const int channels=chartPage==3?4:2;
    float low=999,high=-999;
    for(int k=0;k<channels;k++)for(int i=0;i<192;i++) {
      float v=chartTemperatures[k][i];
      if(isfinite(v)){low=min(low,v);high=max(high,v);any=true;}
    }
    if(any) {
      if(high-low<1.0f) {low-=0.5f;high+=0.5f;}
      else {low-=0.4f;high+=0.4f;}
      const uint16_t colors[4]={UI_CYAN,UI_YELLOW,UI_GREEN,UI_RED};
      for(int k=0;k<channels;k++) {
        bool prev=false;
        int prevX=0,prevY=0;
        for(int i=0;i<192;i++) {
          float v=chartTemperatures[k][i];
          if(!isfinite(v)){prev=false;continue;}
          int x=left+2+i*(right-left-4)/191;
          int y=bottom-2-(int)((v-low)*(bottom-top-4)/(high-low));
          y=constrain(y,top+1,bottom-1);
          if(prev)lcd->drawLine(prevX,prevY,x,y,colors[k]);
          else lcd->fillRect(x,y,2,2,colors[k]);
          prev=true;prevX=x;prevY=y;
        }
      }
      txt(1,111,String(high,0),UI_MUTED,1);
      txt(1,185,String(low,0),UI_MUTED,1);
    }
    if(chartPage==3)
      txt(8,213,"T1 CYAN T2 YELLOW T3 GREEN T4 RED",UI_WHITE,1);
    else
      txt(8,213,"INSIDE CYAN / OUTSIDE YELLOW",UI_WHITE,1);
  }
  if(!any && !chartLoading)
    txt(81,146,"NO RECORDED HISTORY",UI_YELLOW,1);
  if(!any && chartLoading)
    txt(94,146,"LOADING HISTORY",UI_CYAN,1);
  chartDirty=false;
}
// Preserve a readable TFT screen even when AP startup, TLS, or networking fails.
void drawStartupStage(const String &stage, bool error=false) {
  visibleLayout=-1; // A different screen is about to replace the dashboard.
  lcd->fillScreen(UI_BG);
  txt(10,9,"ENERGY MONITOR",UI_CYAN,2);
  txt(252,13,FW_VERSION,UI_CYAN,1);
  txt(10,68,error?"NETWORK ERROR":"STARTING",error?UI_RED:UI_YELLOW,2);
  txt(10,105,stage,UI_WHITE,2);
  txt(10,146,"LCD: ST7789 320x240",UI_MUTED,1);
  txt(10,165,"Touch: XPT2046",UI_MUTED,1);
  txt(10,204,error?"Retrying AP automatically":"Boot diagnostics at 115200",UI_CYAN,1);
  drawGear();
  Serial.printf("[DISPLAY] %s%s\n",error?"ERROR ":"",stage.c_str());
}
void drawSetup() {
  visibleLayout=-1; // Force complete redraw on return to tabs.
  lcd->fillScreen(UI_BG);
  txt(12,15,"ENERGY MONITOR",UI_CYAN,2);
  txt(264,19,FW_VERSION,UI_CYAN,1);
  txt(14,53,setupMode?"SETUP WIFI + MQTT":"AP+STA RECOVERY",UI_YELLOW,2);
  txt(14,88,"Connect to WiFi AP:",UI_WHITE,1);
  txt(14,105,apSsid,UI_CYAN,2);
  txt(14,135,"WiFi OPEN / WEB PIN",UI_YELLOW,1);
  txt(14,166,"Open browser:",UI_WHITE,1);
  txt(14,184,"http://"+WiFi.softAPIP().toString(),UI_CYAN,2);
  txt(14,217,"No credentials on GitHub",UI_MUTED,1);
  drawGear();
}
// CommonPlatform-aligned maintenance web UI, with extra PIN protection on LAN.
// Open Wi-Fi AP itself remains unencrypted; passwords are NEVER embedded in HTML.
String safeHtml(const String &input) {
  String out;out.reserve(input.length()+8);
  for(size_t i=0;i<input.length();i++) {
    switch(input[i]) {
      case '&':out+="&amp;";break;
      case '<':out+="&lt;";break;
      case '>':out+="&gt;";break;
      case '"':out+="&quot;";break;
      case '\'':out+="&#39;";break;
      default:out+=input[i];break;
    }
  }
  return out;
}
// CommonPlatform 1.06 open-AP PIN with a 15-minute scoped cookie session.
// While the portal is active, all HTTP pages and actions require authentication.
void registerWebRoutes() {
  if(routesReady)return;
  routesReady=true;
  const char *headers[]={"Cookie"};
  webServer.collectHeaders(headers,1);
  webServer.on("/ap-login",HTTP_POST,handleApWebLogin);
  webServer.on("/health",HTTP_GET,[](){
    if(portalActive && !requireApWebPin())return;
    webServer.sendHeader("Cache-Control","no-store");
    webServer.send(200,"application/json; charset=utf-8",jsonStatus());
  });
  webServer.on("/network-debug",HTTP_GET,[](){
    if(portalActive && !requireApWebPin())return;
    webServer.sendHeader("Cache-Control","no-store");
    webServer.send(200,"application/json; charset=utf-8",jsonStatus());
  });
  webServer.on("/mqtt-status",HTTP_GET,[](){
    if(portalActive && !requireApWebPin())return;
    webServer.sendHeader("Cache-Control","no-store");
    webServer.send(200,"application/json; charset=utf-8",mqttDebugJson());
  });
  webServer.on("/",HTTP_GET,[](){
    if(!requireApWebPin())return;
    String h;h.reserve(3600);
    h+="<!doctype html><html lang='el'><head><meta charset='utf-8'>";
    h+="<meta name='viewport' content='width=device-width,initial-scale=1'>";
    h+="<title>Display28 Setup</title><style>";
    h+="body{font:16px system-ui,sans-serif;background:#081622;color:#f0f7ff;";
    h+="max-width:680px;margin:auto;padding:18px}h1{font-size:24px}";
    h+=".card{background:#112639;border:1px solid #34546d;border-radius:12px;";
    h+="padding:16px;margin:12px 0}label{display:block;margin-top:10px}";
    h+="input,button{box-sizing:border-box;padding:11px;font:inherit;border-radius:7px}";
    h+="input{width:100%;background:#fff;color:#111}button{margin:12px 7px 0 0}";
    h+="a{color:#8fdaff}small{color:#bed2e0}code{word-break:break-all}</style></head><body>";
    h+="<h1>Display 2.8 - "+String(FW_VERSION)+"</h1>";
    h+="<div class='card'><b>Network</b><p>WiFi: ";
    h+=WiFi.status()==WL_CONNECTED?"Connected":"Offline / AP";
    h+=" | IP: <code>"+safeHtml(localIp())+"</code></p>";
    h+="<p>AP: ";
    h+=portalActive?"ON":"OFF";
    h+=" | MQTT: ";
    h+=mqtt.connected()?"CONNECTED":"DISCONNECTED";
    h+="</p><p>RSSI: "+String(WiFi.status()==WL_CONNECTED?WiFi.RSSI():-127)+" dBm";
    h+=" | MQTT state: "+String(mqtt.state())+"</p>";
    h+="<a href='/network-debug'>Network debug JSON</a> | ";
    h+="<a href='/mqtt-status'>MQTT/device debug JSON</a> | ";
    h+="<a href='/debug.txt'>Diagnostics text</a></div>";
    h+="<div class='card'><h2>Wi-Fi / MQTT configuration</h2>";
    h+="<small>Leave a password blank to preserve it for the same network / broker.";
    h+=" No passwords are displayed or downloaded.</small>";
    h+="<form action='/save' method='post' autocomplete='off'>";
    h+="<label>WiFi SSID<input name='ssid' maxlength='32' required value='";
    h+=safeHtml(wifiSsid);h+="'></label>";
    h+="<label>WiFi password<input name='wpass' type='password' maxlength='64'";
    h+=" placeholder='Keep existing if unchanged'></label>";
    h+="<label>HiveMQ host<input name='broker' maxlength='127' required value='";
    h+=safeHtml(broker);h+="'></label>";
    h+="<label>HiveMQ username<input name='user' maxlength='64' required value='";
    h+=safeHtml(mqttUser);h+="'></label>";
    h+="<label>HiveMQ password<input name='mpass' type='password' maxlength='64'";
    h+=" placeholder='Keep existing if blank'></label>";
    h+="<label>Price EUR/kWh<input name='price' inputmode='decimal' value='";
    h+=String(euroPerKwh,3);h+="'></label>";
    h+="<button type='submit'>Save settings & restart</button></form></div>";
    h+="<div class='card'><h2>Maintenance</h2>";
    h+="<p><a href='/ota'>Local app-only OTA</a> | ";
    h+="<a href='/restart-esp'>Restart device</a> | ";
    h+="<a href='/forget-credentials-confirm'>Reset saved credentials</a></p>";
    h+="<small>PIN session expires in 15 minutes. HTTP on the open recovery AP ";
    h+="is not encrypted. Use this interface only on a trusted network.</small></div>";
    h+="</body></html>";
    webServer.sendHeader("Cache-Control","no-store");
    webServer.send(200,"text/html; charset=utf-8",h);
  });
  webServer.on("/setup",HTTP_GET,[](){
    webServer.sendHeader("Location","/",true);
    webServer.send(302,"text/plain","");
  });
  webServer.on("/debug.txt",HTTP_GET,[](){
    if(!requireApWebPin())return;
    webServer.sendHeader("Cache-Control","no-store");
    webServer.send(200,"text/plain; charset=utf-8",
      "Display28 "+String(FW_VERSION)+"\n\nNETWORK\n"+jsonStatus()+
      "\n\nMQTT\n"+mqttDebugJson()+"\n");
  });
  webServer.on("/save",HTTP_POST,[](){
    if(!requireApWebPin())return;
    if(displayOtaBusy || displayOtaJob.pending) {
      webServer.send(409,"text/plain","OTA running, settings unavailable");return;
    }
    String ssid=webServer.arg("ssid"),host=webServer.arg("broker"),user=webServer.arg("user");
    String newWifiPass=webServer.arg("wpass"),newMqttPass=webServer.arg("mpass");
    ssid.trim();host.trim();user.trim();
    if(newWifiPass.isEmpty() && ssid==wifiSsid)newWifiPass=wifiPass;
    if(newMqttPass.isEmpty())newMqttPass=mqttPass;
    if(!ssid.length() || ssid.length()>32 || newWifiPass.length()>64 ||
       host.length()>127 || host.indexOf('/')>=0 || host.indexOf(':')>=0 ||
       !host.length() || !user.length() || user.length()>64 ||
       !newMqttPass.length() || newMqttPass.length()>64) {
      webServer.send(400,"text/plain","Invalid WiFi or MQTT configuration");return;
    }
    settings.begin("energy28",false);
    settings.putString("ssid",ssid);
    settings.putString("wpass",newWifiPass);
    settings.putString("user",user);
    settings.putString("mpass",newMqttPass);
    settings.putString("broker",host);
    float price=webServer.arg("price").toFloat();
    settings.putFloat("price",(price>0.0f && price<10.0f)?price:0.25f);
    settings.end();
    rememberRouterCredentials(ssid,newWifiPass);
    webServer.sendHeader("Cache-Control","no-store");
    restartAt=millis()+1700UL;
    Serial.println("[WEB] Saved AP/LAN credentials to NVS; scheduled reboot");
    webServer.send(200,"text/html; charset=utf-8",
      "<h2>Saved. Device restarting.</h2><p>Connect to the configured router again.</p>");
  });
  webServer.on("/restart-esp",HTTP_GET,[](){
    if(!requireApWebPin())return;
    webServer.send(200,"text/html; charset=utf-8",
      "<html><meta name='viewport' content='width=device-width,initial-scale=1'>"
      "<h2>Restart Display2.8</h2><p>Saved WiFi/MQTT settings will be kept.</p>"
      "<form method='post' action='/restart-esp'>"
      "<input type='hidden' name='confirm' value='RESTART'>"
      "<button type='submit'>Confirm restart</button></form>"
      "<p><a href='/'>Cancel</a></p></html>");
  });
  webServer.on("/restart-esp",HTTP_POST,[](){
    if(!requireApWebPin())return;
    if(webServer.arg("confirm")!="RESTART" || displayOtaBusy || displayOtaJob.pending) {
      webServer.send(409,"text/plain","Restart not confirmed or OTA busy");return;
    }
    webServer.send(200,"text/plain","Restarting Display2.8");
    restartAt=millis()+1400UL;
  });
  webServer.on("/forget-credentials-confirm",HTTP_GET,[](){
    if(!requireApWebPin())return;
    webServer.send(200,"text/html; charset=utf-8",
      "<html><meta name='viewport' content='width=device-width,initial-scale=1'>"
      "<h2>Clear saved Wi-Fi and MQTT?</h2>"
      "<p>This clears only Display2.8 credentials. Do not use for normal edits.</p>"
      "<form action='/forget-credentials' method='post'>"
      "Type ERASE to confirm: <input name='confirm' required>"
      "<button type='submit'>Erase settings and restart</button></form>"
      "<p><a href='/'>Cancel</a></p></html>");
  });
  webServer.on("/forget-credentials",HTTP_POST,[](){
    if(!requireApWebPin())return;
    if(webServer.arg("confirm")!="ERASE" || displayOtaBusy || displayOtaJob.pending) {
      webServer.send(409,"text/plain","Credentials erase not confirmed");return;
    }
    settings.begin("energy28",false);settings.clear();settings.end();
    clearRememberedRouters();
    webServer.send(200,"text/plain","Only this device's saved credentials cleared; restarting");
    restartAt=millis()+1400UL;
  });
  webServer.on("/ota",HTTP_GET,[](){
    if(!requireApWebPin())return;
    webServer.send(200,"text/html; charset=utf-8",
      "<html><body><h3>Energy Monitor "+String(FW_VERSION)+" - Local OTA</h3>"
      "<p>Only <b>Display2.8-app-only.bin</b> (application image), NOT the full merged BIN.</p>"
      "<form method='POST' action='/update' enctype='multipart/form-data'>"
      "<input type='file' name='firmware' accept='.bin' required><button type='submit'>Update</button></form></body></html>");
  });
  webServer.on("/update",HTTP_POST,[](){
    if(!requireApWebPin())return;
    bool ok=otaImageComplete && !Update.hasError();
    webServer.send(ok?200:500,"text/plain",ok?"OTA complete - rebooting":"OTA failed, old image retained");
    if(ok){delay(300);ESP.restart();}
  },[](){
    if((!portalActive && WiFi.status()!=WL_CONNECTED) || !apWebSessionValid())return;
    HTTPUpload& u=webServer.upload();
    if(u.status==UPLOAD_FILE_START) {
      otaImageComplete=false;
      if(u.filename!="Display2.8-app-only.bin" ||
         !Update.begin(UPDATE_SIZE_UNKNOWN,U_FLASH))
        Serial.println("[OTA] begin failed or incorrect app-only filename");
    } else if(u.status==UPLOAD_FILE_WRITE) {
      if(Update.isRunning() && Update.write(u.buf,u.currentSize)!=u.currentSize)
        Update.abort();
    } else if(u.status==UPLOAD_FILE_END) {
      otaImageComplete=Update.isRunning() && Update.end(true);
    } else if(u.status==UPLOAD_FILE_ABORTED) {
      Update.abort();otaImageComplete=false;
    }
  });
  webServer.onNotFound([](){
    if(portalActive) {
      if(!requireApWebPin())return;
      webServer.sendHeader("Location","/",true);
      webServer.send(302,"text/plain","");
    } else webServer.send(404,"text/plain","Not found");
  });
  webServer.begin();
}
void beginPortal(bool recovery=false) {
  if(portalActive)return;
  ++portalAttempts;
  setupMode=!recovery;
  portalRecoveryMode=recovery && wifiSsid.length()>0;
  apSsid="ESP-Display28";
  drawStartupStage(portalRecoveryMode?"AP+STA RECOVERY":"STARTING OPEN AP");

  // ESP32 uses ONE radio. Never switch AP+STA back to STA on each retry:
  // doing so switches off the SoftAP and makes recovery disappear.
  WiFi.setAutoReconnect(false);
  const wifi_mode_t wantedMode=portalRecoveryMode?WIFI_AP_STA:WIFI_AP;
  if(!WiFi.mode(wantedMode)) {
    portalFailures++;portalRetryAt=millis()+30000UL;
    drawStartupStage("AP radio mode failed",true);
    return;
  }
  const IPAddress ip=portalIp();
  if(!WiFi.softAPConfig(ip,ip,IPAddress(255,255,255,0)) ||
     !WiFi.softAP(apSsid.c_str(),nullptr,1,0,4)) {
    portalFailures++;portalRetryAt=millis()+30000UL;
    Serial.println("[AP] AP+STA/SoftAP initialization failed; retrying");
    drawStartupStage("AP start failed",true);
    return;
  }
  txPowerVerified=false;
  if(!setTxLevel(4,"AP+STA active, maximum TX")) {
    // Do not knowingly continue with unknown TX configuration.
    WiFi.softAPdisconnect(true);
    portalFailures++;portalRetryAt=millis()+30000UL;
    Serial.println("[AP] TX verification failed: retry");
    drawStartupStage("AP TX verify failed",true);
    return;
  }
  portalActive=true;
  portalStartMs=millis();portalHealthMs=portalStartMs;
  lastPortalAccessAt=0;recoveryRouterConnectedAt=0;
  nextRecoveryStaAttempt=portalRecoveryMode?
      millis()+RECOVERY_STA_RETRY_MS:0;
  portalRetryAt=0;
  resetApWebSession();
  apPinFailures=0;apPinLockedUntil=0;
  registerWebRoutes();
  portalDns.start(53,"*",WiFi.softAPIP());
  apDnsRunning=true;
  Serial.printf("[AP] %s SSID=%s IP=%s; reconnect every %lus; forced=%d\n",
    portalRecoveryMode?"AP+STA":"AP-only",apSsid.c_str(),
    WiFi.softAPIP().toString().c_str(),
    (unsigned long)(RECOVERY_STA_RETRY_MS/1000),portalForced?1:0);
  if(settingsOpen)render();else drawSetup();
}
void servicePortal() {
  if(portalActive) {
    if(apDnsRunning)portalDns.processNextRequest();
    if(millis()-portalHealthMs>=3000UL) {
      portalHealthMs=millis();
      const wifi_mode_t mode=WiFi.getMode();
      wifi_config_t apCfg={};
      const bool configOk=esp_wifi_get_config(WIFI_IF_AP,&apCfg)==ESP_OK &&
                          String((const char*)apCfg.ap.ssid)==apSsid;
      // In AP+STA the STA connection changes channel, not the SoftAP name.
      const bool modeOk=mode==WIFI_AP_STA || (!portalRecoveryMode && mode==WIFI_AP);
      if(millis()-portalStartMs>5000UL && (!modeOk || !configOk)) {
        if(apDnsRunning)portalDns.stop();
        apDnsRunning=false;portalActive=false;
        resetApWebSession();portalFailures++;
        portalRetryAt=millis()+3000UL;
        Serial.printf("[AP] Lost AP interface (mode=%d config=%d); retry\n",
                      (int)mode,configOk);
        drawStartupStage("AP interface recovery",true);
        return;
      }
    }
    // Periodically connect using the ESP-IDF STA interface. This starts a
    // *single* scan and NEVER calls WiFi.begin/mode/softAPdisconnect in-loop.
    // A scan may briefly interrupt SoftAP beacons: AP+STA shares one channel.
    if(portalRecoveryMode && WiFi.status()!=WL_CONNECTED)
      recoveryRouterConnectedAt=0;
    if(portalRecoveryMode && !portalForced && wifiSsid.length() &&
       WiFi.status()!=WL_CONNECTED && nextRecoveryStaAttempt &&
       dueAt(nextRecoveryStaAttempt)) {
      if(WiFi.softAPgetStationNum()>0) {
        // One ESP32 radio: DO NOT scan while a phone is configuring the AP.
        nextRecoveryStaAttempt=millis()+10000UL;
      } else if(lastPortalAccessAt &&
                millis()-lastPortalAccessAt<RECOVERY_WEB_GRACE_MS) {
        nextRecoveryStaAttempt=lastPortalAccessAt+RECOVERY_WEB_GRACE_MS;
      } else {
        nextRecoveryStaAttempt=millis()+RECOVERY_STA_RETRY_MS;
        lastRecoveryStaAttempt=millis();
        ++recoveryStaAttempts;
        const esp_err_t error=esp_wifi_connect();
        recoveryStaLastError=(int32_t)error;
        if(error!=ESP_OK)++recoveryStaFailures;
        Serial.printf("[AP+STA] saved SSID reconnect attempt %lu: %s (0x%X)\n",
          (unsigned long)recoveryStaAttempts,esp_err_to_name(error),(unsigned)error);
      }
    }
  } else if(portalRetryAt && dueAt(portalRetryAt)) {
    portalRetryAt=0;
    if(wifiSsid.isEmpty() || portalForced ||
       (disconnectedSince && millis()-disconnectedSince>=20000UL))
      beginPortal(!wifiSsid.isEmpty());
  }
}
void stopRecoveryPortal() {
  if(!portalActive || !portalRecoveryMode || portalForced)return;
  if(WiFi.status()!=WL_CONNECTED || WiFi.localIP()==IPAddress(0,0,0,0)) {
    recoveryRouterConnectedAt=0;return;
  }
  if(recoveryRouterConnectedAt==0) {
    recoveryRouterConnectedAt=millis();
    Serial.printf("[AP+STA] Router found (%s); waiting 15s before AP close\n",
                  WiFi.localIP().toString().c_str());
    return;
  }
  // Finish any active configuration before withdrawing the AP.
  if(millis()-recoveryRouterConnectedAt<RECOVERY_ROUTER_STABLE_MS ||
     WiFi.softAPgetStationNum()>0 ||
     (lastPortalAccessAt && millis()-lastPortalAccessAt<RECOVERY_WEB_GRACE_MS))
    return;
  Serial.printf("[AP+STA] Router stable; closing recovery AP (STA IP=%s)\n",
                WiFi.localIP().toString().c_str());
  if(apDnsRunning)portalDns.stop();
  apDnsRunning=false;
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  portalActive=false;portalRecoveryMode=false;
  recoveryRouterConnectedAt=0;nextRecoveryStaAttempt=0;
  resetApWebSession();
  setupMode=false;
  render();
}
int typeById(const String &id) {
  String n=id; n.toLowerCase();
  if(n.indexOf("boiler")>=0) return 3;
  if(n.indexOf("vent")>=0) return 2;
  if(n.indexOf("jsy")>=0) return 1;
  if(n.indexOf("dds")>=0) return 0;
  return -1;
}
int typeByDoc(JsonDocument &doc) {
  String t=doc["device_type"] | "";
  if(t.isEmpty()) t=String(doc["meter"]|"");
  t.toUpperCase();
  if(t.indexOf("BOILER")>=0) return 3;
  if(t.indexOf("VENTILATION")>=0) return 2;
  if(t.indexOf("JSY")>=0) return 1;
  if(t.indexOf("DDS")>=0) return 0;
  if(doc.containsKey("v1") || doc.containsKey("i1")) return 1;
  if(doc.containsKey("power")) return 0;
  return -1;
}
// CommonPlatform-compatible device-management topics. The display is separate
// from DDS/JSY/VENT/BOILER and never acts as an EnergyMeter.
String displayTopic(const char* suffix) {
  return "home/energy/"+deviceId()+"/"+suffix;
}
void publishDisplaySnapshot(bool withState=true) {
  if(!mqtt.connected())return;
  StaticJsonDocument<1024> doc;
  doc["meter"]="DISPLAY28";
  doc["device_type"]="DISPLAY28";
  doc["application"]="display28";
  doc["driver"]="ST7789-XPT2046";
  doc["profile"]=OTA_PROFILE_ID;
  doc["ota_protocol"]=2;
  doc["platform_version"]="1.06";
  doc["firmware"]=FW_VERSION;
  doc["meter_ok"]=true;
  doc["read_attempted"]=true;
  doc["has_sample"]=true;
  doc["rssi"]=(WiFi.status()==WL_CONNECTED)?WiFi.RSSI():-127;
  doc["sta_ip"]=(WiFi.status()==WL_CONNECTED)?WiFi.localIP().toString():"";
  doc["tx_dbm_tenths"]=txPowerVerified?TX_TENTHS[txLevel]:0;
  doc["ap_active"]=portalActive;
  doc["uptime_sec"]=millis()/1000UL;
  doc["heap_free"]=ESP.getFreeHeap();
  doc["mqtt_packets"]=mqttPackets;
  doc["ota_busy"]=displayOtaBusy;
  char json[1024];size_t n=serializeJson(doc,json,sizeof(json));
  if(!n)return;
  const String h=displayTopic("health");
  mqtt.publish(h.c_str(),json,true);
  if(withState) {
    const String t=displayTopic("state");
    mqtt.publish(t.c_str(),json,false);
  }
  nextDisplayHealthAt=millis()+30000UL;
}
void adminReply(const char *cmd,bool ok,const String &status,
                int progress=-1) {
  if(!mqtt.connected())return;
  StaticJsonDocument<448> doc;
  doc["cmd"]=cmd;doc["ok"]=ok;
  doc["state"]=status;
  doc["firmware"]=FW_VERSION;
  doc["profile"]=OTA_PROFILE_ID;
  if(progress>=0)doc["progress"]=progress;
  char out[448];serializeJson(doc,out,sizeof(out));
  String t=displayTopic("admin/response");
  mqtt.publish(t.c_str(),out,false);
}
bool isAsciiMd5(const String &m) {
  if(m.length()!=32)return false;
  for(size_t i=0;i<m.length();i++) {
    char c=m[i];
    if(!((c>='0'&&c<='9')||(c>='a'&&c<='f')||
         (c>='A'&&c<='F')))return false;
  }
  return true;
}
bool approvedDisplayOtaUrl(const String &url) {
  const String prefix=
    "https://raw.githubusercontent.com/ApostolosGit/ESP32-OTA.bin/main/Display2.8-ver0.";
  const String suffix="-app-only.bin";
  if(!url.startsWith(prefix)||!url.endsWith(suffix))return false;
  String n=url.substring(prefix.length(),url.length()-suffix.length());
  if(n.isEmpty()||n.length()>3)return false;
  for(size_t i=0;i<n.length();i++)if(!isDigit(n[i]))return false;
  // Reject reinstall/downgrade; version is enforced again by Update's MD5.
  return n.toInt()>7;
}
void handleDisplayAdmin(const String &command) {
  if(command=="restart_esp|12134") {
    if(displayOtaBusy||displayOtaJob.pending) {
      adminReply("restart_esp",false,"busy");return;
    }
    adminReply("restart_esp",true,"restarting");
    restartAt=millis()+1200UL;
    return;
  }
  if(command.startsWith("ota_https_v2|")) {
    if(displayOtaBusy||displayOtaJob.pending) {
      adminReply("ota_https_v2",false,"busy");return;
    }
    // Strict allowlist + independent profile + PIN, no arbitrary URL commands.
    String fields[6];int start=0;
    for(int k=0;k<5;k++) {
      int sep=command.indexOf('|',start);
      if(sep<0){adminReply("ota_https_v2",false,"malformed");return;}
      fields[k]=command.substring(start,sep);start=sep+1;
    }
    fields[5]=command.substring(start);
    size_t bytes=(size_t)fields[1].toInt();
    if(fields[0]!="ota_https_v2" || !bytes || bytes>1500000 ||
       !isAsciiMd5(fields[2]) ||
       !approvedDisplayOtaUrl(fields[3]) ||
       fields[4]!=OTA_PROFILE_ID || fields[5]!="12134") {
      adminReply("ota_https_v2",false,"profile_or_manifest_rejected");return;
    }
    if(WiFi.status()!=WL_CONNECTED || time(nullptr)<1600000000) {
      adminReply("ota_https_v2",false,"network_or_ntp_unavailable");return;
    }
    displayOtaJob.size=bytes;
    displayOtaJob.md5=fields[2];
    displayOtaJob.url=fields[3];
    displayOtaJob.pending=true;
    adminReply("ota_https_v2",true,"accepted");
    Serial.printf("[OTA] accepted %u bytes from approved GitHub repo\n",(unsigned)bytes);
    return;
  }
  adminReply("unknown",false,"unsupported_command");
}
void performDisplayOta() {
  if(!displayOtaJob.pending || displayOtaBusy)return;
  displayOtaBusy=true;
  displayOtaJob.pending=false;
  if(!mqtt.connected() || WiFi.status()!=WL_CONNECTED) {
    adminReply("ota_https_v2",false,"network_disconnected");
    displayOtaBusy=false;return;
  }
  drawStartupStage("OTA UPDATING");
  adminReply("ota_https_v2",true,"starting",0);
  WiFiClientSecure otaTls;
  otaTls.setCACert(GITHUB_ROOT_CA);
  otaTls.setTimeout(16000);
  HTTPClient http;
  http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
  http.setConnectTimeout(15000);
  http.setTimeout(16000);
  bool success=false;
  String error="unknown";
  do {
    const char *otaHost="raw.githubusercontent.com";
    IPAddress resolved;
    if(WiFi.hostByName(otaHost,resolved)!=1) {
      error="github_dns_failed";
      Serial.println("[OTA] GitHub DNS failed; check router DNS");
      break;
    }
    Serial.printf("[OTA] GitHub DNS=%s free heap=%lu largest=%lu\n",
       resolved.toString().c_str(),(unsigned long)ESP.getFreeHeap(),
       (unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT));
    int code=0;
    for(uint8_t attempt=1;attempt<=3;attempt++) {
      // Retry only before flashing starts. HTTPS is validated against the
      // DigiCert + ISRG X1 root CA bundle; never use setInsecure().
      if(!http.begin(otaTls,displayOtaJob.url)) {
        error="https_start_failed";break;
      }
      code=http.GET();
      if(code==HTTP_CODE_OK)break;
      char tlsDetails[128]={0};
      int tlsError=otaTls.lastError(tlsDetails,sizeof(tlsDetails));
      Serial.printf("[OTA] GitHub HTTPS attempt %u/3 HTTP=%d (%s) TLS=%d (%s) heap=%lu\n",
        (unsigned)attempt,code,http.errorToString(code).c_str(),
        tlsError,tlsDetails,(unsigned long)ESP.getFreeHeap());
      error="https_http_"+String(code);
      http.end();otaTls.stop();
      if(code>0 || attempt>=3 || WiFi.status()!=WL_CONNECTED)break;
      delay(450UL*attempt);
    }
    if(code!=HTTP_CODE_OK)break;
    int announced=http.getSize();
    if(announced<=0 || (size_t)announced!=displayOtaJob.size) {
      error="size_mismatch_or_chunked";break;
    }
    if(!Update.begin(displayOtaJob.size,U_FLASH)) {
      error="update_partition_unavailable";break;
    }
    if(!Update.setMD5(displayOtaJob.md5.c_str())) {
      error="md5_configuration_failed";Update.abort();break;
    }
    uint8_t buffer[1024];
    WiFiClient *input=http.getStreamPtr();
    size_t done=0;
    uint32_t started=millis(),lastData=started,lastProgress=started;
    bool streamError=false;
    while(done<displayOtaJob.size && millis()-started<240000UL) {
      size_t available=input->available();
      if(!available) {
        if(!http.connected() && millis()-lastData>1000UL)break;
        if(millis()-lastData>12000UL)break;
        mqtt.loop();delay(5);continue;
      }
      size_t take=min((size_t)sizeof(buffer),min(available,displayOtaJob.size-done));
      size_t got=input->readBytes(buffer,take);
      if(!got)continue;
      if(Update.write(buffer,got)!=got){streamError=true;break;}
      done+=got;lastData=millis();
      if(millis()-lastProgress>4000UL) {
        lastProgress=millis();
        adminReply("ota_https_v2",true,"writing",(int)(done*100/displayOtaJob.size));
        mqtt.loop();
      }
      delay(1);
    }
    if(streamError || done!=displayOtaJob.size) {
      error="download_or_write_incomplete";
      Update.abort();break;
    }
    if(!Update.end()) {
      error="md5_or_flash_validation_failed";break;
    }
    success=true;
  } while(false);
  http.end();otaTls.stop();
  if(success) {
    adminReply("ota_https_v2",true,"rebooting",100);
    Serial.println("[OTA] verified update complete, rebooting");
    delay(800);ESP.restart();
    return;
  }
  adminReply("ota_https_v2",false,error);
  Serial.printf("[OTA] failed %s, Update error=%d\n",error.c_str(),Update.getError());
  displayOtaBusy=false;
  if(portalActive)drawSetup();else render();
}

// Decode every subscribed topic. Every device has its own counters and ages,
// making it possible to distinguish missing traffic from stale/invalid JSON.
void mqttMessage(char *topic,byte *payload,unsigned int len) {
  mqttPackets++;lastMqttPacketAt=millis();
  snprintf(lastMqttTopic,sizeof(lastMqttTopic),"%s",topic);
  String t=topic, prefix="home/energy/";
  if(!t.startsWith(prefix)) {mqttUnknown++;return;}
  String rest=t.substring(prefix.length());
  int div=rest.indexOf('/');
  if(div<1) {mqttUnknown++;snprintf(lastUnknownTopic,sizeof(lastUnknownTopic),"%s",topic);return;}
  String id=rest.substring(0,div), suffix=rest.substring(div+1);
  if(id==deviceId()) {
    if(suffix=="admin/request") {
      if(len>380){adminReply("unknown",false,"command_too_large");return;}
      String command;
      command.reserve(len+1);
      for(unsigned i=0;i<len;i++)command+=(char)payload[i];
      handleDisplayAdmin(command);
    } else if(suffix=="request") {
      String command;
      for(unsigned i=0;i<len&&i<20;i++)command+=(char)payload[i];
      if(command=="update")displayStateRequested=true;
    }
    return;
  }
  int type=typeById(id);
  // Graphs reuse the sources' existing 48h MQTT history protocol.
  // Treat history responses separately from /state, not as unknown topics.
  if(suffix=="admin/history" || suffix=="vent/history" ||
     suffix=="boiler/history") {
    if(type<0 && chartOpen && ids[chartPage]==id)type=chartPage;
    if(type>=0)handleChartMessage((uint8_t)type,suffix,payload,len);
    return;
  }
  if(suffix=="state" || suffix=="health") {
    incomingState.clear();
    DeserializationError err=deserializeJson(incomingState,(const char*)payload,len);
    if(err) {
      mqttJsonErrors++;
      snprintf(lastMqttError,sizeof(lastMqttError),
               "%.18s JSON %s",id.c_str(),err.c_str());
      Serial.printf("[MQTT] BAD JSON %s (%u bytes): %s\n",
                    topic,len,err.c_str());
      return;
    }
    // Prefer the firmware's own meter/device_type field over heuristic ID matching.
    int declared=typeByDoc(incomingState);
    if(declared>=0)type=declared;
    if(type<0) {
      mqttUnknown++;
      snprintf(lastUnknownTopic,sizeof(lastUnknownTopic),"%s",topic);
      Serial.printf("[MQTT] Unknown type for %s\n",topic);
      return;
    }
    ids[type]=id; // Discovered from the actual MQTT topic.
    SourceDebug &d=sourceDebug[type];
    if(suffix=="health") {
      d.healthCount++;d.healthAt=millis();
      if(!hasState[type] || !live(type))nextProbeAt[type]=millis()+500UL;
      Serial.printf("[MQTT] HEALTH type=%d id=%s bytes=%u\n",type,id.c_str(),len);
    } else {
      state[type].clear();
      state[type].set(incomingState.as<JsonObjectConst>());
      if(state[type].overflowed()) {
        mqttJsonErrors++;
        snprintf(lastMqttError,sizeof(lastMqttError),"%s: state overflow",id.c_str());
        Serial.printf("[MQTT] state overflow: %s\n",id.c_str());
        return;
      }
      hasState[type]=true;lastRx[type]=millis();
      d.stateCount++;d.stateAt=millis();
      nextProbeAt[type]=0;
      Serial.printf("[MQTT] STATE type=%d id=%s bytes=%u\n",type,id.c_str(),len);
    }
  } else if(suffix=="status") {
    if(type<0) {
      mqttUnknown++;
      snprintf(lastUnknownTopic,sizeof(lastUnknownTopic),"%s",topic);
      Serial.printf("[MQTT] Unknown STATUS %s\n",topic);
      return;
    }
    ids[type]=id;
    SourceDebug &d=sourceDebug[type];
    d.statusAt=millis();d.statusCount++;
    String value;
    for(unsigned i=0;i<len&&i<16;i++)value+=(char)payload[i];
    value.trim();value.toLowerCase();
    if(value=="online"||value=="offline") {
      d.statusKnown=true;d.statusOnline=(value=="online");
      if(d.statusOnline && (!hasState[type] || !live(type)))
        nextProbeAt[type]=millis()+600UL;
    }
    Serial.printf("[MQTT] STATUS type=%d id=%s value=%s\n",
                  type,id.c_str(),value.c_str());
  } else {
    mqttUnknown++;
    snprintf(lastUnknownTopic,sizeof(lastUnknownTopic),"%s",topic);
    return;
  }
  if((settingsOpen && settingsPane==2) ||
     (!settingsOpen && !portalActive && type==(int)currentPage))
    uiRefreshPending=true; // Do not draw inside the MQTT network callback.
}
// Last four intentionally configured routers; no password shown in the UI.
bool findRouterCredentials(const String &ssid,String &password) {
  password="";
  if(ssid.isEmpty())return false;
  if(ssid==wifiSsid && !wifiPass.isEmpty()) {
    password=wifiPass;return true;
  }
  if(!settings.begin("wifi28",true))return false;
  bool found=false;
  for(int i=0;i<4;i++) {
    const String keyS="s"+String(i),keyP="p"+String(i);
    if(settings.getString(keyS.c_str(),"")==ssid) {
      password=settings.getString(keyP.c_str(),"");
      found=true;break;
    }
  }
  settings.end();return found;
}
void rememberRouterCredentials(const String &ssid,const String &password) {
  if(ssid.isEmpty()||ssid.length()>32||password.length()>64)return;
  if(!settings.begin("wifi28",false))return;
  int slot=-1;
  for(int i=0;i<4;i++) {
    if(settings.getString(("s"+String(i)).c_str(),"")==ssid) {
      slot=i;break;
    }
  }
  if(slot>=0) {
    if(settings.getString(("p"+String(slot)).c_str(),"")!=password)
      settings.putString(("p"+String(slot)).c_str(),password);
  } else {
    slot=(int)(settings.getUChar("slot",0)%4);
    settings.putString(("s"+String(slot)).c_str(),ssid);
    settings.putString(("p"+String(slot)).c_str(),password);
    settings.putUChar("slot",(uint8_t)((slot+1)%4));
  }
  settings.end();
}
void clearRememberedRouters() {
  if(settings.begin("wifi28",false)) {
    settings.clear();settings.end();
  }
}
void readConfig() {
  settings.begin("energy28",true); // Keep backwards compatibility with ver0.01 NVS.
  wifiSsid=settings.getString("ssid","");
  wifiPass=settings.getString("wpass","");
  mqttUser=settings.getString("user","");
  mqttPass=settings.getString("mpass","");
  broker=settings.getString("broker",BROKER_DEFAULT);
  euroPerKwh=settings.getFloat("price",0.25f);
  settings.end();
  if(broker.isEmpty())broker=BROKER_DEFAULT;
  if(!wifiSsid.isEmpty())rememberRouterCredentials(wifiSsid,wifiPass);
}
// Device firmware responds to "update" with a non-retained /state frame.
// The /status and /health messages are retained, so request samples as soon
// as their actual full MAC-suffixed MQTT topic IDs have been discovered.
bool requestDevice(int i,bool manual=false) {
  if(i<0||i>3||!mqtt.connected()||ids[i].isEmpty())return false;
  SourceDebug &d=sourceDebug[i];
  if(!manual && d.requestAt && millis()-d.requestAt<15000UL)return false;
  const String topic="home/energy/"+ids[i]+"/request";
  bool ok=mqtt.publish(topic.c_str(),"update");
  d.requestAt=millis();d.requests++;mqttRequestTotal++;
  if(!ok)d.requestFails++;
  nextProbeAt[i]=0;
  Serial.printf("[MQTT] REQUEST type=%d id=%s result=%s\n",i,ids[i].c_str(),ok?"sent":"FAILED");
  return ok;
}
void requestAll() {
  if(!mqtt.connected())return;
  for(int i=0;i<4;i++)requestDevice(i,true);
  lastPoll=millis();
}
void serviceDeviceProbes() {
  if(!mqtt.connected())return;
  for(int i=0;i<4;i++) {
    if(ids[i].isEmpty())continue;
    // For missing/stale samples request an update every 15s, never flood broker.
    bool stale=!hasState[i] || (millis()-lastRx[i]>90000UL);
    if(stale && (nextProbeAt[i]==0 || dueAt(nextProbeAt[i]))) {
      if(!sourceDebug[i].requestAt || millis()-sourceDebug[i].requestAt>=15000UL)
        requestDevice(i,false);
    }
  }
}
void startStation() {
  if(portalActive) {
    // Recovery STA retries run via esp_wifi_connect() in servicePortal().
    nextWifiAttempt=millis()+RECOVERY_STA_RETRY_MS;return;
  }
  if(!WiFi.mode(WIFI_STA)) {
    Serial.println("[WiFi] STA radio mode failed");
    wifiFailures++;nextWifiAttempt=millis()+10000;return;
  }
  // The radio is started by WiFi.begin; TX setting is valid only afterwards.
  // Use the requested maximum for router acquisition; radio remains shared.
  uint8_t target=4; // MAX TX to reach newly configured router.
  WiFi.begin(wifiSsid.c_str(),wifiPass.c_str());
  txPowerVerified=false;
  if(!setTxLevel(target,"STA MAX TX")) {
    WiFi.disconnect(false,false); // Stop association with an unverified TX cap.
    Serial.println("[WiFi] TX cap failed; STA attempt stopped");
    nextWifiAttempt=millis()+10000;wifiFailures++;return;
  }
  wifiReconnects++;
  wifiFailures++;
  nextWifiAttempt=millis()+WIFI_RETRY_MS[wifiRetryIdx];
  if(wifiRetryIdx<3)wifiRetryIdx++;
}
void maintainConnection() {
  if(WiFi.status()!=WL_CONNECTED) {
    if(wifiCurrentlyConnected) {
      wifiCurrentlyConnected=false;
      mqtt.disconnect();tls.stop();
      nextMqttAttempt=millis();
      Serial.println("[WiFi] lost connection, MQTT transport stopped");
    }
    if(!disconnectedSince)disconnectedSince=millis();
    if(!portalActive && dueAt(nextWifiAttempt) && wifiSsid.length()) {
      Serial.println("[WiFi] reconnecting");startStation();
    }
    if(!portalActive && (!portalRetryAt || dueAt(portalRetryAt)) &&
       wifiSsid.length() && millis()-disconnectedSince>20000UL) {
      beginPortal(true); // AP+STA recovery: keep AP available and periodically retry STA.
    }
    return;
  }
  if(!wifiCurrentlyConnected) {
    wifiCurrentlyConnected=true;wifiEverConnected=true;
    wifiRetryIdx=0;nextMqttAttempt=millis();
    Serial.printf("[WiFi] connected IP=%s\n",localIp().c_str());
    // CommonPlatform initializes NTP after a real router association.
    configTzTime("EET-2EEST,M3.5.0/3,M10.5.0/4",
                 "pool.ntp.org","time.google.com");
    timeConfigured=true;lastNtpAttempt=millis();
  }
  if(disconnectedSince) {
    disconnectedSince=0;
    Serial.printf("[WiFi] recovered IP=%s\n",localIp().c_str());
  }
  if(portalActive && !portalForced)stopRecoveryPortal();
  retuneWifi();
  if(time(nullptr)<1600000000 && millis()-lastNtpAttempt>30000UL) {
    configTzTime("EET-2EEST,M3.5.0/3,M10.5.0/4",
                 "pool.ntp.org","time.google.com");
    lastNtpAttempt=millis();
    Serial.println("[NTP] retry");
  }
  if(mqtt.connected()) {
    if(!mqtt.loop()) {
      Serial.println("[MQTT] broker transport interrupted");
      return;
    }
    mqttRetryIdx=0;
    if(dueAt(nextSubRetryAt)) {
      if(!mqttSubs[0])mqttSubs[0]=mqtt.subscribe("home/energy/+/state");
      if(!mqttSubs[1])mqttSubs[1]=mqtt.subscribe("home/energy/+/status");
      if(!mqttSubs[2])mqttSubs[2]=mqtt.subscribe("home/energy/+/health");
      if(!chartSubs[0])chartSubs[0]=mqtt.subscribe("home/energy/+/admin/history");
      if(!chartSubs[1])chartSubs[1]=mqtt.subscribe("home/energy/+/vent/history");
      if(!chartSubs[2])chartSubs[2]=mqtt.subscribe("home/energy/+/boiler/history");
      nextSubRetryAt=millis()+30000UL;
    }
    return;
  }
  // TLS certificate verification must never run against an unknown clock.
  if(time(nullptr)<1600000000)return;
  if(!dueAt(nextMqttAttempt))return;
  mqttReconnects++;
  const String id=deviceId();
  Serial.printf("[MQTT] TLS connect attempt %lu\n",(unsigned long)mqttReconnects);
  const String will=displayTopic("status");
  if(mqtt.connect(id.c_str(),mqttUser.c_str(),mqttPass.c_str(),
                  will.c_str(),1,true,"offline")) {
    mqttSubs[0]=mqtt.subscribe("home/energy/+/state");
    mqttSubs[1]=mqtt.subscribe("home/energy/+/status");
    mqttSubs[2]=mqtt.subscribe("home/energy/+/health");
    chartSubs[0]=mqtt.subscribe("home/energy/+/admin/history");
    chartSubs[1]=mqtt.subscribe("home/energy/+/vent/history");
    chartSubs[2]=mqtt.subscribe("home/energy/+/boiler/history");
    Serial.printf("[HISTORY] subscriptions energy=%d vent=%d boiler=%d\n",
                  chartSubs[0],chartSubs[1],chartSubs[2]);
    const String ownAdmin=displayTopic("admin/request");
    const String ownRequest=displayTopic("request");
    bool adminSub=mqtt.subscribe(ownAdmin.c_str());
    bool stateRequestSub=mqtt.subscribe(ownRequest.c_str());
    mqtt.publish(will.c_str(),"online",true);
    publishDisplaySnapshot(true);
    Serial.printf("[MQTT] display admin=%d request=%d\n",adminSub,stateRequestSub);
    Serial.printf("[MQTT] connected SUBS state=%d status=%d health=%d\n",
                  mqttSubs[0],mqttSubs[1],mqttSubs[2]);
    mqttEverConnected=true;
    mqttRetryIdx=0;nextSubRetryAt=millis()+30000UL;
    for(int i=0;i<4;i++)if(ids[i].length())nextProbeAt[i]=millis()+1500UL+i*300UL;
    lastPoll=millis()-55000UL;if(!portalActive)render();
  } else {
    mqttFailures++;
    const uint32_t* steps=mqttEverConnected?MQTT_RUN_RETRY_MS:MQTT_BOOT_RETRY_MS;
    uint8_t last=mqttEverConnected?2:4;
    uint32_t delayMs=steps[mqttRetryIdx];
    if(mqttRetryIdx<last)mqttRetryIdx++;
    nextMqttAttempt=millis()+delayMs;
    Serial.printf("[MQTT] state=%d retry in %lu ms\n",mqtt.state(),(unsigned long)delayMs);
  }
}
// Two deliberate taps prevent accidental exposure of the maintenance AP.
void requestLocalSetupPortal() {
  if(portalActive) {
    setupConfirmUntil=0;settingsOpen=false;drawSetup();return;
  }
  if(!setupConfirmUntil || dueAt(setupConfirmUntil)) {
    setupConfirmUntil=millis()+8000UL;
    render();
    Serial.println("[WEB] Press OPEN AP again within 8s to enter maintenance");
    return;
  }
  setupConfirmUntil=0;
  settingsOpen=false;
  portalForced=true; // Never silently close a user-requested maintenance AP.
  Serial.println("[WEB] User requested maintenance AP from the touchscreen");
  beginPortal(wifiSsid.length()>0);
}
void handleTouch() {
  static bool held=false;
  bool pressed=touch.touched();
  if(!pressed) {held=false;return;}
  if(held)return;
  held=true;
  TS_Point p=touch.getPoint();
  if(p.z<100)return;
  lastTouchRawX=p.x;lastTouchRawY=p.y;
  int x=constrain((int)map(p.x,250,3800,0,319),0,319);
  int y=constrain((int)map(p.y,250,3800,0,239),0,239);
  Serial.printf("[TOUCH] raw=(%d,%d) -> (%d,%d)\n",p.x,p.y,x,y);
  // Persistent settings gear in the top-right corner, even in AP setup mode.
  if(x>=287 && y<=30) {
    if(chartOpen) {
      chartOpen=false;chartLoading=false;chartDirty=false;visibleLayout=-1;
    }
    settingsOpen=!settingsOpen;
    if(settingsOpen) {
      settingsPane=0;render();
    } else if(portalActive)drawSetup();
    else render();
    return;
  }
  if(chartOpen) {
    // History display: touching anywhere in the plot returns to live values.
    // Top navigation still works, including a fresh MQTT request on the active tab.
    if(y>=29 && y<=64) {
      uint8_t tab=constrain(x/80,0,3);
      if(tab==currentPage) {
        requestDevice(tab,true);
        chartDirty=true;render();
      } else {
        chartOpen=false;chartLoading=false;chartDirty=false;
        visibleLayout=-1;currentPage=tab;
        if(!live(tab))requestDevice(tab,true);
        render();
      }
    } else closeHistoryChart();
    return;
  }
  if(settingsOpen) {
    if(settingsPane==0 && x>=200 && y>=196 && y<=223) {
      requestLocalSetupPortal();return;
    }
    if(settingsPane==1 && x>=8 && x<=115 && y>=190 && y<=222) {
      requestLocalSetupPortal();return;
    }
    if(y>=29 && y<=63) {
      uint8_t pane=constrain(x/80,0,3);
      if(settingsPane!=pane){settingsPane=pane;render();}
      return;
    }
    if(settingsPane==1 && x>=205 && y>=190 && y<=222) {
      requestAll();render();return;
    }
    if(settingsPane==2 && y>=84 && y<223) {
      int n=(y-84)/35;
      if(n>=0 && n<4) {
        currentPage=n;settingsOpen=false;
        if(!live(n))requestDevice(n,true);
        render();
      }
      return;
    }
    return;
  }
  if(portalActive)return; // Keep AP setup screen visible except for gear.
  if(y>=29 && y<=64) {
    int newPage=constrain(x/80,0,3);
    if(currentPage!=newPage) {
      currentPage=newPage;
      if(!live(newPage))requestDevice(newPage,false);
      render();
    } else {
      // Explicit user touch always requests a new /state, even if already LIVE.
      bool sent=requestDevice(currentPage,true);
      Serial.printf("[TOUCH] active tab %u immediate MQTT update=%d\n",
                    (unsigned)currentPage,sent?1:0);
      uiRefreshPending=true;
    }
    return;
  }
  // Main body tap opens the selected device's true 48h history.
  // No invented data: if the source has no stored history, show NO HISTORY.
  if(y>=65)startHistoryChart(currentPage);
}
void setup() {
  Serial.begin(115200);
  Serial.printf("[BOOT] Energy Monitor %s / %s\n",FW_VERSION,COMPAT_PLATFORM);
  Serial.printf("[BOOT] Reset reason=%d, heap=%lu\n",
                (int)esp_reset_reason(),(unsigned long)ESP.getFreeHeap());
  pinMode(21,OUTPUT);
  digitalWrite(21,HIGH);
  delay(50); // Allow display power and SPI lines to settle.
  bool displayReady=lcd->begin();
  if(!displayReady) {
    Serial.println("[BOOT] LCD begin() failed; retry");
    delay(150);
    displayReady=lcd->begin();
  }
  if(!displayReady) {
    Serial.println("[BOOT] FATAL: TFT initialization failed");
    // If LCD initialization failed, stop instead of enabling Wi-Fi over random TFT RAM.
    while(true)delay(1000);
  }
  lcd->invertDisplay(true);
  lcd->fillScreen(UI_BG);
  txt(10,10,"ENERGY MONITOR",UI_CYAN,2);
  txt(10,38,FW_VERSION,UI_WHITE,2);
  Serial.println("[BOOT] TFT init OK");
  SPI.begin(25,39,32,33);
  touch.begin();touch.setRotation(1);
  readConfig();
  tls.setCACert(LE_ROOT_CA);
  mqtt.setServer(broker.c_str(),8883);
  mqtt.setBufferSize(12288);
  mqtt.setCallback(mqttMessage);

  // CRITICAL: display the actual DDS tab before ANY Wi-Fi radio operation.
  // If AP setup or Wi-Fi initialization fails, the TFT never stays uninitialized.
  render();
  if(wifiSsid.isEmpty() || mqttUser.isEmpty() || mqttPass.isEmpty()) {
    Serial.println("[BOOT] No credentials -> open AP setup");
    beginPortal(false);
    return;
  }
  if(!WiFi.mode(WIFI_STA)) {
    Serial.println("[BOOT] Wi-Fi STA mode failed");
    drawStartupStage("STA radio failed",true);
    beginPortal(true);
    return;
  }
  WiFi.setAutoReconnect(false);
  registerWebRoutes(); // Read-only /health on LAN; protected config in AP.
  startStation();       // Nonblocking; fallback AP+STA after 20 seconds.
  Serial.println("[BOOT] Display and networking initialized");
}
void loop() {
  // Serial: PORTAL enters maintenance open AP with CommonPlatform PIN 12134;
  // CONFIG erases just this display's saved credentials (as in ver0.01).
  if(Serial.available()) {
    String cmd=Serial.readStringUntil('\n');cmd.trim();cmd.toUpperCase();
    if(cmd=="CONFIG") {
      settings.begin("energy28",false);settings.clear();settings.end();
      clearRememberedRouters();
      ESP.restart();
    } else if(cmd=="PORTAL") {
      portalForced=true;beginPortal(wifiSsid.length()>0);
    } else if(cmd=="STATUS") {
      Serial.println(jsonStatus());
    } else if(cmd=="MQTT") {
      Serial.println(mqttDebugJson());
    }
  }
  if(routesReady)webServer.handleClient();
  // CRITICAL: scheduled /save, /restart-esp and /forget-credentials
  // must execute even when credentials are empty. In ver0.13 the
  // no-credentials branch returned BEFORE the restart timer was checked.
  if(restartAt && dueAt(restartAt)) {
    Serial.println("[WEB] Saved configuration: restarting ESP32");
    Serial.flush();
    delay(60);
    ESP.restart();
    return;
  }
  servicePortal();
  if(wifiSsid.isEmpty() || mqttUser.isEmpty() || mqttPass.isEmpty()) {
    if(!portalActive && !portalRetryAt && dueAt(nextWifiAttempt)) {
      nextWifiAttempt=millis()+30000;beginPortal(false);
    }
    handleTouch(); // The gear works on the setup screen too.
    if(settingsOpen && millis()-lastDraw>10000UL)render();
    delay(10);return;
  }
  maintainConnection();
  handleTouch();
  serviceDeviceProbes();
  serviceHistoryChart();
  if(mqtt.connected()) {
    if(displayStateRequested || dueAt(nextDisplayHealthAt)) {
      displayStateRequested=false;publishDisplaySnapshot(true);
    }
    if(millis()-lastPoll>60000UL)requestAll();
    if(displayOtaJob.pending)performDisplayOta();
  }
  if(!portalActive || settingsOpen) {
    if(chartOpen) {
      if(chartDirty && millis()-lastDraw>=800UL)render();
    } else if((uiRefreshPending && millis()-lastDraw>=UI_REFRESH_MIN_MS) ||
              millis()-lastDraw>10000UL)render();
  }
  if(millis()-lastHealthLine>60000UL) {
    lastHealthLine=millis();Serial.println(jsonStatus());
  }
  delay(8);
}
