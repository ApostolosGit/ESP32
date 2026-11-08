/*
  Energy Monitor ver0.01 - 2.8" CYD ESP32-2432S028 (dual USB) / 320x240 landscape.
  DDS: Metritis DEI Artemida; JSY: Nestou 5A; Ventilation; Boiler.
  Live read-only dashboard mirroring the MQTT.app state schema.
  Factory backup before flashing. Values are NEVER hard-coded samples.
  AP setup (first boot): SSID ESP28-SETUP-XXXX, password Display28!, http://192.168.4.1
  Sensitive Wi-Fi/MQTT settings stay in on-device Preferences, never in source.
  MQTT TLS 8883, trusted ISRG Root X1, no control commands; refresh requests only.
  Libraries: Arduino_GFX 1.4.9, XPT2046_Touchscreen, PubSubClient, ArduinoJson 6.
*/
#define FW_VERSION "ver0.03"
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
#include "TrustedCA.h"

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
String ids[4];
bool hasState[4] = {false,false,false,false};
StaticJsonDocument<6144> state[4];
StaticJsonDocument<6144> incomingState; // Global: avoid 6 KB stack allocation in MQTT callback

// CommonPlatform 1.06-compatible network lifecycle for classic ESP32.
// Deliberately NOT the C3-only platform source (target-specific adapters differ).
bool portalActive=false, portalForced=false, routesReady=false, timeConfigured=false;
bool apDnsRunning=false, pendingPortalStop=false;
const char AP_WEB_PIN[] = "12134"; // CommonPlatform 1.06
const uint32_t AP_WEB_SESSION_MS = 15UL*60UL*1000UL;
const uint32_t AP_PIN_COOLDOWN_MS = 30000UL;
char apWebSessionToken[33] = {};
uint32_t apWebSessionStartedMs=0, apPinLockedUntil=0;
uint8_t apPinFailures=0;
uint32_t portalStartMs=0, portalHealthMs=0, portalRetryAt=0, portalAttempts=0, portalFailures=0;
bool wifiEverConnected=false, mqttEverConnected=false, otaImageComplete=false;
uint32_t wifiFailures=0, mqttFailures=0, wifiReconnects=0, mqttReconnects=0;
uint32_t disconnectedSince=0, nextWifiAttempt=0, nextMqttAttempt=0;
uint32_t lastTxTune=0, lastHealthLine=0;
uint8_t wifiRetryIdx=0, mqttRetryIdx=0, txLevel=0;

const wifi_power_t TX_STEPS[] = {WIFI_POWER_8_5dBm, WIFI_POWER_11dBm,
                                WIFI_POWER_13dBm, WIFI_POWER_15dBm};
