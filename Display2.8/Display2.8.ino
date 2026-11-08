/*
  Energy Monitor 2.8" CYD ESP32-2432S028 (dual USB) / 320x240 landscape.
  DDS: Metritis DEI Artemida; JSY: Nestou 5A; Ventilation; Boiler.
  Live read-only dashboard mirroring the MQTT.app state schema.
  Factory backup before flashing. Values are NEVER hard-coded samples.
  AP setup (first boot): SSID ESP28-SETUP-XXXX, password Display28!, http://192.168.4.1
  Sensitive Wi-Fi/MQTT settings stay in on-device Preferences, never in source.
  MQTT TLS 8883, trusted ISRG Root X1, no control commands; refresh requests only.
  Libraries: Arduino_GFX 1.4.9, XPT2046_Touchscreen, PubSubClient, ArduinoJson 6.
*/
#include <Arduino.h>
#include <SPI.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <WebServer.h>
#include <Preferences.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <Arduino_GFX_Library.h>
#include <XPT2046_Touchscreen.h>
#include <time.h>
#include <math.h>
#include "TrustedCA.h"

Arduino_DataBus *db = new Arduino_ESP32SPI(2, 15, 14, 13, 12);
Arduino_GFX *lcd = new Arduino_ST7789(db, GFX_NOT_DEFINED, 1, true, 240, 320);
XPT2046_Touchscreen touch(33, 36);
WiFiClientSecure tls;
PubSubClient mqtt(tls);
WebServer webServer(80);
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
  static const uint8_t PI[]={0x1F,0x11,0x11,0x11,0x11,0x11,0x11}; // Π
  static const uint8_t SG[]={0x1F,0x10,0x08,0x04,0x08,0x10,0x1F}; // Σ
  static const uint8_t PH[]={0x04,0x0E,0x15,0x15,0x15,0x0E,0x04}; // Φ
  static const uint8_t PS[]={0x15,0x15,0x15,0x0E,0x04,0x04,0x04}; // Ψ
  static const uint8_t OM[]={0x0E,0x11,0x11,0x11,0x0A,0x1B,0x00}; // Ω
  static const uint8_t EURO[]={0x0E,0x11,0x1E,0x08,0x1E,0x11,0x0E};
  switch(c) {
    case 0x393: return G; case 0x394: return D;
    case 0x398: return TH; case 0x39B: return L;
    case 0x39E: return XI; case 0x3A0: return PI;
    case 0x3A3: return SG; case 0x3A6: return PH;
    case 0x3A8: return PS; case 0x3A9: return OM;
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
  txt(270,7,mqtt.connected()?"MQTT":"OFF",mqtt.connected()?UI_GREEN:UI_RED,1);
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
    if(isfinite(power) && power>0 && String(state[1][dir] | "")=="REV") power=-power;
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
  txt(14,53,"SETUP WIFI + MQTT",UI_YELLOW,2);
  txt(14,88,"Connect to WiFi AP:",UI_WHITE,1);
  txt(14,105,apSsid,UI_CYAN,2);
  txt(14,136,"PASSWORD: Display28!",UI_WHITE,1);
  txt(14,166,"Open browser:",UI_WHITE,1);
  txt(14,184,"http://192.168.4.1",UI_CYAN,2);
  txt(14,217,"No credentials on GitHub",UI_MUTED,1);
}
void beginSetup() {
  setupMode=true;
  apSsid="ESP28-SETUP-"+String((uint16_t)ESP.getEfuseMac(),HEX);
  WiFi.mode(WIFI_AP);
  WiFi.softAP(apSsid.c_str(),"Display28!");
  webServer.on("/",HTTP_GET,[](){
    String h="<html><head><meta name='viewport' content='width=device-width,initial-scale=1'></head>";
    h+="<body style='background:#071321;color:white;font:16px sans-serif'><h2>Energy Monitor 2.8</h2>";
    h+="<form action='/save' method='post'>";
    h+="WiFi SSID<br><input name='ssid' required><br>WiFi password<br><input type='password' name='wpass'><br>";
    h+="HiveMQ host<br><input name='broker' size='42' value='"+String(BROKER_DEFAULT)+"'><br>";
    h+="HiveMQ user<br><input name='user' required><br>HiveMQ password<br><input name='mpass' type='password' required><br>";
    h+="Price (euro/kWh)<br><input name='price' value='0.25'><br><br>";
    h+="<button type='submit'>Save & restart</button></form><p>MQTT TLS: 8883. Read-only measurements.</p></body></html>";
    webServer.send(200,"text/html; charset=utf-8",h);
  });
  webServer.on("/save",HTTP_POST,[](){
    if(!webServer.hasArg("ssid") || !webServer.hasArg("user") ||
       !webServer.hasArg("mpass") || webServer.arg("ssid").isEmpty()) {
      webServer.send(400,"text/plain","Missing fields"); return;
    }
    settings.begin("energy28",false);
    settings.putString("ssid",webServer.arg("ssid"));
    settings.putString("wpass",webServer.arg("wpass"));
    settings.putString("user",webServer.arg("user"));
    settings.putString("mpass",webServer.arg("mpass"));
    settings.putString("broker",webServer.arg("broker"));
    float p=webServer.arg("price").toFloat();
    settings.putFloat("price",(p>0.0f && p<10.0f)?p:0.25f);
    settings.end();
    webServer.send(200,"text/html","Saved. Device is restarting.");
    delay(700);ESP.restart();
  });
  webServer.begin();
  drawSetup();
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
  settings.begin("energy28",true);
  wifiSsid=settings.getString("ssid","");
  wifiPass=settings.getString("wpass","");
  mqttUser=settings.getString("user","");
  mqttPass=settings.getString("mpass","");
  broker=settings.getString("broker",BROKER_DEFAULT);
  euroPerKwh=settings.getFloat("price",0.25f);
  settings.end();
  if(broker.isEmpty()) broker=BROKER_DEFAULT;
}
void requestAll() {
  if(!mqtt.connected())return;
  for(int i=0;i<4;i++) if(!ids[i].isEmpty()) {
    String topic="home/energy/"+ids[i]+"/request";
    mqtt.publish(topic.c_str(),"update");
  }
  lastPoll=millis();
}
void maintainConnection() {
  if(WiFi.status()!=WL_CONNECTED) {
    if(millis()-lastWifiTry>10000) {
      lastWifiTry=millis(); WiFi.reconnect();
    }
    return;
  }
  if(mqtt.connected()) {mqtt.loop();return;}
  if(millis()-lastMqttTry<8000) return;
  lastMqttTry=millis();
  String id="display28-"+String((uint32_t)ESP.getEfuseMac(),HEX);
  Serial.println("Connecting secure MQTT...");
  if(mqtt.connect(id.c_str(),mqttUser.c_str(),mqttPass.c_str())) {
    mqtt.subscribe("home/energy/+/state");
    mqtt.subscribe("home/energy/+/status");
    mqtt.subscribe("home/energy/+/health");
    lastPoll=millis()-55000; render();
  } else {
    Serial.printf("MQTT connect error: %d\n",mqtt.state());
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
  pinMode(21,OUTPUT); digitalWrite(21,HIGH);
  lcd->begin();lcd->invertDisplay(true);
  SPI.begin(25,39,32,33);
  touch.begin(); touch.setRotation(1);
  readConfig();
  if(wifiSsid.isEmpty() || mqttUser.isEmpty() || mqttPass.isEmpty()) {
    beginSetup();return;
  }
  render();
  WiFi.mode(WIFI_STA);
  WiFi.begin(wifiSsid.c_str(),wifiPass.c_str());
  uint32_t st=millis();
  while(WiFi.status()!=WL_CONNECTED && millis()-st<20000)delay(200);
  if(WiFi.status()!=WL_CONNECTED) {beginSetup();return;}
  configTime(0,0,"pool.ntp.org","time.google.com");
  st=millis();
  while(time(nullptr)<1600000000 && millis()-st<12000) delay(200);
  // CA validation requires a correct system clock, never use setInsecure().
  tls.setCACert(LE_ROOT_CA);
  mqtt.setServer(broker.c_str(),8883);
  mqtt.setBufferSize(12288);
  mqtt.setCallback(mqttMessage);
  lastMqttTry=millis()-10000;
  render();
}
void loop() {
  if(setupMode) {webServer.handleClient();delay(10);return;}
  // Reset local credentials using USB Serial Monitor: CONFIG + Enter.
  if(Serial.available()) {
    String cmd=Serial.readStringUntil('\n');cmd.trim();
    if(cmd=="CONFIG") {
      settings.begin("energy28",false);settings.clear();settings.end();
      ESP.restart();
    }
  }
  maintainConnection();
  handleTouch();
  if(mqtt.connected() && millis()-lastPoll>60000) requestAll();
  if(millis()-lastDraw>10000) render();
  delay(8);
}