const uint8_t TX_TENTHS[] = {85,110,130,150};
const uint32_t WIFI_RETRY_MS[] = {10000,15000,30000,60000};
const uint32_t MQTT_BOOT_RETRY_MS[] = {10000,30000,60000,120000,300000};
const uint32_t MQTT_RUN_RETRY_MS[] = {5000,10000,30000};
bool dueAt(uint32_t target) {return (int32_t)(millis()-target)>=0;}
String deviceId() {return String(DEVICE_ID_PREFIX)+String((uint32_t)ESP.getEfuseMac(),HEX);}
bool setTxLevel(uint8_t level,const char *reason) {
  if(level>3)level=3; // Hardware cap: 15.0 dBm, NEVER a higher enum.
  if(!WiFi.setTxPower(TX_STEPS[level])) {
    Serial.printf("[TX] setTxPower failed: %s\n",reason);return false;
  }
  txLevel=level;lastTxTune=millis();
  Serial.printf("[TX] requested max %d.%d dBm (%s)\n",
                TX_TENTHS[level]/10,TX_TENTHS[level]%10,reason);
  return true;
}
void retuneWifi() {
  if(WiFi.status()!=WL_CONNECTED || millis()-lastTxTune<60000UL)return;
  int rssi=WiFi.RSSI();
  uint8_t target=(rssi>=-66)?0:(rssi>=-74)?1:(rssi>=-81)?2:3;
  if(target!=txLevel)setTxLevel(target,"RSSI");
  else lastTxTune=millis();
}
String localIp() {return WiFi.status()==WL_CONNECTED?WiFi.localIP().toString():String("--");}
String jsonStatus() {
  StaticJsonDocument<1152> j;
  j["fw"]=FW_VERSION;
  j["platform_compat"]="1.06";j["chip"]="ESP32-D0WD";j["device_id"]=deviceId();
  j["uptime_sec"]=millis()/1000UL;j["heap_free"]=ESP.getFreeHeap();
  j["largest_block"]=heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
  j["sta_connected"]=WiFi.status()==WL_CONNECTED;j["sta_ip"]=localIp();
  j["rssi"]=WiFi.status()==WL_CONNECTED?WiFi.RSSI():-127;
  j["tx_max_tenths_dbm"]=TX_TENTHS[txLevel];
  j["ap_active"]=portalActive;j["mqtt_connected"]=mqtt.connected();
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
  if(!portalActive || !apWebSessionToken[0])return false;
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
  webServer.sendHeader("Cache-Control","no-store");
  webServer.send(status,"text/html; charset=utf-8",
    "<!doctype html><html><head><meta name='viewport' "
    "content='width=device-width,initial-scale=1'></head>"
    "<body style='background:#081522;color:#fff;font:20px Arial;padding:16px'>"
    "<h3>Energy Monitor PIN</h3><p>Enter the 5-digit setup PIN.</p>"
    "<form method='post' action='/ap-login'>"
    "<input name='pin' type='password' inputmode='numeric' "
    "pattern='[0-9]{5}' minlength='5' maxlength='5' required autofocus>"
    "<button type='submit'>Open</button></form></body></html>");
}
bool requireApWebPin() {
  if(!portalActive)return false;
  if(apWebSessionValid())return true;
  sendPinPage(webServer.method()==HTTP_GET?200:403);
  return false;
}
void handleApWebLogin() {
  if(!portalActive){webServer.send(404,"text/plain","Not found");return;}
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
uint8_t portalOctet() {
  if(WiFi.status()==WL_CONNECTED && WiFi.localIP()[3]>=2 &&
     WiFi.localIP()[3]<=254)return WiFi.localIP()[3];
  return 88; // No pre-reserved static IP for this board.
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
void card(int x,int y,int w,int h) {
  lcd->fillRoundRect(x,y,w,h,7,UI_PANEL);
  lcd->drawRoundRect(x,y,w,h,7,UI_BORDER);
}
void line(int x,int y,const String &label,const String &value,
          uint16_t valueColor=UI_WHITE) {
  txt(x,y,label,UI_MUTED,1);
  txt(x+103,y-3,value,valueColor,2);
}
void topbar() {
  lcd->fillScreen(UI_BG);
  txt(7,3,"ENERGY MONITOR",UI_CYAN,2);
  txt(232,4,FW_VERSION,UI_CYAN,1);
  txt(285,17,mqtt.connected()?"MQTT":"OFF",mqtt.connected()?UI_GREEN:UI_RED,1);
  const String tabNames[4]={"ΑΡΤΕΜΙΔΑ","ΝΕΣΤΟΥ 5Α","VENT","BOILER"};
  for(int i=0;i<4;i++) {
    int x=i*80+2;
    lcd->fillRoundRect(x,29,76,31,5,(currentPage==i)?UI_ACTIVE:UI_TABOFF);
    txt(x+3,40,tabNames[i],UI_WHITE,1);
  }
}
void footer() {
  uint16_t c=live(currentPage)?UI_GREEN:UI_MUTED;
  String s=live(currentPage)?"LIVE":"ΑΝΑΜΟΝΗ ΔΕΔΟΜΕΝΩΝ";
  lcd->fillRect(0,226,320,14,UI_BG);
  txt(7,231,s,c,1);
  if(live(currentPage)) txt(248,231,String((millis()-lastRx[currentPage])/1000)+"s",UI_MUTED,1);
}
void ddsPage() {
  txt(8,65,"ΜΕΤΡΗΤΗΣ ΔΕΗ ΑΡΤΕΜΙΔΑ",UI_WHITE,1);
  card(5,79,310,140);
  txt(12,88,"ΙΣΧΥΣ",UI_MUTED,1);
  double p=live(0)?numberOf(0,"power"):NAN;
  txt(102,82,fmt(p,0),powerColor(p),3);
  txt(275,92,"W",UI_MUTED,2);
  txt(12,120,"ΔΙΑΦΟΡΑ",UI_MUTED,1);
  txt(106,116,live(0) && state[0]["has_utility"]==true &&
    state[0]["estimate_valid"]==true?shown(0,"z_diff",2):"--",UI_RED,2);
  txt(216,122,"kWh",UI_MUTED,1);
  if(live(0) && state[0]["has_utility"]==true && state[0]["estimate_valid"]==true) {
    double d=numberOf(0,"z_diff");
    txt(256,122,"("+fmt(d*euroPerKwh,2)+"€)",UI_MUTED,1);
  }
  txt(12,154,"ΕΚΤΙΜΗΣΗ",UI_MUTED,1);
  txt(106,149,live(0)&&state[0]["estimate_valid"]==true?shown(0,"z_now",2):"--",UI_WHITE,2);
  txt(254,155,"kWh",UI_MUTED,1);
  txt(12,190,"ΤΕΛ. ΔΕΗ",UI_MUTED,1);
  txt(106,182,live(0)&&state[0]["has_utility"]==true?shown(0,"z_ref",0):"--",UI_WHITE,2);
  txt(242,189,"kWh",UI_MUTED,1);
  txt(268,206,isoDay(0),UI_CYAN,1);
}
void jsyPage() {
  txt(7,64,"ΝΕΣΤΟΥ 5Α",UI_WHITE,1);
  txt(123,64,"ΣΥΝΟΛΙΚΗ ΙΣΧΥΣ",UI_MUTED,1);
  double total=live(1)?numberOf(1,"power_total"):NAN;
  if(isfinite(total) && total>0 && live(1) && (state[1]["power_direction"].as<int>() & 0x08))
    total=-total;
  txt(8,77,fmt(total,0),powerColor(total),2);
  txt(108,80,"W",UI_MUTED,1);
  for(int i=0;i<3;i++) {
    int x=4+i*106; card(x,99,101,44);
    txt(x+6,105,"ΦΑΣΗ "+String("RST"[i]),UI_WHITE,1);
    String f="p"+String(i+1);
    double power=live(1)?numberOf(1,f.c_str()):NAN;
    String dir="dir"+String(i+1);
    if(isfinite(power) && power>0 && (String(state[1][dir] | "")=="REV" || (String(state[1][dir] | "").isEmpty() && numberOf(1,("i"+String(i+1)).c_str())<0))) power=-power;
    txt(x+5,118,fmt(power,0),powerColor(power),2);
  }
  for(int z=1;z<=2;z++) {
    int x=z==1?4:162, w=154;
    card(x,148,w,75);
    txt(x+5,152,z==1?"Ζ1 ΑΚΡΙΒΟ":"Ζ2 ΟΙΚΟΝΟΜΙΚΟ",z==1?UI_WHITE:UI_CYAN,1);
    const bool ok=live(1) && state[1]["has_utility"]==true && state[1]["estimate_valid"]==true;
    String prefix="z"+String(z)+"_";
    double diff=ok?numberOf(1,(prefix+"diff").c_str()):NAN;
    txt(x+5,163,fmt(diff,2),UI_RED,2);
    txt(x+90,170,"kWh",UI_MUTED,1);
    if(isfinite(diff))txt(x+5,182,fmt(diff*euroPerKwh,2)+"€",UI_MUTED,1);
    txt(x+59,182,"ΕΚΤ.",UI_MUTED,1);
    txt(x+90,182,live(1) && state[1]["estimate_valid"]==true?
      shown(1,(prefix+"now").c_str(),2):"--",UI_WHITE,1);
    txt(x+5,196,"ΔΕΗ",UI_MUTED,1);
    txt(x+41,196,live(1)&&state[1]["has_utility"]==true?
      shown(1,(prefix+"ref").c_str(),0):"--",UI_WHITE,1);
    txt(x+109,210,isoDay(1),UI_MUTED,1);
  }
}
void ventPage() {
  txt(8,65,"ΕΞΑΕΡΙΣΜΟΣ / VENTILATION",UI_WHITE,1);
  const char *keys[3]={"temp_outside","temp_inside","temp_middle"};
  const char *titles[3]={"ΕΞΩ","ΜΕΣΑ","ΕΝΔΙΑΜΕΣΑ"};
  for(int i=0;i<3;i++) {
    int x=4+i*106;
    card(x,79,101,47);
    txt(x+6,85,titles[i],UI_MUTED,1);
    String v=shown(2,keys[i],1)+"°C";
    txt(x+6,102,v,UI_WHITE,2);
  }
  card(4,132,312,65);
  txt(10,136,"DHT11 OUTSIDE",UI_CYAN,1);
  txt(150,136,shown(2,"dht_temp_outside",1)+"°C",UI_WHITE,1);
  txt(224,136,"H "+shown(2,"humidity_outside",1)+"%",UI_WHITE,1);
  txt(10,148,"AH "+shown(2,"absolute_humidity_outside",1)+"g/m3",UI_MUTED,1);
  txt(182,148,"FEELS "+shown(2,"feels_like_outside",1)+"°C",UI_MUTED,1);
  lcd->drawFastHLine(10,161,300,UI_BORDER);
  txt(10,166,"DHT11 INSIDE",UI_CYAN,1);
  txt(150,166,shown(2,"dht_temp_inside",1)+"°C",UI_WHITE,1);
  txt(224,166,"H "+shown(2,"humidity_inside",1)+"%",UI_WHITE,1);
  txt(10,179,"AH "+shown(2,"absolute_humidity_inside",1)+"g/m3",UI_MUTED,1);
  txt(182,179,"FEELS "+shown(2,"feels_like_inside",1)+"°C",UI_MUTED,1);
  card(4,200,312,24);
  String mode=live(2)?String(state[2]["mode"]|"--"):"--";
  String fan=live(2)?(state[2]["fan_on"].as<bool>()?"ON":"OFF"):"--";
  String speed=live(2)?String(state[2]["speed"]|"--"):"--";
  String season=live(2)?String(state[2]["season"]|"--"):"--";
  bool safe=live(2) && state[2]["safe_mode"]==true;
  txt(10,207,"MODE "+mode+"  FAN "+fan+"  "+speed, safe?UI_RED:UI_WHITE,1);
  txt(227,207,season,UI_MUTED,1);
}
void boilerPage() {
  txt(8,65,"BOILER / ΘΕΡΜΟΚΡΑΣΙΕΣ",UI_WHITE,1);
  const char* labels[4]={"Θ1  GPIO5","Θ2  GPIO6","Θ3  GPIO7","Θ4  GPIO21"};
  for(int i=0;i<4;i++) {
    int x=(i%2)*157+4;
    int y=79+(i/2)*72;
    card(x,y,152,68);
    txt(x+9,y+7,labels[i],UI_MUTED,1);
    String field="temp_"+String(i+1);
    String statusField=field+"_status";
    bool disconnected=live(3) &&
      String(state[3][statusField] | "")=="not connected";
    String value=disconnected?"NC":shown(3,field.c_str(),1)+"°C";
    txt(x+9,y+28,value,disconnected?UI_RED:UI_WHITE,2);
  }
}
void render() {
  topbar();
  switch(currentPage) {
    case 0:ddsPage();break;
    case 1:jsyPage();break;
    case 2:ventPage();break;
    case 3:boilerPage();break;
  }
  footer();
  lastDraw=millis();
}
void drawSetup() {
  lcd->fillScreen(UI_BG);
  txt(12,15,"ENERGY MONITOR",UI_CYAN,2);
  txt(264,19,FW_VERSION,UI_CYAN,1);
  txt(14,53,setupMode?"SETUP WIFI + MQTT":"AP+STA RECOVERY",UI_YELLOW,2);
  txt(14,88,"Connect to WiFi AP:",UI_WHITE,1);
  txt(14,105,apSsid,UI_CYAN,2);
  txt(14,135,"WiFi OPEN  PIN: 12134",UI_YELLOW,1);
  txt(14,166,"Open browser:",UI_WHITE,1);
  txt(14,184,"http://"+WiFi.softAPIP().toString(),UI_CYAN,2);
  txt(14,217,"No credentials on GitHub",UI_MUTED,1);
}
// A local portal requires physical access to the screen for its temporary code.
// Every POST for configuration, including on the STA interface, requires this code.
void registerWebRoutes() {
  if(routesReady)return;
  routesReady=true;
  const char *headers[]={"Cookie"};
  webServer.collectHeaders(headers,1);
  webServer.on("/ap-login",HTTP_POST,handleApWebLogin);
  webServer.on("/health",HTTP_GET,[](){
    // CommonPlatform: every AP page/action is PIN-protected while the AP is active.
    if(portalActive && !requireApWebPin())return;
    webServer.sendHeader("Cache-Control","no-store");
    webServer.send(200,"application/json; charset=utf-8",jsonStatus());
  });
  webServer.on("/",HTTP_GET,[](){
    if(!portalActive) {
      webServer.send(200,"text/html; charset=utf-8",
        "<h3>Energy Monitor "+String(FW_VERSION)+
        "</h3><p>Read-only dashboard</p><a href='/health'>Health</a>");
      return;
    }
    if(!requireApWebPin())return;
    String h="<html><head><meta name='viewport' content='width=device-width,initial-scale=1'></head>";
    h+="<body style='background:#071321;color:white;font:16px sans-serif'><h2>Energy Monitor "+String(FW_VERSION)+"</h2>";
    h+="<p>CommonPlatform v1.06: open WiFi AP + PIN 12134; 15-minute session.</p>";
    h+="<form action='/save' method='post'>";
    h+="WiFi SSID<br><input name='ssid' maxlength='32' required><br>WiFi password<br><input type='password' name='wpass' maxlength='64'><br>";
    h+="HiveMQ host<br><input name='broker' size='38' maxlength='127' value='"+String(BROKER_DEFAULT)+"'><br>";
    h+="HiveMQ user<br><input name='user' maxlength='64' required><br>HiveMQ password<br><input name='mpass' type='password' maxlength='64' required><br>";
    h+="Price (euro/kWh)<br><input name='price' value='0.25'><br><br>";
    h+="<button type='submit'>Save & restart</button></form>";
    h+="<p><a href='/health'>Health JSON</a> | <a href='/ota'>Local OTA update</a></p>";
    h+="<p>OTA requires <b>Display2.8-app-only.bin</b>, not the full merged BIN.</p>";
    h+="</body></html>";
    webServer.sendHeader("Cache-Control","no-store");
    webServer.send(200,"text/html; charset=utf-8",h);
  });
  webServer.on("/save",HTTP_POST,[](){
    if(!requireApWebPin())return;
    String ssid=webServer.arg("ssid"),host=webServer.arg("broker"),user=webServer.arg("user");
    String mpass=webServer.arg("mpass"),wpass=webServer.arg("wpass");
    if(!ssid.length() || ssid.length()>32 || wpass.length()>64 ||
       host.length()>127 || host.indexOf('/')>=0 || host.indexOf(':')>=0 ||
       !host.length() || !user.length() || user.length()>64 ||
       !mpass.length() || mpass.length()>64) {
      webServer.send(400,"text/plain","Invalid configuration");return;
    }
    settings.begin("energy28",false);
    settings.putString("ssid",ssid);
    settings.putString("wpass",wpass);
    settings.putString("user",user);
    settings.putString("mpass",mpass);
    settings.putString("broker",host);
    float price=webServer.arg("price").toFloat();
    settings.putFloat("price",(price>0.0f && price<10.0f)?price:0.25f);
    settings.end();
    webServer.send(200,"text/html","Saved. Device restarting.");
    delay(400);ESP.restart();
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
    if(!portalActive || !apWebSessionValid())return;
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
  apSsid="ESP28-SETUP-"+String((uint16_t)ESP.getEfuseMac(),HEX);
  WiFi.mode(recovery?WIFI_AP_STA:WIFI_AP);
  if(!setTxLevel(0,"portal startup")) {
    portalFailures++;portalRetryAt=millis()+30000;
    Serial.println("[TX] Portal disabled until safe TX limit can be set");return;
  }
  uint8_t octet=portalOctet();
  IPAddress ip(192,168,2,octet);
  if(!WiFi.softAPConfig(ip,ip,IPAddress(255,255,255,0)) ||
     !WiFi.softAP(apSsid.c_str(),nullptr,1,0,4)) {
    portalFailures++;portalRetryAt=millis()+30000;
    Serial.println("[AP] open AP start failed; 30s retry");return;
  }
  portalActive=true;
  portalStartMs=millis();
  portalHealthMs=portalStartMs;
  portalRetryAt=0;
  resetApWebSession();
  apPinFailures=0;apPinLockedUntil=0;
  registerWebRoutes();
  portalDns.start(53,"*",WiFi.softAPIP());
  apDnsRunning=true;
  Serial.printf("[AP] OPEN, PIN 12134, %s at http://%s\n",
    recovery?"AP+STA recovery":"AP-only setup",WiFi.softAPIP().toString().c_str());
  drawSetup();
}
void servicePortal() {
  if(portalActive) {
    if(apDnsRunning)portalDns.processNextRequest();
    if(millis()-portalHealthMs>=3000) {
      portalHealthMs=millis();
      // Avoid repeatedly turning the radio off on transient readback failures.
      if(millis()-portalStartMs>5000 &&
         WiFi.getMode()!=WIFI_AP && WiFi.getMode()!=WIFI_AP_STA) {
        portalActive=false;
        if(apDnsRunning)portalDns.stop();
        apDnsRunning=false;
        resetApWebSession();
        portalFailures++;
        portalRetryAt=millis()+30000;
        Serial.println("[AP] interface missing, retry scheduled");
      }
    }
  } else if(portalRetryAt && dueAt(portalRetryAt)) {
    portalRetryAt=0;
    if(wifiSsid.isEmpty() || portalForced || (disconnectedSince && millis()-disconnectedSince>=20000))
      beginPortal(!wifiSsid.isEmpty());
  }
}
void stopRecoveryPortal() {
  if(!portalActive || !wifiSsid.length() || portalForced)return;
  if(apDnsRunning)portalDns.stop();
  apDnsRunning=false;
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  portalActive=false;
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
void mqttMessage(char *topic,byte *payload,unsigned int len) {
  String t=topic, prefix="home/energy/";
  if(!t.startsWith(prefix)) return;
  String rest=t.substring(prefix.length());
  int div=rest.indexOf('/');if(div<1) return;
  String id=rest.substring(0,div), suffix=rest.substring(div+1);
  if(suffix=="state") {
    incomingState.clear();
    DeserializationError err=deserializeJson(incomingState,(const char *)payload,len);
    if(err) {Serial.println(String("Invalid JSON: ")+err.c_str());return;}
    int type=typeById(id);if(type<0) type=typeByDoc(incomingState);
    if(type<0) return;
    state[type].clear();
    state[type].set(incomingState.as<JsonObjectConst>());
    hasState[type]=true;
    ids[type]=id;
    lastRx[type]=millis();
    if(currentPage==type) render();
  } else if(suffix=="status") {
    int type=typeById(id);
    if(type>=0 && (ids[type].isEmpty() || ids[type]==id)) ids[type]=id;
  } else if(suffix=="health") {
    // Health frames advertise identity but are not live measurement samples.
    int type=typeById(id);
    if(type>=0 && ids[type].isEmpty()) ids[type]=id;
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
}
void requestAll() {
  if(!mqtt.connected())return;
  for(int i=0;i<4;i++)if(!ids[i].isEmpty()) {
    String topic="home/energy/"+ids[i]+"/request";
    mqtt.publish(topic.c_str(),"update"); // Read-only request, NO actuator command.
  }
  lastPoll=millis();
}
void startStation() {
  WiFi.mode(portalActive?WIFI_AP_STA:WIFI_STA);
  if(!setTxLevel(0,"before STA begin")) {
    Serial.println("[WiFi] TX cap failed, no STA connect");
    nextWifiAttempt=millis()+10000;wifiFailures++;return;
  }
  WiFi.begin(wifiSsid.c_str(),wifiPass.c_str());
  wifiReconnects++;
  nextWifiAttempt=millis()+WIFI_RETRY_MS[wifiRetryIdx];
  if(wifiRetryIdx<3)wifiRetryIdx++;
}
void maintainConnection() {
  if(WiFi.status()!=WL_CONNECTED) {
    if(!disconnectedSince)disconnectedSince=millis();
    if(dueAt(nextWifiAttempt) && wifiSsid.length()) {
      Serial.println("[WiFi] reconnecting");startStation();
    }
    if(!portalActive && wifiSsid.length() && millis()-disconnectedSince>20000UL) {
      beginPortal(true); // Recovery keeps STA reconnecting (AP+STA).
    }
    return;
  }
  if(!wifiEverConnected) {wifiEverConnected=true;Serial.println("[WiFi] connected");}
  if(disconnectedSince) {
    wifiRetryIdx=0;disconnectedSince=0;
    Serial.printf("[WiFi] recovered IP=%s\n",localIp().c_str());
  }
  if(portalActive && !portalForced)stopRecoveryPortal();
  retuneWifi();
  if(mqtt.connected()) {
    mqtt.loop();mqttRetryIdx=0;
    return;
  }
  // TLS certificate verification must never run against an unknown clock.
  if(time(nullptr)<1600000000)return;
  if(!dueAt(nextMqttAttempt))return;
  mqttReconnects++;
  const String id=deviceId();
  Serial.printf("[MQTT] TLS connect attempt %lu\n",(unsigned long)mqttReconnects);
  if(mqtt.connect(id.c_str(),mqttUser.c_str(),mqttPass.c_str())) {
    mqtt.subscribe("home/energy/+/state");
    mqtt.subscribe("home/energy/+/status");
    mqtt.subscribe("home/energy/+/health");
    Serial.println("[MQTT] connected and subscribed");
    mqttEverConnected=true;
    mqttRetryIdx=0;
    lastPoll=millis()-55000UL;render();
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
void handleTouch() {
  static bool held=false;
  bool pressed=touch.touched();
  if(!pressed) {held=false;return;}
  if(held)return;
  held=true;
  TS_Point p=touch.getPoint();
  if(p.z<100)return;
  int x=constrain((int)map(p.x,250,3800,0,319),0,319);
  int y=constrain((int)map(p.y,250,3800,0,239),0,239);
  Serial.printf("Touch %d,%d -> %d,%d\n",p.x,p.y,x,y);
  if(y>=29 && y<=64) {
    int newPage=constrain(x/80,0,3);
    if(currentPage!=newPage) {currentPage=newPage;render();}
  }
}
void setup() {
  Serial.begin(115200);
  Serial.printf("Energy Monitor %s / %s\n",FW_VERSION,COMPAT_PLATFORM);
  pinMode(21,OUTPUT); digitalWrite(21,HIGH);
  lcd->begin();lcd->invertDisplay(true);
  SPI.begin(25,39,32,33);
  touch.begin();touch.setRotation(1);
  readConfig();
  tls.setCACert(LE_ROOT_CA);
  mqtt.setServer(broker.c_str(),8883);
  mqtt.setBufferSize(12288);
  mqtt.setCallback(mqttMessage);
  configTzTime("EET-2EEST,M3.5.0/3,M10.5.0/4","pool.ntp.org","time.google.com");
  timeConfigured=true;
  if(wifiSsid.isEmpty() || mqttUser.isEmpty() || mqttPass.isEmpty()) {
    beginPortal(false);return;
  }
  WiFi.mode(WIFI_STA);
  registerWebRoutes(); // Local read-only /health; credential POST closed outside portal.
  startStation();       // Nonblocking; AP+STA fallback after 20 seconds.
  render();
}
void loop() {
  // Serial: PORTAL enters maintenance open AP with CommonPlatform PIN 12134;
  // CONFIG erases just this display's saved credentials (as in ver0.01).
  if(Serial.available()) {
    String cmd=Serial.readStringUntil('\n');cmd.trim();cmd.toUpperCase();
    if(cmd=="CONFIG") {
      settings.begin("energy28",false);settings.clear();settings.end();
      ESP.restart();
    } else if(cmd=="PORTAL") {
      portalForced=true;beginPortal(wifiSsid.length()>0);
    } else if(cmd=="STATUS") {
      Serial.println(jsonStatus());
    }
  }
  if(routesReady)webServer.handleClient();
  servicePortal();
  if(wifiSsid.isEmpty() || mqttUser.isEmpty() || mqttPass.isEmpty()) {
    if(!portalActive && !portalRetryAt && dueAt(nextWifiAttempt)) {
      nextWifiAttempt=millis()+30000;beginPortal(false);
    }
    delay(10);return;
  }
  maintainConnection();
  handleTouch();
  if(mqtt.connected() && millis()-lastPoll>60000UL)requestAll();
  if(millis()-lastDraw>10000UL && !portalActive)render();
  if(millis()-lastHealthLine>60000UL) {
    lastHealthLine=millis();Serial.println(jsonStatus());
  }
  delay(8);
}
