// ESP32-C3 Super Mini common platform, ported from ESP8266 common 1.03.
// Upstream SHA256: 3ae8ce9fb4fa1642373b8905face9251b5a06007fb278480dccc33bf43a1a9ed
// Application generation inserts only the selected application's declarations,
// implementation and profile at the marked integration points below.
// Baseline: EnergyMeter 5.58 behavior, cleaned source commit 0c874d3 (5.59).
// Boiler v4.05: RTC runtime breadcrumbs / OTA, SSD1309 1.3-inch; CommonPlatform v1.07.
// OLED profile for USB testing: SSD1309 1.3-inch I2C 128x64 (GPIO3 SDA / GPIO4 SCL).
// Keep OLED_TYPE 3 support in Common Platform for CH1116 1.54-inch installations.
// OLED hardware map for these installations (128x64 I2C):
//   OLED_TYPE 3 = CH1116 1.54-inch; U8g2 SSD1309 init (tested on the user's panel).
//   OLED_TYPE 2 = SSD1309 1.3-inch; OLED_TYPE 1 = legacy SH1106; 0 = none.
// Existing releases keep their original OLED_TYPE, version and OTA identity.
// BEGIN PLATFORM PROFILE
#define PLATFORM_CUSTOM_APPLICATION 0
#define PLATFORM_NAME "ESP32-C3-supermiini-CommonPlatoform"
#define PLATFORM_VERSION "1.07"
#define VERSION "4.05"
#define BUILD_DATE "11/11/2026"
#define DEVICE_ID "boiler"
#define DEVICE_KIND "BOILER"
#define DEVICE_LABEL "Boiler"
#define APPLICATION_ID "boiler"
#define DRIVER_ID "4xDS18B20"
#define OTA_PROFILE_PREFIX "esp32c3-boiler-4ds18b20"
#define MQTT_TOPIC_PREFIX "home/energy/"
#ifndef OLED_TYPE
#define OLED_TYPE 2  // SSD1309 1.3-inch 128x64 I2C, GPIO3 SDA / GPIO4 SCL.
#endif
#define PLATFORM_OTA_FILENAME_PREFIX "Boiler_ESP32C3_V"
#define PLATFORM_OTA_FILENAME_SUFFIX ".bin"
#define APP_SAMPLE_INTERVAL_MS 1000UL
// END PLATFORM PROFILE

// Actuator applications opt into shared lifecycle hooks.
#ifndef PLATFORM_LOCAL_CONTROL_APPLICATION
#define PLATFORM_LOCAL_CONTROL_APPLICATION 0
#endif
#ifndef PLATFORM_REQUIRE_ROUTER_BEFORE_APPLICATION
#define PLATFORM_REQUIRE_ROUTER_BEFORE_APPLICATION 0
#endif
#ifndef PLATFORM_FLASH_BUTTON_ENABLED
#define PLATFORM_FLASH_BUTTON_ENABLED 1
#endif
#ifndef PLATFORM_SENSOR_ERROR
#define PLATFORM_SENSOR_ERROR "boiler_temperature_unavailable"
#endif
// Applications may require more room for their own content buffers.
#ifndef PLATFORM_WEB_MIN_HEAP_BYTES
#define PLATFORM_WEB_MIN_HEAP_BYTES 1024UL
#endif
#ifndef PLATFORM_WEB_MIN_BLOCK_BYTES
#define PLATFORM_WEB_MIN_BLOCK_BYTES 256U
#endif

// Build identity describes compiled capabilities; it does not probe the display.
#if OLED_TYPE == 1
#define OLED_ID "SH1106"
#define OTA_PROFILE_ID OTA_PROFILE_PREFIX "-sh1106"
#elif OLED_TYPE == 2
#define OLED_ID "SSD1309"
#define OTA_PROFILE_ID OTA_PROFILE_PREFIX "-ssd1309"
#elif OLED_TYPE == 3
#define OLED_ID "CH1116"
#define OTA_PROFILE_ID OTA_PROFILE_PREFIX "-ch1116"
#elif OLED_TYPE == 0
#define OLED_ID "NONE"
#define OTA_PROFILE_ID OTA_PROFILE_PREFIX "-none"
#else
#error "Unsupported OTA display profile"
#endif
#define OTA_PROTOCOL_VERSION 2

#ifndef USE_STATIC_IP
#define USE_STATIC_IP 1 // The generic platform uses 192.168.1.79; applications may select their reserved address.
#endif
#define SITE_IP_A 192
#define SITE_IP_B 168
#define SITE_IP_C 1
#ifndef SITE_IP_D
#define SITE_IP_D 79
#endif
#define SITE_GW_A 192
#define SITE_GW_B 168
#define SITE_GW_C 1
#define SITE_GW_D 1
#define SITE_MASK_A 255
#define SITE_MASK_B 255
#define SITE_MASK_C 255
#define SITE_MASK_D 0
#define SITE_DNS_A 192
#define SITE_DNS_B 168
#define SITE_DNS_C 1
#define SITE_DNS_D 1

#define PLATFORM_STRINGIFY_INNER(value) #value
#define PLATFORM_STRINGIFY(value) PLATFORM_STRINGIFY_INNER(value)
#define PLATFORM_SETUP_IP_TEXT "192.168.2.XX"
#define PLATFORM_SETUP_BASE_URL "http://" PLATFORM_SETUP_IP_TEXT "/"

#include <Arduino.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <time.h>
#include <math.h>
#include <LittleFS.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Update.h>
#include <PubSubClient.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <ArduinoOTA.h>
#include <Wire.h>
#include <stdarg.h>
#include <pgmspace.h>
#include <stddef.h>
#include <algorithm>
#include <esp_system.h>
#include <esp_mac.h>
#include <esp_wifi.h>
#include <esp_partition.h>
#include <esp_ota_ops.h>
#include <esp_heap_caps.h>
#include <esp_arduino_version.h>
#include <mbedtls/ssl.h>
#if !defined(CONFIG_IDF_TARGET_ESP32C3)
#error "This firmware is only for ESP32-C3 Super Mini"
#endif

struct RtcOledDiagnostic;
struct DeviceCredentialRecord;
struct RtcCrashTrace;
struct RecoveryRecord;
struct OledDiagnostic;
enum class DisplayFont : uint8_t;
enum MqttAdminCommand : uint8_t;

// BEGIN APPLICATION FORWARD DECLARATIONS
struct BhDaily;
struct BhArchive;
// END APPLICATION FORWARD DECLARATIONS

// Native ESP32-C3 SDK adapters. Heap and largest-block sizes are 32-bit;
// ESP-IDF's FreeRTOS stack high-water mark is already expressed in bytes.
#include <esp_attr.h>
#include <esp_timer.h>
#include <driver/gpio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <lwip/sockets.h>
#include <lwip/dns.h>
#include <lwip/tcpip.h>
#include <errno.h>

uint32_t platformMaxFreeBlock() {
    return (uint32_t)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
}
void platformGetHeapStats(uint32_t* freeHeap, uint32_t* largestBlock, uint8_t* fragmentation) {
    multi_heap_info_t info = {};
    heap_caps_get_info(&info, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (freeHeap) *freeHeap = (uint32_t)info.total_free_bytes;
    if (largestBlock) *largestBlock = (uint32_t)info.largest_free_block;
    if (fragmentation) *fragmentation = info.total_free_bytes
        ? (uint8_t)(100ULL - (100ULL * info.largest_free_block / info.total_free_bytes)) : 0;
}
uint8_t platformHeapFragmentation() {
    uint8_t fragmentation = 0;
    platformGetHeapStats(nullptr, nullptr, &fragmentation);
    return fragmentation;
}
uint32_t platformFreeStack() {
    return (uint32_t)uxTaskGetStackHighWaterMark(nullptr);
}
uint32_t platformChipId() {
    uint8_t mac[6] = {};
    if (esp_read_mac(mac, ESP_MAC_WIFI_STA) != ESP_OK) return 0;
    return ((uint32_t)mac[2] << 24) | ((uint32_t)mac[3] << 16) |
           ((uint32_t)mac[4] << 8) | (uint32_t)mac[5];
}
void platformReadRetained(void* destination, const volatile void* source, size_t size) {
    uint8_t* out = reinterpret_cast<uint8_t*>(destination);
    const volatile uint8_t* in = reinterpret_cast<const volatile uint8_t*>(source);
    for (size_t i = 0; i < size; ++i) out[i] = in[i];
}
void platformWriteRetained(volatile void* destination, const void* source, size_t size) {
    volatile uint8_t* out = reinterpret_cast<volatile uint8_t*>(destination);
    const uint8_t* in = reinterpret_cast<const uint8_t*>(source);
    for (size_t i = 0; i < size; ++i) out[i] = in[i];
}
bool platformRtcContentsUsable() {
    const esp_reset_reason_t reason = esp_reset_reason();
    return reason != ESP_RST_UNKNOWN && reason != ESP_RST_POWERON &&
           reason != ESP_RST_BROWNOUT && reason != ESP_RST_PWR_GLITCH;
}

// A single persistent request avoids a callback referencing a timed-out stack
// frame. A timed-out request remains allocated until lwIP completes it. New
// callers never reuse its hostname/result storage while a callback is pending.
enum PlatformDnsState : uint8_t { PLATFORM_DNS_IDLE, PLATFORM_DNS_QUEUED,
    PLATFORM_DNS_ACTIVE, PLATFORM_DNS_DONE };
struct PlatformDnsRequest {
    char hostname[128];
    uint32_t address;
    PlatformDnsState state;
};
static PlatformDnsRequest platformDnsRequest = {};
static portMUX_TYPE platformDnsMux = portMUX_INITIALIZER_UNLOCKED;
void platformDnsComplete(const char*, const ip_addr_t* address, void* context) {
    PlatformDnsRequest* request = static_cast<PlatformDnsRequest*>(context);
    uint32_t ipv4 = 0;
    if (address && IP_IS_V4(address)) ipv4 = ip4_addr_get_u32(ip_2_ip4(address));
    portENTER_CRITICAL(&platformDnsMux);
    request->address = ipv4;
    request->state = PLATFORM_DNS_DONE;
    portEXIT_CRITICAL(&platformDnsMux);
}
void platformDnsStart(void* context) {
    PlatformDnsRequest* request = static_cast<PlatformDnsRequest*>(context);
    portENTER_CRITICAL(&platformDnsMux);
    request->state = PLATFORM_DNS_ACTIVE;
    portEXIT_CRITICAL(&platformDnsMux);
    ip_addr_t address = {};
    const err_t result = dns_gethostbyname_addrtype(request->hostname, &address,
        platformDnsComplete, request, LWIP_DNS_ADDRTYPE_IPV4);
    if (result == ERR_OK) platformDnsComplete(request->hostname, &address, request);
    else if (result != ERR_INPROGRESS) platformDnsComplete(request->hostname, nullptr, request);
}
int platformResolveHost(const char* hostname, IPAddress& result, uint32_t timeoutMs) {
    result = IPAddress((uint32_t)0);
    if (!hostname || !hostname[0] || strlen(hostname) >= sizeof(platformDnsRequest.hostname)) return 0;
    ip_addr_t literal = {};
    if (ipaddr_aton(hostname, &literal)) {
        if (!IP_IS_V4(&literal)) return 0;
        result = IPAddress(ip4_addr_get_u32(ip_2_ip4(&literal)));
        return 1;
    }
    bool start = false;
    portENTER_CRITICAL(&platformDnsMux);
    if (platformDnsRequest.state == PLATFORM_DNS_DONE &&
        strcmp(platformDnsRequest.hostname, hostname) != 0) platformDnsRequest.state = PLATFORM_DNS_IDLE;
    if (platformDnsRequest.state == PLATFORM_DNS_IDLE) {
        memcpy(platformDnsRequest.hostname, hostname, strlen(hostname) + 1);
        platformDnsRequest.address = 0;
        platformDnsRequest.state = PLATFORM_DNS_QUEUED;
        start = true;
    }
    const bool matches = strcmp(platformDnsRequest.hostname, hostname) == 0;
    portEXIT_CRITICAL(&platformDnsMux);
    if (!matches) return 0;
    if (start && tcpip_try_callback(platformDnsStart, &platformDnsRequest) != ERR_OK) {
        portENTER_CRITICAL(&platformDnsMux);
        platformDnsRequest.state = PLATFORM_DNS_IDLE;
        portEXIT_CRITICAL(&platformDnsMux);
        return 0;
    }
    const uint32_t started = millis();
    do {
        uint32_t address = 0;
        bool done = false;
        portENTER_CRITICAL(&platformDnsMux);
        if (platformDnsRequest.state == PLATFORM_DNS_DONE) {
            address = platformDnsRequest.address;
            platformDnsRequest.state = PLATFORM_DNS_IDLE;
            done = true;
        }
        portEXIT_CRITICAL(&platformDnsMux);
        if (done) {
            result = IPAddress(address);
            return address ? 1 : 0;
        }
        if ((uint32_t)(millis() - started) >= timeoutMs) break;
        delay(1);
    } while (true);
    return 0;
}

// BEGIN APPLICATION DECLARATIONS
// END APPLICATION DECLARATIONS

// Display backend for the generated, single-translation-unit firmware sketch.
// OLED_TYPE: 1=legacy SH1106, 2=SSD1309 1.3-inch, 3=CH1116 1.54-inch (SSD1309 init), 0=headless.
// No U8g2 code or framebuffer is included in a headless build.
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>

#ifndef OLED_TYPE
  #error "Set OLED_TYPE to 1 (SH1106), 2 (SSD1309), 3 (CH1116 trial) or 0 (headless)"
#endif

enum class DisplayFont : uint8_t {
    Tiny4x6,
    Small5x7,
    Medium6x12,
    Normal10
};

#if OLED_TYPE == 1 || OLED_TYPE == 2 || OLED_TYPE == 3
  #include <U8g2lib.h>

class PlatformDisplay {
public:
    PlatformDisplay() : oled_(U8G2_R0, U8X8_PIN_NONE) {}

    // This describes the selected build capability, not an I2C hardware probe.
    bool available() const { return initialized_; }
    void begin() {
        if (initialized_) return;
        Wire.begin(3, 4); Wire.setClock(100000); oled_.setI2CAddress(0x3C << 1); oled_.begin();
        initialized_ = true;
    }
    void clear() { if (initialized_) oled_.clearBuffer(); }
    void present() { if (initialized_) oled_.sendBuffer(); }
    void horizontalLine(int16_t x, int16_t y, int16_t width) { if (initialized_) oled_.drawHLine(x, y, width); }
    void contrast(uint8_t value) { if (initialized_) oled_.setContrast(value); }

    void font(DisplayFont value) {
        if (!initialized_) return;
        switch (value) {
            case DisplayFont::Tiny4x6: oled_.setFont(u8g2_font_4x6_tf); break;
            case DisplayFont::Small5x7: oled_.setFont(u8g2_font_5x7_tf); break;
            case DisplayFont::Medium6x12: oled_.setFont(u8g2_font_6x12_tf); break;
            case DisplayFont::Normal10: oled_.setFont(u8g2_font_helvR10_tr); break;
        }
    }

    void text(int16_t x, int16_t y, const char* value) {
        if (initialized_ && value) oled_.drawStr(x, y, value);
    }

private:
    bool initialized_ = false;
#if OLED_TYPE == 1
    U8G2_SH1106_128X64_NONAME_F_HW_I2C oled_;
#else
    // CH1116 candidate uses the SSD1309 initialization sequence; it is
    // distinct from the SH1106-based Boiler v4.01 build.
    U8G2_SSD1309_128X64_NONAME0_F_HW_I2C oled_;
#endif
};

#elif OLED_TYPE == 0

class PlatformDisplay {
public:
    bool available() const { return false; }
    void begin() {}
    void clear() {}
    void present() {}
    void horizontalLine(int16_t, int16_t, int16_t) {}
    void contrast(uint8_t) {}
    void font(DisplayFont) {}
    void text(int16_t, int16_t, const char*) {}
};

#else
  #error "Invalid OLED_TYPE: use 1 (SH1106), 2 (SSD1309), 3 (CH1116 trial) or 0 (headless)"
#endif

static PlatformDisplay platformDisplay;

void displayClearAndPresent() {
    platformDisplay.clear();
    platformDisplay.present();
}

// Compatibility with the EnergyMeter line layout: draw into the existing
// framebuffer, use the original 420 font selector, and present immediately.
// A headless build avoids formatting the text as well as all display I/O.
void displayPrintf(int fontCode, int line, int pos, const char* format, ...) {
    if (!platformDisplay.available() || fontCode != 420 || !format) return;
    char buffer[128];
    va_list arguments;
    va_start(arguments, format);
    vsnprintf(buffer, sizeof(buffer), format, arguments);
    va_end(arguments);
    platformDisplay.font(DisplayFont::Normal10);
    platformDisplay.text((pos - 1) * 8, line * 15, buffer);
    platformDisplay.present();
}



void applicationInitialize();
void applicationInitializeTopics();
bool applicationReadMeasurements();
void applicationRenderMeasurements(bool ok);
bool applicationStageCommand(const String& message);
bool applicationProcessCommand(MqttAdminCommand command);
void applicationResetCommand();
void applicationBackground(unsigned long now);
bool applicationAppendDebug(WiFiClient& client);
void applicationFormatSummary(char* out, size_t capacity);
bool applicationDualZoneEnabled();
#if PLATFORM_LOCAL_CONTROL_APPLICATION
void applicationEarlyInitialize();
void applicationServiceLocal(unsigned long now);
void applicationPrepareOta();
void applicationConfigureWebRoutes();
bool applicationImportLegacyCredentials();
bool applicationClearLegacyCredentials();
#endif

// Wi-Fi / MQTT runtime credentials.
// Broker host/port are public build settings. Wi-Fi/MQTT login secrets stay in LittleFS.
// Existing V1 credential records remain compatible after OTA.
char ssid[33] = {0};
char password[65] = {0};
uint8_t configuredStaOctet = SITE_IP_D;
#define MQTT_BROKER_HOST "b7e93fa24c0c4c86a995afecd61e93f3.s1.eu.hivemq.cloud"
const uint16_t MQTT_BROKER_PORT = 8883;
char mqttHost[128] = MQTT_BROKER_HOST;
uint16_t mqttPort = MQTT_BROKER_PORT;
char mqttUser[65] = {0};
char mqttPassword[65] = {0};

const char* DEVICE_CREDENTIAL_FILE = "/device_credentials.bin";
const char* DEVICE_CREDENTIAL_TEMP_FILE = "/device_credentials.tmp";
const uint32_t DEVICE_CREDENTIAL_MAGIC = 0x43524432UL; // "CRD2"
const uint16_t DEVICE_CREDENTIAL_VERSION = 2;

struct DeviceCredentialRecord {
    uint32_t magic;
    uint16_t version;
    uint16_t mqttPort;
    char ssid[33];
    char wifiPassword[65];
    char mqttHost[128];
    char mqttUser[65];
    char mqttPassword[65];
    uint8_t staOctet = SITE_IP_D;
    uint8_t reserved[3] = {0};
    uint32_t checksum;
};

struct LegacyDeviceCredentialRecord {
    uint32_t magic;
    uint16_t version;
    uint16_t mqttPort;
    char ssid[33];
    char wifiPassword[65];
    char mqttHost[128];
    char mqttUser[65];
    char mqttPassword[65];
    uint32_t checksum;
};

bool parseStaOctet(const char* value, uint8_t* result) {
    if (!value || !result || !*value || strlen(value) > 3) return false;
    unsigned number = 0;
    for (const char* p = value; *p; ++p) {
        if (*p < '0' || *p > '9') return false;
        number = number * 10U + (unsigned)(*p - '0');
    }
    // .0/.255 are network/broadcast and .1 is the configured router.
    if (number < 2 || number > 254) return false;
    *result = (uint8_t)number;
    return true;
}

uint32_t deviceCredentialChecksum(const DeviceCredentialRecord& rec) {
    const uint8_t* p = reinterpret_cast<const uint8_t*>(&rec);
    const size_t n = offsetof(DeviceCredentialRecord, checksum);
    uint32_t h = 2166136261UL;
    for (size_t i = 0; i < n; ++i) {
        h ^= p[i];
        h *= 16777619UL;
    }
    return h;
}

bool credentialFieldIsValid(const char* value, size_t capacity) {
    if (!value || capacity == 0) return false;
    size_t n = strnlen(value, capacity);
    return n > 0 && n < capacity;
}

bool copyCredentialField(char* dst, size_t dstSize, const char* src) {
    if (!dst || dstSize == 0 || !src) return false;
    size_t n = strlen(src);
    if (n == 0 || n >= dstSize) return false;
    memcpy(dst, src, n + 1);
    return true;
}

bool copyOptionalCredentialField(char* dst, size_t capacity, const char* src) {
    if (!dst || !src || capacity == 0) return false;
    const size_t n = strnlen(src, capacity);
    if (n >= capacity) return false;
    memcpy(dst, src, n + 1);
    return true;
}

// Wi-Fi availability does not depend on MQTT credentials or broker acceptance.
bool validateDeviceCredentialRecord(const DeviceCredentialRecord& rec) {
    return
        rec.magic == DEVICE_CREDENTIAL_MAGIC &&
        rec.version == DEVICE_CREDENTIAL_VERSION &&
        rec.staOctet >= 2 && rec.staOctet <= 254 &&
        rec.checksum == deviceCredentialChecksum(rec) &&
        credentialFieldIsValid(rec.ssid, sizeof(rec.ssid)) &&
        credentialFieldIsValid(rec.wifiPassword, sizeof(rec.wifiPassword)) &&
        strnlen(rec.mqttHost, sizeof(rec.mqttHost)) < sizeof(rec.mqttHost) &&
        strnlen(rec.mqttUser, sizeof(rec.mqttUser)) < sizeof(rec.mqttUser) &&
        strnlen(rec.mqttPassword, sizeof(rec.mqttPassword)) < sizeof(rec.mqttPassword);
}

bool applyDeviceCredentials(const DeviceCredentialRecord& rec) {
    if (!validateDeviceCredentialRecord(rec)) return false;
    if (!copyCredentialField(ssid, sizeof(ssid), rec.ssid) ||
        !copyCredentialField(password, sizeof(password), rec.wifiPassword) ||
        !copyCredentialField(mqttHost, sizeof(mqttHost), MQTT_BROKER_HOST) ||
        !copyOptionalCredentialField(mqttUser, sizeof(mqttUser), rec.mqttUser) ||
        !copyOptionalCredentialField(mqttPassword, sizeof(mqttPassword), rec.mqttPassword)) {
        return false;
    }
    mqttPort = MQTT_BROKER_PORT;
    configuredStaOctet = rec.staOctet;
    return true;
}

bool readDeviceCredentialRecordAtPath(DeviceCredentialRecord& rec, const char* path) {
    if (!LittleFS.exists(path)) return false;
    File file = LittleFS.open(path, "r");
    if (!file) return false;
    rec = DeviceCredentialRecord();
    bool ok = false;
    if (file.size() == sizeof(rec)) {
        ok = file.readBytes(reinterpret_cast<char*>(&rec), sizeof(rec)) == sizeof(rec);
    } else if (file.size() == sizeof(LegacyDeviceCredentialRecord)) {
        LegacyDeviceCredentialRecord old = {};
        ok = file.readBytes(reinterpret_cast<char*>(&old), sizeof(old)) == sizeof(old);
        uint32_t checksum = 2166136261UL;
        const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&old);
        for (size_t i = 0; i < offsetof(LegacyDeviceCredentialRecord, checksum); ++i) {
            checksum ^= bytes[i]; checksum *= 16777619UL;
        }
        ok = ok && old.magic == DEVICE_CREDENTIAL_MAGIC && old.version == 1 &&
             checksum == old.checksum;
        if (ok) {
            static_assert(offsetof(DeviceCredentialRecord, staOctet) ==
                          offsetof(LegacyDeviceCredentialRecord, checksum), "Credential prefix changed");
            memcpy(reinterpret_cast<uint8_t*>(&rec), &old,
                   offsetof(LegacyDeviceCredentialRecord, checksum));
            rec.version = DEVICE_CREDENTIAL_VERSION;
            rec.staOctet = SITE_IP_D;
            rec.checksum = deviceCredentialChecksum(rec);
        }
    }
    file.close();
    return ok && validateDeviceCredentialRecord(rec);
}

bool readDeviceCredentialRecord(DeviceCredentialRecord& rec) {
    return readDeviceCredentialRecordAtPath(rec, DEVICE_CREDENTIAL_FILE);
}

bool writeDeviceCredentialRecord(const DeviceCredentialRecord& rec) {
    if (!validateDeviceCredentialRecord(rec)) return false;
    // Keep the existing record intact until the staged write has been closed
    // and read back. LittleFS rename replaces the destination atomically.
    File file = LittleFS.open(DEVICE_CREDENTIAL_TEMP_FILE, "w");
    if (!file) return false;
    bool ok = file.write(reinterpret_cast<const uint8_t*>(&rec), sizeof(rec)) == sizeof(rec);
    file.flush();
    file.close();
    DeviceCredentialRecord verified = {};
    ok = ok && readDeviceCredentialRecordAtPath(verified, DEVICE_CREDENTIAL_TEMP_FILE) &&
         memcmp(&verified, &rec, sizeof(rec)) == 0;
    if (!ok || !LittleFS.rename(DEVICE_CREDENTIAL_TEMP_FILE, DEVICE_CREDENTIAL_FILE)) {
        LittleFS.remove(DEVICE_CREDENTIAL_TEMP_FILE);
        return false;
    }
    return readDeviceCredentialRecord(verified) && memcmp(&verified, &rec, sizeof(rec)) == 0;
}

bool loadDeviceCredentials() {
    DeviceCredentialRecord stored = {};
    return readDeviceCredentialRecord(stored) && applyDeviceCredentials(stored);
}

// ===== MQTT REQUEST/RESPONSE TOPICS =====

// Το ESP θα ακούει στο REQUEST topic και θα απαντά στο STATE topic
// μόνο όταν ο χρήστης στείλει το μήνυμα: update
const size_t MQTT_DEVICE_ID_BYTES = sizeof(DEVICE_ID) + 13; // dash + 12 hex digits + NUL
const size_t MQTT_TOPIC_BASE_BYTES = sizeof(MQTT_TOPIC_PREFIX) + MQTT_DEVICE_ID_BYTES - 1;
char mqttDeviceId[MQTT_DEVICE_ID_BYTES] = {};
char MQTT_STATE_TOPIC[MQTT_TOPIC_BASE_BYTES + sizeof("/state") - 1] = {};
char MQTT_STATUS_TOPIC[MQTT_TOPIC_BASE_BYTES + sizeof("/status") - 1] = {};
char MQTT_REQUEST_TOPIC[MQTT_TOPIC_BASE_BYTES + sizeof("/request") - 1] = {};

// ===== MQTT ADMIN TOPICS: ΔΕΗ / HISTORY / OTA =====
char MQTT_ADMIN_REQUEST_TOPIC[MQTT_TOPIC_BASE_BYTES + sizeof("/admin/request") - 1] = {};
char MQTT_ADMIN_RESPONSE_TOPIC[MQTT_TOPIC_BASE_BYTES + sizeof("/admin/response") - 1] = {};
char MQTT_ADMIN_CSV_TOPIC[MQTT_TOPIC_BASE_BYTES + sizeof("/admin/csv") - 1] = {};

WiFiClientSecure mqttNet;
PubSubClient mqttClient(mqttNet);

bool mqttUpdateRequested = false;
unsigned long lastMqttReconnectMs = 0;

// v5.57 long-run network stability.
// The APP asks for an update every minute. Keep the hot request path
// allocation-free and back off failed publishes instead of retrying every
// 1.5 seconds forever under memory/network pressure.
const unsigned long MQTT_UPDATE_RETRY_STEPS_MS[] = {
    5000UL, 10000UL, 30000UL
};
const uint8_t MQTT_UPDATE_RETRY_STEP_COUNT =
    sizeof(MQTT_UPDATE_RETRY_STEPS_MS) / sizeof(MQTT_UPDATE_RETRY_STEPS_MS[0]);
uint8_t mqttUpdateRetryStep = 0;
unsigned long mqttUpdateRetryDelayMs = 1500UL;

// If a previously healthy unit stays on Wi-Fi but repeatedly gets MQTT
// transport state -2, recycle only the network stack. This releases stale
// lwIP/TLS resources and restarts the local HTTP listener without rebooting
// metering/accounting state.
const uint8_t MQTT_RUNTIME_RECOVERY_FAILURES_LOW_HEAP = 4;
const uint8_t MQTT_RUNTIME_RECOVERY_FAILURES_GENERIC = 12;
uint8_t mqttRuntimeConnectFailureStreak = 0;
bool networkStackRecoveryPending = false;
uint32_t networkStackRecoveryCount = 0;
// Boot-time broker failures back off gradually. Once this ESP has had a
// working MQTT session, a later router/Internet interruption uses a much
// faster recovery cadence so MQTT comes back without a device restart.
const unsigned long MQTT_RETRY_STEPS_MS[] = {
    10000UL, 30000UL, 60000UL, 120000UL, 300000UL
};
const unsigned long MQTT_RUNTIME_RETRY_STEPS_MS[] = {
    5000UL, 10000UL, 30000UL
};
const uint8_t MQTT_RETRY_STEP_COUNT =
    sizeof(MQTT_RETRY_STEPS_MS) / sizeof(MQTT_RETRY_STEPS_MS[0]);
const uint8_t MQTT_RUNTIME_RETRY_STEP_COUNT =
    sizeof(MQTT_RUNTIME_RETRY_STEPS_MS) / sizeof(MQTT_RUNTIME_RETRY_STEPS_MS[0]);
uint8_t mqttRetryStep = 0;
unsigned long mqttRetryDelayMs = MQTT_RETRY_STEPS_MS[0];
uint32_t mqttConnectFailures = 0;
bool mqttEverConnected = false;

// Passive diagnostics of the actual MQTT/mbedTLS connection.
uint32_t mqttConnectAttempts = 0;
unsigned long mqttLastAttemptMs = 0;
int mqttLastAttemptState = -1;
bool mqttLastConnectSucceeded = false;
bool mqttLastStatusPublishSucceeded = false;
bool mqttLastRequestSubscribeSucceeded = false;
bool mqttLastAdminSubscribeSucceeded = false;
int mqttLastSslErrorCode = 0;
char mqttLastSslErrorText[160] = {0};
// Capture the resource gate before TLS allocates buffers; current web-request
// heap alone cannot explain a deferred connection attempt.
uint32_t mqttConnectHeapAtAttempt = 0;
uint32_t mqttConnectBlockAtAttempt = 0;
uint32_t mqttConnectPreflightDeferrals = 0;
// Native mbedTLS capacities from the pinned ESP-IDF configuration.
const uint16_t MQTT_TLS_RX_BYTES = MBEDTLS_SSL_IN_CONTENT_LEN;
const uint16_t MQTT_TLS_TX_BYTES = MBEDTLS_SSL_OUT_CONTENT_LEN;
const uint32_t MQTT_TLS_MIN_HEAP_BYTES = 60000UL;
const uint32_t MQTT_TLS_MIN_BLOCK_BYTES = 32768UL;
const uint16_t MQTT_PACKET_BUFFER_BYTES = 1024; // enough for ~844-byte JSY state + topic/header
const uint16_t MQTT_STATE_PAYLOAD_BYTES = 864;  // bounded stack buffer; includes utility reference date
// The publish heap check uses locals inside publishMqttSample(); no diagnostic
// globals are kept for values that are never displayed or consumed later.
const unsigned long MQTT_UPDATE_MIN_INTERVAL_MS = 1500UL;
unsigned long lastMqttUpdatePublishMs = 0;

// Stream flash-resident HTML through a fixed RAM slice and bounded,
// nonblocking native sockets; do not assemble complete pages in heap.
const size_t WEB_ROOT_RAM_SLICE_BYTES = 64;
// v4.04 drains every 64-byte COPY write before queuing the next slice.
// These floors allow the page under TLS load, while refusing normal writes
// above the observed crash point (464 B free / 184 B max block). A smaller
// emergency response reports memory pressure instead of a blank HTTP 204.
const uint32_t WEB_ROOT_MIN_HEAP_BYTES = 1024UL;
const uint16_t WEB_ROOT_MIN_BLOCK_BYTES = 256U;
// Browsers can cancel an in-flight page by pressing Refresh again. Explicit
// socket cleanup + a very short reload guard prevents abandoned HTTP sockets
// from exhausting the ESP32-C3 lwIP PCB/pbuf pool while MQTT keeps running.
const unsigned long WEB_ROOT_MIN_REQUEST_GAP_MS = 350UL;
unsigned long webRootLastRequestMs = 0;
uint32_t webRootRequestCount = 0;
uint32_t webRootRateLimitedCount = 0;
uint32_t webRootWriteAbortCount = 0;
uint32_t webRootForcedCloseCount = 0;
uint32_t webRootBusyCount = 0;
const unsigned long WEB_RESPONSE_BUDGET_MS = 4000UL;
unsigned long webResponseStartedMs = 0;
bool webResponseActive = false;
uint16_t mqttTlsRecvConfiguredBytes = MQTT_TLS_RX_BYTES;
const unsigned long MQTT_DNS_TIMEOUT_MS = 1500UL;
const unsigned long MQTT_TLS_TIMEOUT_MS = 4000UL;

// v2.06: crash breadcrumb in RTC user memory. It survives exception/watchdog
// resets without flash wear and records the last high-risk operation entered.
// Offset is in 4-byte blocks; 64 is outside the first 32 blocks used by OTA.
const uint32_t RTC_CRASH_TRACE_MAGIC = 0x43543242UL; // "CT2B" v2.11 layout
enum CrashStage : uint32_t {
    CRASH_STAGE_NONE = 0,
    CRASH_STAGE_SETUP = 10,
    CRASH_STAGE_BOOT_OLED_INIT = 11,
    CRASH_STAGE_BOOT_FILESYSTEM = 12,
    CRASH_STAGE_BOOT_CREDENTIALS = 13,
    CRASH_STAGE_BOOT_RECOVERY_CHECK = 14,
    CRASH_STAGE_BOOT_RECOVERY_CLEAR = 15,
    CRASH_STAGE_BOOT_EEPROM = 20,
    CRASH_STAGE_BOOT_CLEAN_START = 21,
    CRASH_STAGE_BOOT_TARIFF = 22,
    CRASH_STAGE_BOOT_STATE = 23,
    CRASH_STAGE_BOOT_UTILITY = 24,
    CRASH_STAGE_BOOT_ESTIMATE = 25,
    CRASH_STAGE_BOOT_SERIAL = 30,
    CRASH_STAGE_BOOT_WEB_CONFIG = 31,
    CRASH_STAGE_BOOT_MQTT_CONFIG = 32,
    CRASH_STAGE_WIFI_CONNECT = 40,
    CRASH_STAGE_BOOT_AP_START = 41,
    CRASH_STAGE_BOOT_AP_IDLE = 42,
    CRASH_STAGE_BOOT_AP_FAILED = 43,
    CRASH_STAGE_NETWORK_SERVICES = 50,
    CRASH_STAGE_BOOT_LOOP_ENTER = 51,
    CRASH_STAGE_BOOT_FIRST_WEB = 52,
    CRASH_STAGE_BOOT_MQTT_DNS = 59,
    CRASH_STAGE_MQTT_CONNECT = 61,
    CRASH_STAGE_BOOT_FIRST_METER = 65,
    CRASH_STAGE_BOOT_ACCOUNTING = 66,
    CRASH_STAGE_BOOT_FIRST_DISPLAY = 67,
    CRASH_STAGE_BOOT_RUNNING = 68,
    CRASH_STAGE_BOOT_RECOVERY_IDLE = 69,
    CRASH_STAGE_MQTT_JSON_BUILD = 70,
    CRASH_STAGE_MQTT_PUBLISH = 72,
    CRASH_STAGE_WEB_ROOT = 80,
    CRASH_STAGE_WEB_ROOT_HEADER = 85,
    CRASH_STAGE_WEB_ROOT_RAMCOPY = 86,
    CRASH_STAGE_WEB_ROOT_WRITE = 87,
    CRASH_STAGE_WEB_SETUP = 88,
    CRASH_STAGE_WEB_NET_DEBUG = 89,
    CRASH_STAGE_REMOTE_OTA_PREP = 90,
    CRASH_STAGE_REMOTE_OTA_TLS = 91,
    CRASH_STAGE_REMOTE_OTA_HTTP = 92,
    CRASH_STAGE_REMOTE_OTA_FLASH = 93,
    CRASH_STAGE_BOOT_OLED_WRITE = 100,
    CRASH_STAGE_APP_SENSOR_INIT = 110,
    CRASH_STAGE_APP_HISTORY_INIT = 111
};
bool bootCheckpointsActive = true;
bool bootOledReady = false;
bool bootFirstMeterDone = false;
bool bootFirstMqttAttempted = false;
uint32_t bootLastCheckpoint = CRASH_STAGE_NONE;
void bootCheckpoint(uint32_t stage);
void finishBootCheckpoints(uint32_t stage);
struct RtcCrashTrace {
    uint32_t magic;
    uint32_t stage;
    uint32_t detail;
    uint32_t heapFree;
    uint32_t maxFreeBlock;
    uint32_t heapFragmentation;
    uint32_t checksum;
};
RTC_NOINIT_ATTR static volatile RtcCrashTrace rtcCrashTraceStorage;
RtcCrashTrace rtcTraceBeforeReset = {};
bool rtcTraceBeforeResetValid = false;

uint32_t rtcCrashTraceChecksum(uint32_t stage, uint32_t detail,
                               uint32_t heapFree, uint32_t maxFreeBlock,
                               uint32_t heapFragmentation) {
    return RTC_CRASH_TRACE_MAGIC ^ stage ^ detail ^ heapFree ^
           maxFreeBlock ^ heapFragmentation ^ 0xA526C31DUL;
}

void loadRtcCrashTrace() {
    rtcTraceBeforeResetValid = false;
    if (!platformRtcContentsUsable()) {
        const RtcCrashTrace empty = {};
        platformWriteRetained(&rtcCrashTraceStorage, &empty, sizeof(empty));
        return;
    }
    RtcCrashTrace t = {};
    platformReadRetained(&t, &rtcCrashTraceStorage, sizeof(t));
    if (t.magic == RTC_CRASH_TRACE_MAGIC &&
        t.checksum == rtcCrashTraceChecksum(t.stage, t.detail, t.heapFree,
                                            t.maxFreeBlock, t.heapFragmentation)) {
        rtcTraceBeforeReset = t;
        rtcTraceBeforeResetValid = true;
    }
}

void markCrashStage(uint32_t stage, uint32_t detail) {
    RtcCrashTrace t = {};
    t.stage = stage; t.detail = detail;
    uint8_t fragmentation = 0;
    platformGetHeapStats(&t.heapFree, &t.maxFreeBlock, &fragmentation);
    t.heapFragmentation = fragmentation;
    t.checksum = rtcCrashTraceChecksum(t.stage, t.detail, t.heapFree,
                                       t.maxFreeBlock, t.heapFragmentation);
    rtcCrashTraceStorage.magic = 0;
    platformWriteRetained(&rtcCrashTraceStorage, &t, sizeof(t));
    rtcCrashTraceStorage.magic = RTC_CRASH_TRACE_MAGIC;
}

void clearRtcCrashTrace() {
    const RtcCrashTrace empty = {};
    platformWriteRetained(&rtcCrashTraceStorage, &empty, sizeof(empty));
    rtcTraceBeforeReset = RtcCrashTrace();
    rtcTraceBeforeResetValid = false;
}


// Common Platform v1.07: bounded RTC-only runtime breadcrumb ring.
// Each slot commits its magic LAST, making a watchdog-interrupted write
// detectable by checksum. No flash writes, heap use, locks or ISR callbacks.
enum RuntimeEvent : uint16_t {
    RB_LOOP_BEGIN=1, RB_LOOP_IDLE=2,
    RB_SENSOR_BEGIN=10, RB_SENSOR_END=11,
    RB_OLED_BEGIN=20, RB_OLED_END=21,
    RB_WIFI_BEGIN=30, RB_WIFI_END=31,
    RB_WEB_BEGIN=40, RB_WEB_END=41,
    RB_MQTT_BEGIN=50, RB_MQTT_END=51,
    RB_MQTT_DNS_BEGIN=52, RB_MQTT_DNS_END=53,
    RB_MQTT_TLS_BEGIN=54, RB_MQTT_TLS_END=55,
    RB_LITTLEFS_BEGIN=60, RB_LITTLEFS_END=61,
    RB_BACKGROUND_BEGIN=70, RB_BACKGROUND_END=71,
    RB_ADMIN_BEGIN=80, RB_ADMIN_END=81,
    RB_WIFI_STACK_BEGIN=90, RB_WIFI_STACK_END=91
};
static const uint8_t RUNTIME_BREADCRUMB_CAPACITY=12;
static const uint32_t RUNTIME_BREADCRUMB_MAGIC=0x52544231UL;
struct RuntimeBreadcrumbEntry {
    uint32_t magic, sequence, uptimeMs;
    uint16_t code, detail;
    uint32_t checksum;
};
RTC_NOINIT_ATTR static volatile RuntimeBreadcrumbEntry rtcRuntimeBreadcrumbs[RUNTIME_BREADCRUMB_CAPACITY];
RuntimeBreadcrumbEntry runtimeBeforeReset[RUNTIME_BREADCRUMB_CAPACITY] = {};
uint8_t runtimeBeforeResetCount=0;
uint32_t runtimeBreadcrumbSequence=0;

uint32_t runtimeBreadcrumbChecksum(const RuntimeBreadcrumbEntry& b) {
    return RUNTIME_BREADCRUMB_MAGIC ^ b.sequence ^ b.uptimeMs ^
           (((uint32_t)b.code << 16) | (uint32_t)b.detail) ^ 0x39AC0B5DUL;
}
const char* runtimeBreadcrumbLabel(uint16_t code) {
    switch(code) {
        case RB_LOOP_BEGIN:return "LOOP_BEGIN"; case RB_LOOP_IDLE:return "LOOP_IDLE";
        case RB_SENSOR_BEGIN:return "SENSOR_BEGIN"; case RB_SENSOR_END:return "SENSOR_END";
        case RB_OLED_BEGIN:return "OLED_BEGIN"; case RB_OLED_END:return "OLED_END";
        case RB_WIFI_BEGIN:return "WIFI_BEGIN"; case RB_WIFI_END:return "WIFI_END";
        case RB_WEB_BEGIN:return "WEB_BEGIN"; case RB_WEB_END:return "WEB_END";
        case RB_MQTT_BEGIN:return "MQTT_BEGIN"; case RB_MQTT_END:return "MQTT_END";
        case RB_MQTT_DNS_BEGIN:return "MQTT_DNS_BEGIN"; case RB_MQTT_DNS_END:return "MQTT_DNS_END";
        case RB_MQTT_TLS_BEGIN:return "MQTT_TLS_BEGIN"; case RB_MQTT_TLS_END:return "MQTT_TLS_END";
        case RB_LITTLEFS_BEGIN:return "LITTLEFS_BEGIN"; case RB_LITTLEFS_END:return "LITTLEFS_END";
        case RB_BACKGROUND_BEGIN:return "BACKGROUND_BEGIN"; case RB_BACKGROUND_END:return "BACKGROUND_END";
        case RB_ADMIN_BEGIN:return "ADMIN_BEGIN"; case RB_ADMIN_END:return "ADMIN_END";
        case RB_WIFI_STACK_BEGIN:return "WIFI_STACK_BEGIN"; case RB_WIFI_STACK_END:return "WIFI_STACK_END";
        default:return "UNKNOWN";
    }
}
void loadRtcRuntimeBreadcrumbs() {
    runtimeBeforeResetCount=0;
    // A power-on/brownout does not guarantee retained RTC bytes.
    if (platformRtcContentsUsable()) {
        for(uint8_t i=0;i<RUNTIME_BREADCRUMB_CAPACITY;++i) {
            RuntimeBreadcrumbEntry b={};
            platformReadRetained(&b,&rtcRuntimeBreadcrumbs[i],sizeof(b));
            if(b.magic!=RUNTIME_BREADCRUMB_MAGIC ||
               b.checksum!=runtimeBreadcrumbChecksum(b) || b.sequence==0) continue;
            uint8_t j=runtimeBeforeResetCount++;
            // Show oldest -> newest, without relying on RTC array positions.
            while(j>0 && (int32_t)(b.sequence-runtimeBeforeReset[j-1].sequence)<0) {
                runtimeBeforeReset[j]=runtimeBeforeReset[j-1];
                --j;
            }
            runtimeBeforeReset[j]=b;
        }
    }
    // The pre-reset snapshot is now in RAM. Start a clean log for this boot.
    for(uint8_t i=0;i<RUNTIME_BREADCRUMB_CAPACITY;++i) rtcRuntimeBreadcrumbs[i].magic=0;
    runtimeBreadcrumbSequence=0;
}
void runtimeBreadcrumb(uint16_t code,uint16_t detail=0) {
    const uint32_t seq=++runtimeBreadcrumbSequence;
    volatile RuntimeBreadcrumbEntry& slot=
        rtcRuntimeBreadcrumbs[(seq-1)%RUNTIME_BREADCRUMB_CAPACITY];
    const uint32_t tick=millis();
    RuntimeBreadcrumbEntry b={};
    b.sequence=seq; b.uptimeMs=tick; b.code=code; b.detail=detail;
    b.checksum=runtimeBreadcrumbChecksum(b);
    slot.magic=0;       // invalidate any previous record
    slot.sequence=b.sequence; slot.uptimeMs=b.uptimeMs;
    slot.code=b.code; slot.detail=b.detail; slot.checksum=b.checksum;
    slot.magic=RUNTIME_BREADCRUMB_MAGIC; // commit
}

const char* crashStageLabel(uint32_t stage) {
    switch (stage) {
        case CRASH_STAGE_SETUP: return "SETUP";
        case CRASH_STAGE_BOOT_OLED_INIT: return "OLED_INIT";
        case CRASH_STAGE_BOOT_FILESYSTEM: return "FILESYSTEM";
        case CRASH_STAGE_BOOT_CREDENTIALS: return "LOAD_CREDENTIALS";
        case CRASH_STAGE_BOOT_RECOVERY_CHECK: return "CHECK_RECOVERY";
        case CRASH_STAGE_BOOT_RECOVERY_CLEAR: return "CLEAR_RECOVERY";
        case CRASH_STAGE_BOOT_EEPROM: return "EEPROM_INIT";
        case CRASH_STAGE_BOOT_CLEAN_START: return "V3_INIT";
        case CRASH_STAGE_BOOT_TARIFF: return "LOAD_TARIFF";
        case CRASH_STAGE_BOOT_STATE: return "LOAD_STATE";
        case CRASH_STAGE_BOOT_UTILITY: return "LOAD_UTILITY";
        case CRASH_STAGE_BOOT_ESTIMATE: return "ESTIMATE_VIEW";
        case CRASH_STAGE_BOOT_SERIAL: return "METER_SERIAL";
        case CRASH_STAGE_BOOT_WEB_CONFIG: return "WEB_CONFIG";
        case CRASH_STAGE_BOOT_MQTT_CONFIG: return "MQTT_CONFIG";
        case CRASH_STAGE_WIFI_CONNECT: return "WIFI_CONNECT";
        case CRASH_STAGE_BOOT_AP_START: return "SETUP_AP_START";
        case CRASH_STAGE_BOOT_AP_IDLE: return "SETUP_AP_WAIT";
        case CRASH_STAGE_BOOT_AP_FAILED: return "SETUP_AP_FAILED";
        case CRASH_STAGE_NETWORK_SERVICES: return "NETWORK_SERVICES";
        case CRASH_STAGE_BOOT_LOOP_ENTER: return "ENTER_LOOP";
        case CRASH_STAGE_BOOT_FIRST_WEB: return "FIRST_WEB";
        case CRASH_STAGE_BOOT_MQTT_DNS: return "MQTT_DNS";
        case CRASH_STAGE_MQTT_CONNECT: return "MQTT_CONNECT";
        case CRASH_STAGE_BOOT_FIRST_METER: return "FIRST_METER_READ";
        case CRASH_STAGE_BOOT_ACCOUNTING: return "FIRST_ACCOUNTING";
        case CRASH_STAGE_BOOT_FIRST_DISPLAY: return "FIRST_LIVE_OLED";
        case CRASH_STAGE_BOOT_RUNNING: return "LOCAL_RUNNING";
        case CRASH_STAGE_BOOT_RECOVERY_IDLE: return "RECOVERY_WAIT";
        case CRASH_STAGE_MQTT_JSON_BUILD: return "MQTT_JSON_BUILD";
        case CRASH_STAGE_MQTT_PUBLISH: return "MQTT_PUBLISH";
        case CRASH_STAGE_WEB_ROOT: return "WEB_ROOT";
        case CRASH_STAGE_WEB_ROOT_HEADER: return "WEB_ROOT_HEADER";
        case CRASH_STAGE_WEB_ROOT_RAMCOPY: return "WEB_ROOT_RAMCOPY";
        case CRASH_STAGE_WEB_ROOT_WRITE: return "WEB_ROOT_WRITE";
        case CRASH_STAGE_WEB_SETUP: return "WEB_SETUP";
        case CRASH_STAGE_WEB_NET_DEBUG: return "WEB_NET_DEBUG";
        case CRASH_STAGE_REMOTE_OTA_PREP: return "REMOTE_OTA_PREP";
        case CRASH_STAGE_REMOTE_OTA_TLS: return "REMOTE_OTA_TLS";
        case CRASH_STAGE_REMOTE_OTA_HTTP: return "REMOTE_OTA_HTTP";
        case CRASH_STAGE_REMOTE_OTA_FLASH: return "REMOTE_OTA_FLASH";
        case CRASH_STAGE_BOOT_OLED_WRITE: return "CHECKPOINT_OLED_WRITE";
        case CRASH_STAGE_APP_SENSOR_INIT: return "APP_SENSOR_INIT";
        case CRASH_STAGE_APP_HISTORY_INIT: return "APP_HISTORY_INIT";
        default: return "UNKNOWN/NONE";
    }
}



// ===== MQTT ADMIN COMMAND STATE =====
enum MqttAdminCommand : uint8_t {
    MQTT_ADMIN_NONE = 0,
    MQTT_ADMIN_REMOTE_OTA,
    MQTT_ADMIN_OTA_REJECTED,
    MQTT_ADMIN_RESTART,
    // APPLICATION COMMAND ENUM
    MQTT_ADMIN_INVALID
};

MqttAdminCommand mqttAdminCommand = MQTT_ADMIN_NONE;
bool mqttAdminCommandPending = false;

// v4.03 remote normal restart, requested from MQTT.app Settings.
bool mqttAdminRestartPending = false;
unsigned long mqttAdminRestartRequestedMs = 0;
const unsigned long MQTT_ADMIN_RESTART_DELAY_MS = 1200UL;

// v2.26: remote pull-OTA command transported by the already-established
// MQTT admin channel. The firmware file itself is downloaded by the ESP32-C3
// over outbound HTTPS, so no router port-forwarding is required.
//
// Command:
// ota_https_v2|<size_bytes>|<md5_32hex>|<https_url>|<compiled_profile>
//
// Size and MD5 are mandatory. The URL should preferably be a short-lived
// private/presigned HTTPS URL is still recommended for controlled rollout.
// v3.00+ firmware binaries do not contain site Wi-Fi/MQTT credentials.
const size_t REMOTE_OTA_URL_BYTES = 420;
const size_t REMOTE_OTA_MD5_BYTES = 33;
char mqttAdminOtaUrl[REMOTE_OTA_URL_BYTES] = {0};
char mqttAdminOtaMd5[REMOTE_OTA_MD5_BYTES] = {0};
uint32_t mqttAdminOtaSize = 0;
const char* mqttAdminOtaError = "invalid_parameters";
// HTTPS application OTA retains bounded 4096-byte range transfers.
const uint16_t REMOTE_OTA_RANGE_BYTES = 4096;
const uint16_t REMOTE_OTA_STREAM_BUFFER_BYTES = 512;
const unsigned long REMOTE_OTA_IO_TIMEOUT_MS = 15000UL;
const unsigned long REMOTE_OTA_TOTAL_TIMEOUT_MS = 240000UL;
const unsigned long REMOTE_OTA_ACK_SETTLE_MS = 180UL;

WebServer server(80);
DNSServer credentialDns;
bool credentialPortalActive = false;
bool credentialPortalOnly = false;
// AP-only describes the radio, not whether a configured meter may keep running.
bool normalMeterInitialized = false;
unsigned long lastRead = 0;
const unsigned long interval = APP_SAMPLE_INTERVAL_MS;
bool lastMeterReadOk = false;
// Device transport and physical meter health are independent.
bool meterReadAttempted = false;
bool meterHasGoodSample = false;
bool meterHealthDirty = true;
bool meterHealthConnectionPublished = false;
uint32_t meterReadFailures = 0;
unsigned long meterLastGoodMs = 0;
unsigned long meterLastHealthPublishMs = 0;
char MQTT_HEALTH_TOPIC[MQTT_TOPIC_BASE_BYTES + sizeof("/health") - 1] = {};

unsigned long credentialPortalLastAttemptMs = 0;
const unsigned long CREDENTIAL_AP_RETRY_MS = 30000UL;
uint32_t credentialApAttempts = 0;
uint8_t credentialApLastFailure = 0; // 1=radio, 2=IP, 3=create, 4=PHY, 5=readback, 6=lost
const uint8_t CREDENTIAL_AP_CHANNEL = 1;
const uint8_t CREDENTIAL_AP_MAX_CONNECTIONS = 4;
const unsigned long CREDENTIAL_AP_HEALTH_CHECK_MS = 1000UL;
const unsigned long CREDENTIAL_AP_START_GRACE_MS = 3000UL;
const uint8_t CREDENTIAL_AP_LOSS_CONFIRM_CHECKS = 3;
const unsigned long CREDENTIAL_AP_DIAGNOSTIC_MS = 10000UL;
uint8_t credentialApLossChecks = 0;
unsigned long credentialApStartedMs = 0;
unsigned long credentialApLastDiagnosticMs = 0;
uint32_t credentialApBootRequests = 0;
const char* credentialApLastAction = "not_started";
esp_err_t credentialApLastNativeError = ESP_OK;
uint8_t credentialApLastCheckStage = 0;
bool credentialApStateVerified = false;
unsigned long credentialApLastHealthCheckMs = 0;
uint8_t credentialApChannel = 0;
uint8_t credentialApProtocol = 0;
uint8_t credentialApBandwidth = 0;
uint32_t credentialApHeapAtAttempt = 0;
bool credentialRoutesConfigured = false;
bool credentialRestartPending = false;
unsigned long credentialRestartRequestedMs = 0;
unsigned long wifiOfflineSinceMs = 0;
const unsigned long CREDENTIAL_RESTART_DELAY_MS = 1200UL;
// Router outages must not switch a configured device into setup AP.
const unsigned long WIFI_RETRY_STEPS_MS[] = {1500UL, 3000UL, 5000UL, 10000UL, 30000UL};
const unsigned long WIFI_ATTEMPT_TIMEOUT_MS = 15000UL;
const uint8_t WIFI_MAX_FAILED_ATTEMPTS = 3;
uint8_t wifiConsecutiveFailures = 0;
uint32_t wifiFailedAttempts = 0;
bool wifiAttemptActive = false;
unsigned long wifiAttemptStartedMs = 0;

// Shared Common Platform adaptive Wi-Fi power controller.
// Hard physical request limit: 15.0 dBm. The next Arduino level is
// 16.5 dBm, so it is intentionally excluded by the <=16 dBm policy.
const wifi_power_t WIFI_TX_LEVELS[] = {WIFI_POWER_8_5dBm, WIFI_POWER_11dBm,
                                      WIFI_POWER_13dBm, WIFI_POWER_15dBm};
const uint8_t WIFI_TX_LEVEL_TENTHS[] = {85, 110, 130, 150};
const uint8_t WIFI_TX_LAST_LEVEL = 3;
const unsigned long WIFI_TX_SCAN_TIMEOUT_MS = 7000UL;
const unsigned long WIFI_TX_REEVALUATE_MS = 60000UL;
uint8_t wifiTxCurrentLevel = 0;
uint8_t wifiTxRetryFloor = 0;
bool wifiTxApplied = false;
bool wifiTxScanning = false;
unsigned long wifiTxScanStartedMs = 0;
unsigned long wifiTxLastAdjustedMs = 0;
int wifiTxLastPreconnectRssi = -127;
uint32_t wifiTxScanCount = 0;
uint32_t wifiTxApplyFailures = 0;

int wifiTxTenths() {
    return wifiTxApplied ? (int)WIFI_TX_LEVEL_TENTHS[wifiTxCurrentLevel] : 0;
}
uint8_t wifiTxLevelForRssi(int rssi) {
    if (rssi >= -66) return 0; // 8.5 dBm
    if (rssi >= -74) return 1; // 11 dBm
    if (rssi >= -81) return 2; // 13 dBm
    return 3;                  // 15 dBm
}
bool wifiTxApply(uint8_t level, const char* reason) {
    if (level > WIFI_TX_LAST_LEVEL) level = WIFI_TX_LAST_LEVEL;
    if (!WiFi.setTxPower(WIFI_TX_LEVELS[level])) {
        ++wifiTxApplyFailures;
        Serial.printf("[WiFi TX] setTxPower FAILED (%s)\n", reason);
        return false;
    }
    wifiTxCurrentLevel = level;
    wifiTxApplied = true;
    wifiTxLastAdjustedMs = millis();
    Serial.printf("[WiFi TX] requested max=%d.%d dBm (%s), not measured RF\n",
        WIFI_TX_LEVEL_TENTHS[level] / 10, WIFI_TX_LEVEL_TENTHS[level] % 10, reason);
    return true;
}

bool wifiAttemptFailedEvent = false;
uint16_t wifiAttemptEventReason = 0;
bool wifiFallbackActive = false;
uint16_t wifiFallbackReason = 0;
int wifiFallbackStatus = 0;
uint8_t wifiFallbackFailureKind = 0; // 1=disconnect, 2=timeout, 3=radio, 4=IP, 5=begin
const uint8_t WIFI_RETRY_STEP_COUNT = sizeof(WIFI_RETRY_STEPS_MS) / sizeof(WIFI_RETRY_STEPS_MS[0]);
uint8_t wifiRetryStep = 0;
unsigned long lastWifiRetry = 0;
uint32_t wifiConnectAttempts = 0;
bool wifiWasConnected = false;
wifi_event_id_t wifiDisconnectHandler = 0;
static portMUX_TYPE wifiEventMux = portMUX_INITIALIZER_UNLOCKED;
static volatile bool wifiDisconnectPending = false;
static volatile uint16_t pendingWiFiDisconnectReason = 0;
uint16_t wifiLastDisconnectReason = 0;
const uint8_t FLASH_SETUP_PIN = 9; // NodeMCU BOOT GPIO9; release before reset to boot normally.
const uint32_t FLASH_DEBOUNCE_MS = 40UL;
const uint32_t FLASH_ACTION_COOLDOWN_MS = 750UL;
volatile bool flashPressActive = false;
volatile bool flashSetupRequested = false;
volatile uint32_t flashPressStartedMs = 0;
bool flashActionTaken = false;
uint32_t flashLastActionMs = 0;
const unsigned long WIFI_HINT_INTERVAL_MS = 15000UL;
const unsigned long WIFI_HINT_DURATION_MS = 4000UL;
bool wifiHintVisible = false;
bool wifiHintDrawPending = false;
unsigned long wifiHintStartedMs = 0;
unsigned long wifiHintLastFrameMs = 0;
String credentialApName;
String credentialApPassword;

// Setup AP is open; every HTTP page/action requires this PIN in AP mode.
const char AP_WEB_PIN[] = "12134";
const uint32_t AP_WEB_SESSION_MS = 15UL * 60UL * 1000UL;
char apWebSessionToken[33] = {0};
uint32_t apWebSessionStartedMs = 0;

void resetApWebSession() {
    apWebSessionToken[0] = '\0';
    apWebSessionStartedMs = 0;
}

bool apWebSessionValid() {
    if (!apWebSessionToken[0]) return false;
    if ((uint32_t)(millis() - apWebSessionStartedMs) >= AP_WEB_SESSION_MS) {
        resetApWebSession();
        return false;
    }
    String cookies = server.header("Cookie");
    char expected[44];
    snprintf(expected, sizeof(expected), "EMSESSION=%s", apWebSessionToken);
    int start = 0;
    while (start < (int)cookies.length()) {
        int end = cookies.indexOf(';', start);
        if (end < 0) end = cookies.length();
        String part = cookies.substring(start, end);
        part.trim();
        if (part == expected) return true;
        start = end + 1;
    }
    return false;
}

void sendApPinPage(int statusCode) {
    server.sendHeader("Cache-Control", "no-store");
    server.send_P(statusCode, PSTR("text/html; charset=utf-8"), PSTR(
        "<!doctype html><html><head><meta charset='utf-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>" DEVICE_LABEL " PIN</title><style>body{font:22px/1.35 Arial,sans-serif;max-width:760px;margin:auto;padding:10px;color:#142431}h1,h2,small{font-size:inherit}pre{font:inherit;white-space:pre-wrap;overflow-wrap:anywhere}input,button{font:inherit}</style></head><body>"
        "<h2>" DEVICE_LABEL "</h2><p>Enter the PIN to open this device.</p>"
        "<form method='post' action='/ap-login'>"
        "<label>PIN <input name='pin' type='password' inputmode='numeric' "
        "pattern='[0-9]{5}' minlength='5' maxlength='5' required autofocus "
        "autocomplete='off'></label> <button type='submit'>Open</button>"
        "</form></body></html>"));
}

bool requireApWebPin() {
    if (!credentialPortalActive && !credentialPortalOnly) return true; // Existing LAN behavior.
    if (apWebSessionValid()) return true;
    sendApPinPage(server.method() == HTTP_GET ? 200 : 403);
    return false;
}

void closeRootClient(WiFiClient& client, bool graceful);
class WebResponseScope {
public:
    WebResponseScope() : client(server.client()) {
        webResponseStartedMs = millis();
        webResponseActive = true;
        client.setTimeout(700);
        client.setConnectionTimeout(700);
        client.setNoDelay(true);
    }
    ~WebResponseScope() {
        if (client.connected()) closeRootClient(client, true);
        webResponseActive = false;
    }
private:
    WiFiClient client;
};

void handleApWebLogin() {
    WebResponseScope responseScope;
    if (!credentialPortalActive && !credentialPortalOnly) {
        server.send(404, "text/plain", "Not found");
        return;
    }
    if (server.arg("pin") != AP_WEB_PIN) {
        sendApPinPage(403);
        return;
    }
    // The PIN is never sent to the browser or stored in the cookie.
    snprintf(apWebSessionToken, sizeof(apWebSessionToken), "%08lX%08lX%08lX%08lX",
             (unsigned long)esp_random(), (unsigned long)esp_random(),
             (unsigned long)esp_random(), (unsigned long)esp_random());
    apWebSessionStartedMs = millis();
    char cookie[108];
    snprintf(cookie, sizeof(cookie),
             "EMSESSION=%s; Path=/; HttpOnly; SameSite=Strict; Max-Age=900",
             apWebSessionToken);
    server.sendHeader("Cache-Control", "no-store");
    server.sendHeader("Set-Cookie", cookie);
    server.sendHeader("Location", "/", true);
    server.send(303, "text/plain", "");
}


// Non-blocking, fixed-size diagnostic queue. Each distinct failure category
// gets at least four seconds on the OLED; retries never erase an earlier failure.
// RTC replay also retains an error through an automatic OTA/restart, without
// writing flash or changing credentials, accounting or Recovery storage.
enum OledDiagnosticCode : uint32_t {
    OLED_DIAG_WIFI = 1, OLED_DIAG_SETUP_AP = 2, OLED_DIAG_MQTT = 3,
    OLED_DIAG_METER = 4, OLED_DIAG_FILESYSTEM = 5, OLED_DIAG_CREDENTIALS = 6,
    OLED_DIAG_OTA = 7, OLED_DIAG_RECOVERY_CLEAR = 8
};
const unsigned long OLED_DIAGNOSTIC_HOLD_MS = 4000UL;
const uint8_t OLED_DIAGNOSTIC_SLOTS = 8;
struct OledDiagnostic {
    uint32_t code;
    char title[24];
    char detail[64];
};
OledDiagnostic oledDiagnostics[OLED_DIAGNOSTIC_SLOTS] = {};
uint8_t oledDiagnosticCount = 0;
bool oledDiagnosticStarted = false;
unsigned long oledDiagnosticStartedMs = 0;
unsigned long oledDiagnosticLastFrameMs = 0;
const uint32_t RTC_OLED_DIAGNOSTIC_MAGIC = 0x4F443136UL;
struct RtcOledDiagnostic {
    uint32_t magic;
    OledDiagnostic message;
    uint32_t checksum;
};
RTC_NOINIT_ATTR static volatile RtcOledDiagnostic rtcOledDiagnosticStorage;
void queueOledDiagnostic(uint32_t code, const char* title, const char* detail);
bool drawOledDiagnostic(unsigned long now);
void loadOledDiagnostic();
void serviceOledDiagnostic(unsigned long now);
const char* wifiFailureLabel();


const char* GREECE_TZ = "EET-2EEST,M3.5.0/3,M10.5.0/4";

bool littleFsReady = false;
bool networkServicesStarted = false;

// Ver 2.0: crash recovery latch. No credentials or measurements are stored.
const char* RECOVERY_FILE = "/recovery_v2.dat";
const uint32_t RECOVERY_MAGIC = 0x52323031UL; // R201
const uint32_t RECOVERY_REASON_FORCED_OTA = 0xF001UL;
struct RecoveryRecord {
    uint32_t magic;
    uint32_t reason;
    uint32_t exccause;
    uint32_t epc1;
    uint32_t epc2;
    uint32_t epc3;
    uint32_t excvaddr;
    uint32_t depc;
    uint32_t checksum;
};
RecoveryRecord recoveryIncident = {};
RecoveryRecord currentBootReset = {};
bool recoveryMode = false;
bool recoveryNormalRestartPending = false;
unsigned long recoveryNormalRestartRequestedMs = 0;
const unsigned long RECOVERY_NORMAL_RESTART_DELAY_MS = 750UL;
bool recoveryIncidentPersistent = false;
bool recoveryOtaInProgress = false;
bool recoveryResetClearFailed = false;
bool recoveryOtaClearFailed = false;
bool forceRecoveryRestartPending = false;
unsigned long forceRecoveryRestartRequestedMs = 0;
const unsigned long FORCE_RECOVERY_RESTART_DELAY_MS = 1000UL;
unsigned long recoveryWifiRetryMs = 0;
unsigned long recoveryDisplayMs = 0;
const unsigned long RECOVERY_WIFI_RETRY_MS = 15000UL;

// Το DEVICE_ID μπορεί να έχει "_" για τα MQTT topics.
// Για ArduinoOTA/mDNS το μετατρέπουμε σε hostname με "-".
// Παράδειγμα: jsy_house -> jsy-house
String otaHostname;


// =====================================================
// ΤΙΜΟΛΟΓΙΟ Ζ / Ζ1-Ζ2 ΚΑΙ ΣΗΜΑΣΙΑ ΚΑΤΑΧΩΡΗΣΕΩΝ ΔΕΗ
//
// Υπάρχουν δύο ανεξάρτητες έννοιες:
//
// 1) "Καταχώρηση Ένδειξης Μετρητή ΔΕΗ" (INTERMEDIATE / ek):
//    - γίνεται για το ΤΩΡΑ,
//    - παίρνει αυτόματα ώρα/ημερομηνία ESP,
//    - δένει την ένδειξη με τα ίδιας στιγμής internal Import/Export,
//    - είναι το anchor από το οποίο υπολογίζεται το "Εκτίμηση τώρα".
//
// 2) "Καταχώρηση Μέτρησης ΔΕΗ" (FINAL / tk):
//    - μπορεί να εισαχθεί αργότερα με την πραγματική ημερομηνία μέτρησης,
//    - είναι μόνο reference/comparison,
//    - ΔΕΝ ξαναδένει παλιούς ESP counters και ΔΕΝ μετακινεί το estimate anchor.
//
// Η "ΔΕΗ τελευταία" επιλέγεται με βάση την πιο πρόσφατη ΠΡΑΓΜΑΤΙΚΗ
// ημερομηνία μέτρησης, όχι τη σειρά εισαγωγής.
// Διαφορά = Εκτίμηση τώρα - ΔΕΗ τελευταία.
//
// Στο μονοζωνικό χρησιμοποιείται Ζ.
// Στο διζωνικό:
//   Ζ1 = κανονική ζώνη
//   Ζ2 = μειωμένη ζώνη
// Το estimate εξελίσσεται με signed net ενέργεια: Import - Export.
//
// ΩΡΑΡΙΟ Ζ2:
// Χειμερινή περίοδος (Νοέμβριος - Μάρτιος):
//   02:00-05:00 και 12:00-15:00
//
// Θερινή περίοδος (Απρίλιος - Οκτώβριος):
//   02:00-04:00 και 11:00-15:00
//
// Όλες οι άλλες ώρες είναι Ζ1.
//
// Η ώρα έρχεται από NTP. Μετά τον πρώτο έγκυρο συγχρονισμό,
// το system clock του ESP32-C3 συνεχίζει να λειτουργεί χωρίς Internet
// όσο το ESP παραμένει τροφοδοτημένο.
//
// Χωρίς battery-backed RTC, μετά από πλήρη διακοπή τροφοδοσίας
// δεν μπορεί να είναι γνωστή με απόλυτη ακρίβεια η ώρα μέσα στο κενό.
// Όταν επανέλθει NTP, το accounting κατανέμει το μη καταγεγραμμένο delta
// χρονικά στις ζώνες ως την καλύτερη δυνατή εκτίμηση.
// =====================================================

bool isSystemTimeValid() {
    return time(nullptr) > 1600000000;
}









void formatCurrentDateTime(char* out, size_t outSize) {
    if (!out || outSize == 0) return;
    out[0] = '\0';
    if (!isSystemTimeValid()) {
        snprintf(out, outSize, "--");
        return;
    }
    time_t nowValue = time(nullptr);
    struct tm localTm;
    if (!localtime_r(&nowValue, &localTm)) {
        snprintf(out, outSize, "--");
        return;
    }
    // Numeric formatting avoids linking the newlib locale tables into small
    // headless applications just to display an ISO-style timestamp.
    const int n = snprintf_P(out, outSize, PSTR("%04d-%02d-%02d %02d:%02d:%02d"),
        localTm.tm_year + 1900, localTm.tm_mon + 1, localTm.tm_mday,
        localTm.tm_hour, localTm.tm_min, localTm.tm_sec);
    if (n < 0 || (size_t)n >= outSize) out[0] = '\0';
}

// ==========================================================
// MODULE: STORAGE / ACCOUNTING
// ==========================================================

// =====================================================
// APPLICATION STORAGE INTEGRATION
// =====================================================


uint32_t oledDiagnosticChecksum(const RtcOledDiagnostic& record) {
    const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&record);
    uint32_t result = 2166136261UL;
    for (size_t i = 0; i < offsetof(RtcOledDiagnostic, checksum); ++i) {
        result = (result ^ bytes[i]) * 16777619UL;
    }
    return result;
}

void persistOledDiagnostic() {
    RtcOledDiagnostic record = {};
    if (oledDiagnosticCount) {
        record.message = oledDiagnostics[oledDiagnosticCount - 1];
        record.magic = RTC_OLED_DIAGNOSTIC_MAGIC;
        record.checksum = oledDiagnosticChecksum(record);
    }
    rtcOledDiagnosticStorage.magic = 0;
    const uint32_t magic = record.magic;
    record.magic = 0;
    platformWriteRetained(&rtcOledDiagnosticStorage, &record, sizeof(record));
    rtcOledDiagnosticStorage.magic = magic;
}

void loadOledDiagnostic() {
    if (!platformRtcContentsUsable()) {
        const RtcOledDiagnostic empty = {};
        platformWriteRetained(&rtcOledDiagnosticStorage, &empty, sizeof(empty));
        return;
    }
    RtcOledDiagnostic record = {};
    platformReadRetained(&record, &rtcOledDiagnosticStorage, sizeof(record));
    if (record.magic != RTC_OLED_DIAGNOSTIC_MAGIC ||
        record.checksum != oledDiagnosticChecksum(record) ||
        record.message.code < OLED_DIAG_WIFI || record.message.code > OLED_DIAG_RECOVERY_CLEAR ||
        !memchr(record.message.title, '\0', sizeof(record.message.title)) ||
        !memchr(record.message.detail, '\0', sizeof(record.message.detail))) return;
    oledDiagnostics[0] = record.message;
    oledDiagnosticCount = 1;
    oledDiagnosticStarted = false;
}

void queueOledDiagnostic(uint32_t code, const char* title, const char* detail) {
    if (code < OLED_DIAG_WIFI || code > OLED_DIAG_RECOVERY_CLEAR) return;
    for (uint8_t i = 0; i < oledDiagnosticCount; ++i) {
        if (oledDiagnostics[i].code == code) return; // Preserve the first cause.
    }
    if (oledDiagnosticCount >= OLED_DIAGNOSTIC_SLOTS) return;
    OledDiagnostic& message = oledDiagnostics[oledDiagnosticCount++];
    memset(&message, 0, sizeof(message));
    message.code = code;
    snprintf(message.title, sizeof(message.title), "%s", title ? title : "Diagnostic failure");
    snprintf(message.detail, sizeof(message.detail), "%s", detail ? detail : "Cause unavailable");
    if (oledDiagnosticCount == 1) {
        oledDiagnosticStarted = false;
    }
    persistOledDiagnostic();
}

bool drawOledDiagnostic(unsigned long now) {
    if (!bootOledReady || !oledDiagnosticCount) return false;
    if (oledDiagnosticStarted && now - oledDiagnosticStartedMs >= OLED_DIAGNOSTIC_HOLD_MS) {
        for (uint8_t i = 1; i < oledDiagnosticCount; ++i) oledDiagnostics[i - 1] = oledDiagnostics[i];
        --oledDiagnosticCount;
        oledDiagnosticStarted = false;
        persistOledDiagnostic();
        if (!oledDiagnosticCount) return false;
    }
    if (!oledDiagnosticStarted) {
        oledDiagnosticStarted = true;
        oledDiagnosticStartedMs = now;
    }
    const OledDiagnostic& message = oledDiagnostics[0];
    char line[33] = {};
    if (!platformDisplay.available()) {
        oledDiagnosticLastFrameMs = now;
        return false; // Lifecycle above still expires/persists the queue without OLED.
    }
    platformDisplay.clear();
    platformDisplay.font(DisplayFont::Small5x7);
    platformDisplay.text(0, 10, message.title);
    platformDisplay.font(DisplayFont::Tiny4x6);
    memcpy(line, message.detail, 32);
    line[32] = '\0';
    platformDisplay.text(0, 20, line);
    memcpy(line, message.detail + 32, 32);
    line[32] = '\0';
    platformDisplay.text(0, 30, line);
    if (credentialPortalActive) {
        // Keep setup access visible while retaining the diagnostic above it.
        platformDisplay.text(0, 40, credentialApName.c_str());
        snprintf(line, sizeof(line), "WiFi open PIN:%s", AP_WEB_PIN);
        platformDisplay.text(0, 50, line);
        snprintf(line, sizeof(line), "%s/setup", WiFi.softAPIP().toString().c_str());
        platformDisplay.text(0, 60, line);
    } else {
        if (normalMeterInitialized && !recoveryMode) {
            applicationFormatSummary(line, sizeof(line));
        } else {
            snprintf(line, sizeof(line), "%s", recoveryMode ? "OTA RECOVERY / meter paused" : "Setup / meter not initialized");
        }
        platformDisplay.text(0, 40, line);
        const unsigned long left = OLED_DIAGNOSTIC_HOLD_MS - (now - oledDiagnosticStartedMs);
        snprintf(line, sizeof(line), "Hold %lus; pending %u", (left + 999UL) / 1000UL,
                 (unsigned)(oledDiagnosticCount - 1));
        platformDisplay.text(0, 50, line);
        platformDisplay.text(0, 60, recoveryMode ? "BOOT:AP setup WEB:exit" : "FLASH:AP setup");
    }
    platformDisplay.present();
    oledDiagnosticLastFrameMs = millis();
    return true;
}

// Startup-only diagnostics: no Strings, delay(), heap allocation or flash writes.
// The CP is the operation ABOUT TO START. RTC separately identifies a hang while
// transmitting the checkpoint itself (stage=100, detail=the pending CP).
void bootCheckpoint(uint32_t stage) {
    if (!bootCheckpointsActive) return;
    bootLastCheckpoint = stage;
    markCrashStage(stage, ESP.getFreeHeap());
    if (!bootOledReady) return; // OLED init can only be recorded in RTC.
    if (drawOledDiagnostic(millis())) return;

    char heading[28];
    char memoryLine[32];
    snprintf(heading, sizeof(heading), "BOOT v%s  CP%lu", VERSION, (unsigned long)stage);
    snprintf(memoryLine, sizeof(memoryLine), "H:%lu T:%lus",
             (unsigned long)ESP.getFreeHeap(), (unsigned long)(millis() / 1000UL));
    markCrashStage(CRASH_STAGE_BOOT_OLED_WRITE, stage);
    platformDisplay.clear();
    platformDisplay.font(DisplayFont::Medium6x12);
    platformDisplay.text(0, 12, heading);
    platformDisplay.text(0, 28, "Starting:");
    platformDisplay.font(DisplayFont::Small5x7);
    platformDisplay.text(0, 43, crashStageLabel(stage));
    platformDisplay.text(0, 60, memoryLine);
    platformDisplay.present();
    markCrashStage(stage, ESP.getFreeHeap());
}

void finishBootCheckpoints(uint32_t stage) {
    if (!bootCheckpointsActive) return;
    bootLastCheckpoint = stage;
    markCrashStage(stage, ESP.getFreeHeap());
    bootCheckpointsActive = false; // Do not overwrite live/AP/Recovery screens.
}

void cs() {
    platformDisplay.clear();
    platformDisplay.present();
}

void doublePrintf(int fontCode, int line, int pos, const char* fmt, ...) {
    if (fontCode != 420) return;

    char buf[128];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    platformDisplay.font(DisplayFont::Normal10);
    int x = (pos - 1) * 8;
    int y = line * 15;
    platformDisplay.text(x, y, buf);
    platformDisplay.present();
}

// ==========================================================
// END MODULE: STORAGE / ACCOUNTING
// ==========================================================

// ==========================================================
// MODULE: MQTT / TLS / BROKER
// ==========================================================

// ===== MQTT CALLBACK: INCOMING APP/ADMIN REQUESTS =====
// Όταν ο χρήστης στείλει "update" στο MQTT_REQUEST_TOPIC,
// σηκώνεται μόνο ένα flag. Η ανάγνωση του επιλεγμένου μετρητή γίνεται αργότερα στο loop().
bool isValidMd5Hex(const String& value) {
    if (value.length() != 32) return false;
    for (size_t i = 0; i < 32; ++i) {
        char c = value[i];
        if (!((c >= '0' && c <= '9') ||
              (c >= 'a' && c <= 'f') ||
              (c >= 'A' && c <= 'F'))) {
            return false;
        }
    }
    return true;
}

bool parseHttpsHostPort(const char* url, char* host, size_t hostSize, uint16_t& port) {
    if (url == nullptr || host == nullptr || hostSize < 2) return false;
    const char prefix[] = "https://";
    if (strncmp(url, prefix, sizeof(prefix) - 1) != 0) return false;

    const char* begin = url + sizeof(prefix) - 1;
    const char* end = strchr(begin, '/');
    if (end == nullptr) end = begin + strlen(begin);
    if (end <= begin) return false;

    const char* colon = nullptr;
    for (const char* p = begin; p < end; ++p) {
        if (*p == ':') colon = p;
    }

    const char* hostEnd = colon ? colon : end;
    size_t len = (size_t)(hostEnd - begin);
    if (len == 0 || len >= hostSize) return false;

    memcpy(host, begin, len);
    host[len] = '\0';
    port = 443;

    if (colon) {
        unsigned long parsed = strtoul(colon + 1, nullptr, 10);
        if (parsed == 0 || parsed > 65535UL) return false;
        port = (uint16_t)parsed;
    }
    return true;
}







#ifndef PLATFORM_OTA_FILENAME_PREFIX
#define PLATFORM_OTA_FILENAME_PREFIX "CommonPlatform_ESP32C3_V"
#endif
#ifndef PLATFORM_OTA_FILENAME_SUFFIX
#define PLATFORM_OTA_FILENAME_SUFFIX "_APP.bin"
#endif
bool platformIsAllowedOtaUrl(const String& url) {
    const String prefix = "https://raw.githubusercontent.com/ApostolosGit/ESP32-OTA.bin/main/" PLATFORM_OTA_FILENAME_PREFIX;
    if (!url.startsWith(prefix) || !url.endsWith(PLATFORM_OTA_FILENAME_SUFFIX)) return false;
    String version = url.substring(prefix.length(), url.length() - strlen(PLATFORM_OTA_FILENAME_SUFFIX));
    const int separator = version.indexOf('_');
    if (separator <= 0 || version.indexOf('_', separator + 1) >= 0 ||
        version.length() != (size_t)separator + 3) return false;
    for (size_t i = 0; i < version.length(); ++i)
        if ((int)i != separator && (version[i] < '0' || version[i] > '9')) return false;
    return true;
}

// Stage validation only; network/flash work and replies remain outside callback.
bool stageRemoteHttpsOtaCommand(const String& command) {
    bool legacy = command.startsWith("ota_https|");
    if (!legacy && !command.startsWith("ota_https_v2|")) return false;

    mqttAdminOtaError = legacy ? "ota_protocol_required" : "invalid_parameters";
    mqttAdminOtaSize = 0;
    mqttAdminOtaUrl[0] = '\0';
    mqttAdminOtaMd5[0] = '\0';
    mqttAdminCommand = MQTT_ADMIN_OTA_REJECTED;
    mqttAdminCommandPending = true;
    if (legacy) return true;

    int p1 = command.indexOf('|');
    int p2 = command.indexOf('|', p1 + 1);
    int p3 = command.indexOf('|', p2 + 1);
    int p4 = command.indexOf('|', p3 + 1);
    if (p1 < 0 || p2 <= p1 || p3 <= p2 || p4 <= p3 ||
        command.indexOf('|', p4 + 1) >= 0) return true;

    String rawSize = command.substring(p1 + 1, p2);
    String rawMd5 = command.substring(p2 + 1, p3);
    String rawUrl = command.substring(p3 + 1, p4);
    String rawProfile = command.substring(p4 + 1);
    rawSize.trim(); rawMd5.trim(); rawUrl.trim(); rawProfile.trim();

    if (rawProfile != OTA_PROFILE_ID) {
        mqttAdminOtaError = "profile_mismatch";
        return true;
    }
    if (rawSize.length() == 0 || rawSize.length() > 10) return true;
    for (size_t i = 0; i < rawSize.length(); ++i)
        if (rawSize[i] < '0' || rawSize[i] > '9') return true;
    char* end = nullptr;
    unsigned long parsedSize = strtoul(rawSize.c_str(), &end, 10);
    bool valuesOk = end && *end == '\0' &&
        parsedSize >= 65536UL && parsedSize <= ESP.getFreeSketchSpace() &&
        isValidMd5Hex(rawMd5) && platformIsAllowedOtaUrl(rawUrl) &&
        rawUrl.length() > 12 && rawUrl.length() < REMOTE_OTA_URL_BYTES;
    if (!valuesOk) return true;

    mqttAdminOtaSize = (uint32_t)parsedSize;
    rawMd5.toLowerCase();
    rawMd5.toCharArray(mqttAdminOtaMd5, sizeof(mqttAdminOtaMd5));
    rawUrl.toCharArray(mqttAdminOtaUrl, sizeof(mqttAdminOtaUrl));
    mqttAdminCommand = MQTT_ADMIN_REMOTE_OTA;
    return true;
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
    if (strcmp(topic, MQTT_ADMIN_REQUEST_TOPIC) == 0) {
        String adminMessage;
        for (unsigned int i = 0; i < length; ++i) {
            adminMessage += (char)payload[i];
        }
        adminMessage.trim();

        if (mqttAdminCommandPending) return;

        if (applicationStageCommand(adminMessage)) return;

        if (adminMessage.equalsIgnoreCase("restart_esp")) {
            mqttAdminCommand = MQTT_ADMIN_RESTART;
            mqttAdminCommandPending = true;
            return;
        }

        if (stageRemoteHttpsOtaCommand(adminMessage)) return;

        mqttAdminCommand = MQTT_ADMIN_INVALID;
        mqttAdminCommandPending = true;
        return;
    }
    if (strcmp(topic, MQTT_REQUEST_TOPIC) != 0) return;

    // Hot path: APP auto-refreshes every minute. Avoid Arduino String
    // allocation/free churn here because, on the small ESP32-C3 heap, repeated
    // temporary allocations can reduce the largest free block over long runs.
    unsigned int first = 0;
    unsigned int last = length;
    while (first < last &&
           (payload[first] == ' ' || payload[first] == '\t' ||
            payload[first] == '\r' || payload[first] == '\n')) ++first;
    while (last > first &&
           (payload[last - 1] == ' ' || payload[last - 1] == '\t' ||
            payload[last - 1] == '\r' || payload[last - 1] == '\n')) --last;

    static const char updateWord[] = "update";
    bool isUpdate = (last - first) == sizeof(updateWord) - 1;
    for (unsigned int i = 0; isUpdate && i < sizeof(updateWord) - 1; ++i) {
        char c = (char)payload[first + i];
        if (c >= 'A' && c <= 'Z') c = (char)(c + ('a' - 'A'));
        if (c != updateWord[i]) isUpdate = false;
    }

    if (isUpdate) {
        // Keep even a closely spaced request pending. A second app/window may
        // have subscribed after the previous broadcast and still needs a reply.
        mqttUpdateRequested = true;
    }
}

void resetMqttUpdateRetry() {
    mqttUpdateRetryStep = 0;
    mqttUpdateRetryDelayMs = MQTT_UPDATE_MIN_INTERVAL_MS;
}

void scheduleMqttUpdateRetry() {
    uint8_t step = mqttUpdateRetryStep;
    if (step >= MQTT_UPDATE_RETRY_STEP_COUNT) step = MQTT_UPDATE_RETRY_STEP_COUNT - 1;
    mqttUpdateRetryDelayMs = MQTT_UPDATE_RETRY_STEPS_MS[step];
    if (mqttUpdateRetryStep + 1 < MQTT_UPDATE_RETRY_STEP_COUNT) mqttUpdateRetryStep++;
}

bool mqttUpdateResponseDue(unsigned long now) {
    return mqttUpdateRequested &&
           (lastMqttUpdatePublishMs == 0 ||
            now - lastMqttUpdatePublishMs >= mqttUpdateRetryDelayMs);
}

void resetMqttRuntimeFailureStreak() {
    mqttRuntimeConnectFailureStreak = 0;
}

void noteMqttRuntimeTransportFailure() {
    // Only self-heal a unit that has already proved its broker credentials and
    // still reports router Wi-Fi as connected. DNS failures are handled by the
    // normal retry path and do not trigger a LAN recycle.
    if (!mqttEverConnected || WiFi.status() != WL_CONNECTED ||
        mqttLastAttemptState != -2) {
        resetMqttRuntimeFailureStreak();
        return;
    }
    if (mqttRuntimeConnectFailureStreak < 0xFF) ++mqttRuntimeConnectFailureStreak;

    const uint32_t freeHeap = ESP.getFreeHeap();
    const uint32_t maxBlock = platformMaxFreeBlock();
    const bool localResourcePressure =
        mqttLastSslErrorCode == -1000 ||
        freeHeap < MQTT_TLS_MIN_HEAP_BYTES ||
        maxBlock < MQTT_TLS_MIN_BLOCK_BYTES;
    const uint8_t threshold = localResourcePressure
        ? MQTT_RUNTIME_RECOVERY_FAILURES_LOW_HEAP
        : MQTT_RUNTIME_RECOVERY_FAILURES_GENERIC;

    if (mqttRuntimeConnectFailureStreak >= threshold) {
        networkStackRecoveryPending = true;
    }
}

// ===== MQTT CONFIGURATION VALIDATION =====
bool mqttSettingsReady() {
    return mqttHost[0] != '\0' &&
           mqttUser[0] != '\0' &&
           mqttPassword[0] != '\0' &&
           mqttPort != 0;
}

// ===== MQTT / TLS INITIALIZATION =====
void configureMqtt() {
    mqttNet.useBuiltinCACertBundle();
    mqttNet.setConnectionTimeout(MQTT_TLS_TIMEOUT_MS);
    mqttNet.setTimeout(MQTT_TLS_TIMEOUT_MS);
    mqttNet.setHandshakeTimeout((MQTT_TLS_TIMEOUT_MS + 999UL) / 1000UL);
    mqttClient.setServer(mqttHost, mqttPort);
    mqttClient.setCallback(mqttCallback);
    mqttClient.setKeepAlive(30);
    mqttClient.setSocketTimeout(3);
    mqttClient.setBufferSize(MQTT_PACKET_BUFFER_BYTES);
}

// Descriptions live in flash and must be read via writeCredentialProgmem().
// Keeping these outside RAM matters while MQTT/mbedTLS is connected.
const char* mqttStateDescription(int code) {
    // Explicit branches avoid RAM-resident compiler switch lookup tables.
    if (code == -4) return PSTR("MQTT reply timed out");
    if (code == -3) return PSTR("MQTT connection was lost");
    if (code == -2) return PSTR("Connection failed; network or TLS may be involved");
    if (code == -1) return PSTR("MQTT client is disconnected");
    if (code == 0) return PSTR("MQTT connection accepted");
    if (code == 1) return PSTR("Broker rejected the MQTT protocol version");
    if (code == 2) return PSTR("Broker rejected the client ID");
    if (code == 3) return PSTR("Broker reported that the service is unavailable");
    if (code == 4) return PSTR("Broker rejected the username or password");
    if (code == 5) return PSTR("Broker denied access; check credentials and permissions");
    return PSTR("Unknown MQTT status");
}

const char* wifiStatusDescription(int code) {
    // Explicit branches avoid RAM-resident compiler switch lookup tables.
    if (code == 0) return PSTR("Idle or connection attempt in progress");
    if (code == 1) return PSTR("Configured Wi-Fi network was not found");
    if (code == 2) return PSTR("Wi-Fi scan completed");
    if (code == 3) return PSTR("Connected to the router");
    if (code == 4) return PSTR("Connection attempt failed; see recorded disconnect reason");
    if (code == 5) return PSTR("Connection to the router was lost");
    if (code == 6) return PSTR("Disconnected from the router");
    if (code == 255) return PSTR("Wi-Fi interface unavailable");
    return PSTR("Unknown Wi-Fi status");
}

const char* wifiDisconnectDescription(uint16_t code) {
    // Explicit branches avoid RAM-resident compiler switch lookup tables.
    if (code == 0) return PSTR("No disconnect reason recorded");
    if (code == 1) return PSTR("Unspecified Wi-Fi disconnect");
    if (code == 2) return PSTR("Wi-Fi authentication expired");
    if (code == 3) return PSTR("Wi-Fi authentication was ended");
    if (code == 4) return PSTR("Router association expired");
    if (code == 5) return PSTR("Router rejected association: too many clients");
    if (code == 6 || code == 9) return PSTR("Router requires Wi-Fi authentication");
    if (code == 7) return PSTR("Client was not associated with the router");
    if (code == 8) return PSTR("Router association was ended");
    if (code == 10 || code == 11) return PSTR("Router rejected radio capabilities");
    if (code == 13 || code == 17) return PSTR("Invalid or inconsistent Wi-Fi security information");
    if (code == 14) return PSTR("Wi-Fi message integrity check failed");
    if (code == 15 || code == 204) return PSTR("Wi-Fi security handshake timed out; password or radio link may be involved");
    if (code == 16) return PSTR("Wi-Fi group key update timed out");
    if (code == 18 || code == 19 || code == 20 || code == 21 || code == 22 || code == 24) return PSTR("Router rejected the Wi-Fi security configuration");
    if (code == 23) return PSTR("Enterprise Wi-Fi authentication failed");
    if (code == 200) return PSTR("Router beacons were lost; signal or router interruption may be involved");
    if (code == 201) return PSTR("Configured Wi-Fi network was not found");
    if (code == 202) return PSTR("Wi-Fi authentication failed; check credentials and router settings");
    if (code == 203) return PSTR("Router association failed");
    if (code == 205) return PSTR("Router connection failed");
    if (code == 210 || code == 211) return PSTR("Router security is incompatible with the configured Wi-Fi authentication");
    if (code == 212) return PSTR("Router signal is below the required strength");
    return PSTR("Unknown SDK disconnect reason");
}

const char* wifiFailureExplanation() {
    if (wifiFallbackFailureKind == 2) return PSTR("Connection timed out: no usable router connection/IP was obtained within 15 seconds");
    if (wifiFallbackFailureKind == 3) return PSTR("The station Wi-Fi radio could not be initialized");
    if (wifiFallbackFailureKind == 4) return PSTR("The selected static LAN IP could not be configured");
    if (wifiFallbackFailureKind == 5 && !wifiFallbackReason) return PSTR("The router connection attempt could not be started");
    return wifiDisconnectDescription(wifiFallbackReason);
}

const char* apFailureDescription(uint8_t code) {
    // Explicit branches avoid RAM-resident compiler switch lookup tables.
    if (code == 0) return PSTR("No AP startup failure recorded");
    if (code == 1) return PSTR("Could not enable the AP radio mode");
    if (code == 2) return PSTR("Could not configure the AP IP address");
    if (code == 3) return PSTR("Could not start the setup access point");
    if (code == 4) return PSTR("Could not configure the AP PHY compatibility settings");
    if (code == 5) return PSTR("AP native configuration readback failed");
    if (code == 6) return PSTR("AP native state was lost; setup will retry");
    return PSTR("Unknown AP startup failure");
}

const char* networkOperationDescription() {
    if (wifiFallbackActive && credentialPortalActive) return PSTR("Router offline: temporary AP+STA active; router reconnection continues");
    if (credentialPortalOnly) return PSTR("AP-only setup: router Wi-Fi and MQTT are intentionally paused");
    if (recoveryMode) return PSTR("OTA-only recovery: MQTT measurements are paused");
    if (WiFi.status() != WL_CONNECTED) return PSTR("Waiting for the router Wi-Fi connection before MQTT can connect");
    if (!mqttSettingsReady()) return PSTR("MQTT settings are incomplete");
    if (mqttSettingsReady() && !isSystemTimeValid())
        return PSTR("Router Wi-Fi is connected; waiting for NTP time before certificate-verified TLS");
    if (mqttClient.connected()) return PSTR("Router Wi-Fi and MQTT are connected");
    return PSTR("Router Wi-Fi is connected; MQTT is disconnected and will retry");
}

const char* dnsLookupDescription(bool attempted, bool usable) {
    if (!attempted) {
        if (WiFi.status() != WL_CONNECTED) return PSTR("Not run: router Wi-Fi is disconnected");
        return PSTR("Not run: MQTT host is not configured");
    }
    return usable ? PSTR("Lookup succeeded with a usable address")
                  : PSTR("Lookup failed or returned an unusable address");
}

// ===== MQTT CONNECT / RECONNECT / SUBSCRIBE =====
void scheduleMqttRetry() {
    if (mqttEverConnected) {
        uint8_t step = mqttRetryStep;
        if (step >= MQTT_RUNTIME_RETRY_STEP_COUNT) {
            step = MQTT_RUNTIME_RETRY_STEP_COUNT - 1;
        }
        mqttRetryDelayMs = MQTT_RUNTIME_RETRY_STEPS_MS[step];
        if (mqttRetryStep + 1 < MQTT_RUNTIME_RETRY_STEP_COUNT) mqttRetryStep++;
    } else {
        uint8_t step = mqttRetryStep;
        if (step >= MQTT_RETRY_STEP_COUNT) step = MQTT_RETRY_STEP_COUNT - 1;
        mqttRetryDelayMs = MQTT_RETRY_STEPS_MS[step];
        if (mqttRetryStep + 1 < MQTT_RETRY_STEP_COUNT) mqttRetryStep++;
    }
    // Cooldown starts AFTER the blocking DNS/TCP/TLS operation, not before it.
    lastMqttReconnectMs = millis();
}

void maintainMqtt(unsigned long now) {
    if (WiFi.status() != WL_CONNECTED || !mqttSettingsReady()) return;

    if (!isSystemTimeValid()) return;
    if (mqttClient.connected()) {
        mqttRetryStep = 0;
        mqttRetryDelayMs = mqttEverConnected
            ? MQTT_RUNTIME_RETRY_STEPS_MS[0]
            : MQTT_RETRY_STEPS_MS[0];

        // PubSubClient::loop() is the MQTT keepalive/liveness check. v4.01
        // ignored its return value; after an Internet-only router outage the
        // stale TLS transport could therefore remain unusable until reboot.
        // Explicitly tear it down and enter fast runtime recovery.
        if (!mqttClient.loop()) {
            mqttNet.stop();
            mqttRetryStep = 0;
            mqttRetryDelayMs = MQTT_RUNTIME_RETRY_STEPS_MS[0];
            lastMqttReconnectMs = millis();
        }
        return;
    }

    if (lastMqttReconnectMs != 0 &&
        now - lastMqttReconnectMs < mqttRetryDelayMs) {
        return;
    }

    bootFirstMqttAttempted = true;

    // A previously healthy MQTT connection that died while Wi-Fi stayed
    // associated may leave mbedTLS/TCP state behind. Start every runtime
    // recovery attempt from a clean transport before DNS/TLS reconnect.
    if (mqttEverConnected) mqttNet.stop();

    mqttConnectAttempts++;
    mqttLastAttemptMs = millis();
    mqttLastConnectSucceeded = false;
    mqttLastStatusPublishSucceeded = false;
    mqttLastRequestSubscribeSucceeded = false;
    mqttLastAdminSubscribeSucceeded = false;
    mqttLastSslErrorCode = 0;
    mqttLastSslErrorText[0] = '\0';

    // Explicitly bounded DNS preflight also warms the cache for the hostname
    // TLS connection below (keep the hostname/SNI, not an IP-only TLS connect).
    bootCheckpoint(CRASH_STAGE_BOOT_MQTT_DNS);
    IPAddress brokerIp;
    runtimeBreadcrumb(RB_MQTT_DNS_BEGIN);
    const int mqttDnsResult=platformResolveHost(mqttHost,brokerIp,MQTT_DNS_TIMEOUT_MS);
    runtimeBreadcrumb(RB_MQTT_DNS_END,(uint16_t)mqttDnsResult);
    if (mqttDnsResult != 1) {
        mqttLastAttemptState = -2;
        snprintf(mqttLastSslErrorText, sizeof(mqttLastSslErrorText), "MQTT DNS unavailable");
        mqttConnectFailures++;
        resetMqttRuntimeFailureStreak();
        queueOledDiagnostic(OLED_DIAG_MQTT, "MQTT DNS FAILED", "Broker hostname lookup failed; retrying");
        scheduleMqttRetry();
        return;
    }

    char clientId[sizeof(mqttDeviceId) + sizeof("esp-") - 1];
    snprintf_P(clientId, sizeof(clientId), PSTR("esp-%s"), mqttDeviceId);

    bootCheckpoint(CRASH_STAGE_MQTT_CONNECT);
    markCrashStage(CRASH_STAGE_MQTT_CONNECT, ESP.getFreeHeap());

    uint32_t connectFreeHeap = ESP.getFreeHeap();
    uint32_t connectMaxBlock = platformMaxFreeBlock();
    mqttConnectHeapAtAttempt = connectFreeHeap;
    mqttConnectBlockAtAttempt = connectMaxBlock;
    const uint32_t connectHeapFloor = MQTT_TLS_MIN_HEAP_BYTES;
    if (connectFreeHeap < connectHeapFloor ||
        connectMaxBlock < MQTT_TLS_MIN_BLOCK_BYTES) {
        ++mqttConnectPreflightDeferrals;
        mqttLastSslErrorCode = -1000;
        snprintf(mqttLastSslErrorText, sizeof(mqttLastSslErrorText),
                 "TLS connect skipped: low heap RX=%u free=%lu max=%u",
                 (unsigned)mqttTlsRecvConfiguredBytes,
                 (unsigned long)connectFreeHeap,
                 (unsigned)connectMaxBlock);
        mqttLastAttemptState = -2;
        mqttConnectFailures++;
        noteMqttRuntimeTransportFailure();
        queueOledDiagnostic(OLED_DIAG_MQTT, "MQTT TLS LOW HEAP", "TLS attempt deferred; network self-heal if persistent");
        scheduleMqttRetry();
        return;
    }

    // Bound native socket connection and TLS handshake independently from
    // PubSubClient's packet-read timeout.
    mqttNet.setConnectionTimeout(MQTT_TLS_TIMEOUT_MS);
    mqttNet.setHandshakeTimeout((MQTT_TLS_TIMEOUT_MS + 999UL) / 1000UL);
    runtimeBreadcrumb(RB_MQTT_TLS_BEGIN);
    bool ok = mqttClient.connect(
        clientId,
        mqttUser,
        mqttPassword,
        MQTT_STATUS_TOPIC,
        0,
        true,
        "offline"
    );
    runtimeBreadcrumb(RB_MQTT_TLS_END, ok ? 1 : 0);
    mqttLastConnectSucceeded = ok;
    mqttLastAttemptState = mqttClient.state();
    // Snapshot actual mbedTLS failure ASAP. Error 0 on a failed connect
    // means the SSL error is unavailable, not that TLS succeeded.
    if (!ok && mqttLastSslErrorText[0] == '\0') {
        mqttLastSslErrorCode =
            mqttNet.lastError(mqttLastSslErrorText,
                                    sizeof(mqttLastSslErrorText));
    }

    if (ok) {
        mqttEverConnected = true;
        resetMqttRuntimeFailureStreak();
        mqttRetryStep = 0;
        mqttRetryDelayMs = MQTT_RUNTIME_RETRY_STEPS_MS[0];
        mqttLastStatusPublishSucceeded =
            mqttClient.publish(MQTT_STATUS_TOPIC, "online", true);
        mqttLastRequestSubscribeSucceeded =
            mqttClient.subscribe(MQTT_REQUEST_TOPIC);
        mqttLastAdminSubscribeSucceeded =
            mqttClient.subscribe(MQTT_ADMIN_REQUEST_TOPIC);
    } else {
        mqttConnectFailures++;
        noteMqttRuntimeTransportFailure();
        char detail[64];
        snprintf(detail, sizeof(detail), "MQTT state=%d; SSL=%ld. Retrying",
                 mqttLastAttemptState, (long)mqttLastSslErrorCode);
        queueOledDiagnostic(OLED_DIAG_MQTT, "MQTT CONNECT FAILED", detail);
        mqttNet.stop();
        scheduleMqttRetry();
    }
}

// ===== MQTT HEALTH + LIVE MEASUREMENT PUBLISH =====
// v2.12: normal PubSubClient publish(), but JSON is built in a bounded
// stack buffer instead of allocating a second ~1KB heap buffer. This avoids
// the v2.11 MQTT_PUBLISH condition where only 1736 bytes of heap remained.
// Small retained discovery/health frame, also available without any meter.
// Identity derives from the station MAC, independent of site and credentials.
void initializeMqttIdentity() {
    uint8_t mac[6] = {};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf_P(mqttDeviceId, sizeof(mqttDeviceId), PSTR("%s-%02X%02X%02X%02X%02X%02X"), DEVICE_ID,
        (unsigned)mac[0], (unsigned)mac[1], (unsigned)mac[2], (unsigned)mac[3], (unsigned)mac[4], (unsigned)mac[5]);
    snprintf_P(MQTT_STATE_TOPIC, sizeof(MQTT_STATE_TOPIC), PSTR(MQTT_TOPIC_PREFIX "%s/state"), mqttDeviceId);
    snprintf_P(MQTT_STATUS_TOPIC, sizeof(MQTT_STATUS_TOPIC), PSTR(MQTT_TOPIC_PREFIX "%s/status"), mqttDeviceId);
    snprintf_P(MQTT_REQUEST_TOPIC, sizeof(MQTT_REQUEST_TOPIC), PSTR(MQTT_TOPIC_PREFIX "%s/request"), mqttDeviceId);
    snprintf_P(MQTT_ADMIN_REQUEST_TOPIC, sizeof(MQTT_ADMIN_REQUEST_TOPIC), PSTR(MQTT_TOPIC_PREFIX "%s/admin/request"), mqttDeviceId);
    snprintf_P(MQTT_ADMIN_RESPONSE_TOPIC, sizeof(MQTT_ADMIN_RESPONSE_TOPIC), PSTR(MQTT_TOPIC_PREFIX "%s/admin/response"), mqttDeviceId);
    snprintf_P(MQTT_ADMIN_CSV_TOPIC, sizeof(MQTT_ADMIN_CSV_TOPIC), PSTR(MQTT_TOPIC_PREFIX "%s/admin/csv"), mqttDeviceId);
    snprintf_P(MQTT_HEALTH_TOPIC, sizeof(MQTT_HEALTH_TOPIC), PSTR(MQTT_TOPIC_PREFIX "%s/health"), mqttDeviceId);
    applicationInitializeTopics();
}

bool publishMeterHealth() {
    if (!mqttClient.connected()) return false;
    char payload[576];
    char age[16] = "null";
    if (meterHasGoodSample) snprintf(age, sizeof(age), "%lu", (unsigned long)(millis() - meterLastGoodMs));
    int n = snprintf_P(payload, sizeof(payload), PSTR("{\"meter\":\"%s\",\"device\":\"%s\",\"firmware\":\"%s\","
        "\"application\":\"%s\",\"platform_version\":\"%s\",\"driver\":\"%s\","
        "\"oled\":\"%s\",\"profile\":\"%s\",\"ota_protocol\":%u,"
        "\"chip_id\":%lu,\"dual_zone\":%s,\"meter_ok\":%s,\"read_attempted\":%s,"
        "\"has_sample\":%s,\"last_sample_age_ms\":%s,\"read_failures\":%lu,"
        "\"error\":\"%s\",\"rssi\":%d,\"wifi_rssi_dbm\":%d,\"wifi_tx_max_dbm\":%.1f}"),
        DEVICE_KIND, mqttDeviceId, VERSION, APPLICATION_ID, PLATFORM_VERSION, DRIVER_ID,
        OLED_ID, OTA_PROFILE_ID, (unsigned)OTA_PROTOCOL_VERSION, (unsigned long)platformChipId(),
        applicationDualZoneEnabled() ? "true" : "false", lastMeterReadOk ? "true" : "false",
        meterReadAttempted ? "true" : "false", meterHasGoodSample ? "true" : "false", age,
        (unsigned long)meterReadFailures,
        !meterReadAttempted ? "awaiting_first_read" : (lastMeterReadOk ? "" : PLATFORM_SENSOR_ERROR),
        WiFi.status() == WL_CONNECTED ? (int)WiFi.RSSI() : 0,
        WiFi.status() == WL_CONNECTED ? (int)WiFi.RSSI() : 0,
        (double)wifiTxTenths() / 10.0);
    if (n <= 0 || n >= (int)sizeof(payload)) return false;
    bool ok = mqttClient.publish(MQTT_HEALTH_TOPIC, payload, true);
    if (ok) {
        meterHealthDirty = false; meterHealthConnectionPublished = true;
        meterLastHealthPublishMs = millis();
    }
    return ok;
}

void serviceMeterHealth(unsigned long now) {
    if (!mqttClient.connected()) { meterHealthConnectionPublished = false; return; }
    if (meterHealthDirty || !meterHealthConnectionPublished || now - meterLastHealthPublishMs >= 30000UL)
        publishMeterHealth();
}



// Keep the large sample buffer out of the sensor-error publish stack.
bool publishMqttMeasurements() {
    if (!mqttClient.connected()) return false;
#if !PLATFORM_LOCAL_CONTROL_APPLICATION
    if (!lastMeterReadOk) return publishMeterHealth();
#endif
    return publishMqttSample();
}

// ==========================================================
// END MODULE: MQTT / TLS / BROKER
// ==========================================================

// ==========================================================
// MODULE: METER HEALTH / OLED ADAPTER
// ==========================================================

// =====================================================
// OLED / COMMON METER ADAPTERS
// =====================================================
// Every physical read updates health, including utility indication requests.
bool readMeterWithHealth() {
    runtimeBreadcrumb(RB_SENSOR_BEGIN);
    bool ok = applicationReadMeasurements();
    runtimeBreadcrumb(RB_SENSOR_END, ok ? 1 : 0);
    if (!meterReadAttempted || ok != lastMeterReadOk) meterHealthDirty = true;
    meterReadAttempted = true; lastMeterReadOk = ok;
    if (ok) { meterHasGoodSample = true; meterLastGoodMs = millis(); }
    else if (meterReadFailures != 0xFFFFFFFFUL) ++meterReadFailures;
    return ok;
}

// ==========================================================
// END MODULE: METER HEALTH / OLED ADAPTER
// ==========================================================

// ==========================================================
// MODULE: LOCAL WEB
// ==========================================================

// ===== LOCAL WEB DASHBOARD / LOW-HEAP GUARDS =====
// Η πλήρης σελίδα στέλνεται μία φορά και οι live μετρήσεις ανανεώνονται από
// το /live. Τα writes γίνονται σε μικρά RAM slices, με heap/max-block guards,
// deadline και explicit socket close. Έτσι το Web δεν ανταγωνίζεται το
// TLS για μεγάλο συνεχόμενο heap block ούτε αφήνει abandoned TCP sockets.

bool webRootHeapHealthy(uint32_t* freeOut = nullptr,
                        uint32_t* maxBlockOut = nullptr,
                        uint8_t* fragmentationOut = nullptr) {
    uint32_t freeHeap = 0;
    uint32_t maxBlock = 0;
    uint8_t fragmentation = 0;
    platformGetHeapStats(&freeHeap, &maxBlock, &fragmentation);
    if (freeOut) *freeOut = freeHeap;
    if (maxBlockOut) *maxBlockOut = maxBlock;
    if (fragmentationOut) *fragmentationOut = fragmentation;
    return freeHeap >= WEB_ROOT_MIN_HEAP_BYTES &&
           maxBlock >= WEB_ROOT_MIN_BLOCK_BYTES;
}

void closeRootClient(WiFiClient& client, bool graceful = false) {
    // Native NetworkClient::stop() closes its shared socket immediately; it
    // has no ESP8266 timeout argument. A failed response may abort queued TX.
    const int socket = client.fd();
#if LWIP_SO_LINGER
    if (!graceful && socket >= 0) {
        const struct linger abortNow = {1, 0};
        setsockopt(socket, SOL_SOCKET, SO_LINGER, &abortNow, sizeof(abortNow));
    }
#endif
    if (graceful && socket >= 0) shutdown(socket, SHUT_WR);
    client.stop();
    ++webRootForcedCloseCount;
    yield();
}

bool writeWebBytes(WiFiClient& client, const uint8_t* data, size_t length, bool emergency) {
    if (!data || !length) return true;
    const size_t sliceBytes = emergency ? 32 : WEB_ROOT_RAM_SLICE_BYTES;
    const uint32_t started = millis();
    for (size_t offset = 0; offset < length;) {
        uint32_t freeHeap = 0, maxBlock = 0;
        uint8_t fragmentation = 0;
        platformGetHeapStats(&freeHeap, &maxBlock, &fragmentation);
        const bool expired = webResponseActive
            ? (uint32_t)(millis() - webResponseStartedMs) >= WEB_RESPONSE_BUDGET_MS
            : (uint32_t)(millis() - started) >= WEB_RESPONSE_BUDGET_MS;
        const int socket = client.fd();
        if (expired || socket < 0 || !client.connected() ||
            freeHeap < (emergency ? 512UL : WEB_ROOT_MIN_HEAP_BYTES) ||
            maxBlock < (emergency ? 128U : WEB_ROOT_MIN_BLOCK_BYTES)) {
            ++webRootWriteAbortCount;
            closeRootClient(client, false);
            return false;
        }
        size_t chunk = length - offset;
        if (chunk > sliceBytes) chunk = sliceBytes;
        markCrashStage(CRASH_STAGE_WEB_ROOT_WRITE, freeHeap);
        // NetworkClient::write can retry select() for ten seconds. Raw
        // nonblocking socket writes keep every partial/EAGAIN retry inside
        // the common four-second budget; no flush() is used (it is a no-op).
        const int sent = send(socket, data + offset, chunk, MSG_DONTWAIT);
        if (sent > 0) offset += (size_t)sent;
        else if (sent < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) {
            delay(1);
            continue;
        } else {
            ++webRootWriteAbortCount;
            closeRootClient(client, false);
            return false;
        }
        yield();
    }
    return true;
}

bool writeRootBytes(WiFiClient& client, const uint8_t* data, size_t length) {
    return writeWebBytes(client,data,length,false);
}

void sendRootBusyAndClose(WiFiClient& client, int code, uint32_t freeHeap, uint32_t maxBlock) {
    // Unlike 204, this gives a first-time browser an explicit reason to retry.
    char body[96], response[320];
    int bodyLen=snprintf(body,sizeof(body),"Web busy (%d). Heap=%lu, block=%u. Retry after one second.\n",
                         code,(unsigned long)freeHeap,(unsigned)maxBlock);
    if(bodyLen<=0 || bodyLen>=(int)sizeof(body)) {closeRootClient(client,false);return;}
    int n=snprintf(response,sizeof(response),
        "HTTP/1.1 %d %s\r\nContent-Type: text/plain; charset=utf-8\r\n"
        "Content-Length: %d\r\nRetry-After: 1\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n%s",
        code,code==429 ? "Too Many Requests" : "Service Unavailable",bodyLen,body);
    if(n>0 && n<(int)sizeof(response)) writeWebBytes(client,reinterpret_cast<const uint8_t*>(response),(size_t)n,true);
    if(client.connected()) closeRootClient(client,true);
}

bool writeRootProgmemViaRam(WiFiClient& client, PGM_P content, size_t length) {
    uint8_t ramSlice[WEB_ROOT_RAM_SLICE_BYTES];
    size_t offset = 0;

    while (offset < length) {
        size_t chunk = length - offset;
        if (chunk > sizeof(ramSlice)) chunk = sizeof(ramSlice);

        uint32_t freeHeap = 0;
        if (!webRootHeapHealthy(&freeHeap, nullptr, nullptr)) {
            markCrashStage(CRASH_STAGE_WEB_ROOT_RAMCOPY, freeHeap);
            webRootWriteAbortCount++;
            closeRootClient(client, false);
            return false;
        }

        markCrashStage(CRASH_STAGE_WEB_ROOT_RAMCOPY, freeHeap);
        memcpy_P(ramSlice, content + offset, chunk);

        if (!writeRootBytes(client, ramSlice, chunk)) return false;

        offset += chunk;
        yield();
    }
    return true;
}



// ==========================================================
// END MODULE: LOCAL WEB
// ==========================================================

// ==========================================================
// MODULE: ADMIN / REMOTE OTA
// ==========================================================

// ===== MQTT ADMIN RESPONSE HELPER =====
void publishMqttAdminResponse(const char* payload) {
    if (!mqttClient.connected()) return;

    mqttClient.publish(
        MQTT_ADMIN_RESPONSE_TOPIC,
        payload,
        false
    );
}

// =====================================================
// ΔΕΗ UTILITY READINGS / ESTIMATE ANCHORS
// =====================================================











// ===== REMOTE HTTPS RANGE PULL OTA =====
// Redraw the complete OTA status frame in one OLED buffer transaction.
// doublePrintf() intentionally preserves the existing framebuffer, so using
// it for a changing progress string leaves old pixels behind.
void drawRemoteOtaStatus(const char* status) {
    char sizeLine[32];
    snprintf(
        sizeLine,
        sizeof(sizeLine),
        "%lu bytes",
        (unsigned long)mqttAdminOtaSize
    );

    platformDisplay.clear();
    platformDisplay.font(DisplayFont::Normal10);
    platformDisplay.text(0, 15, "REMOTE OTA");
    platformDisplay.text(0, 30, sizeLine);
    platformDisplay.text(0, 45, status ? status : "");
    platformDisplay.present();
}

void remoteOtaFailAndRestart(const char* reason) {
    queueOledDiagnostic(OLED_DIAG_OTA, "REMOTE OTA FAILED", reason ? reason : "Cause unavailable");
    drawOledDiagnostic(millis());
    delay(1800);
    clearRtcCrashTrace();
    ESP.restart();
}

// ESP32 app images carry their application descriptor at byte 32. The
// bootloader in a merged USB image is not an application and must be refused.
bool platformIsC3ApplicationHeader(const uint8_t* bytes, size_t length) {
    return bytes && length >= 36 && bytes[0] == 0xE9 &&
        bytes[1] > 0 && bytes[1] <= 16 && bytes[12] == 5 && bytes[13] == 0 &&
        bytes[32] == 0x32 && bytes[33] == 0x54 && bytes[34] == 0xCD && bytes[35] == 0xAB;
}

bool streamHttpRangeIntoUpdate(
    HTTPClient& http,
    uint32_t expectedBytes,
    uint32_t& totalWritten,
    unsigned long otaStartedMs
) {
    WiFiClient* stream = http.getStreamPtr();
    if (stream == nullptr) return false;

    uint8_t buffer[REMOTE_OTA_STREAM_BUFFER_BYTES];
    uint32_t rangeWritten = 0;
    size_t buffered = 0;
    unsigned long lastDataMs = millis();

    while (rangeWritten < expectedBytes) {
        if (millis() - otaStartedMs >= REMOTE_OTA_TOTAL_TIMEOUT_MS) return false;
        int availableNow = stream->available();

        if (availableNow > 0) {
            uint32_t remaining = expectedBytes - rangeWritten - buffered;
            size_t want = sizeof(buffer) - buffered;
            if ((uint32_t)want > remaining) want = (size_t)remaining;
            if (availableNow < (int)want) want = (size_t)availableNow;

            int got = stream->read(buffer + buffered, want);
            if (got > 0) {
                buffered += (size_t)got;
                lastDataMs = millis();
                if (totalWritten == 0) {
                    if (buffered < 36) { yield(); continue; }
                    if (!platformIsC3ApplicationHeader(buffer, buffered)) return false;
                }
                size_t flashed = Update.write(buffer, buffered);
                if (flashed != buffered) return false;

                rangeWritten += (uint32_t)buffered;
                totalWritten += (uint32_t)buffered;
                buffered = 0;
                yield();
                continue;
            }
        }

        if (!stream->connected() && stream->available() <= 0) {
            return false;
        }

        if (millis() - lastDataMs > REMOTE_OTA_IO_TIMEOUT_MS) {
            return false;
        }

        delay(1);
        yield();
    }

    return true;
}

void processRemoteHttpsOta() {
    if (mqttAdminOtaSize == 0 ||
        mqttAdminOtaMd5[0] == '\0' ||
        mqttAdminOtaUrl[0] == '\0') {
        publishMqttAdminResponse(
            "{\"ok\":false,\"cmd\":\"ota_https\",\"error\":\"missing_parameters\"}"
        );
        return;
    }

    char otaHost[128];
    uint16_t otaPort = 443;
    if (!parseHttpsHostPort(mqttAdminOtaUrl, otaHost, sizeof(otaHost), otaPort)) {
        publishMqttAdminResponse(
            "{\"ok\":false,\"cmd\":\"ota_https\",\"error\":\"invalid_https_url\"}"
        );
        return;
    }

#if PLATFORM_LOCAL_CONTROL_APPLICATION
    applicationPrepareOta();
#endif
    char accepted[180];
    snprintf(
        accepted,
        sizeof(accepted),
        "{\"ok\":true,\"cmd\":\"ota_https\",\"state\":\"starting\","
        "\"from\":\"%s\",\"size\":%lu}",
        VERSION,
        (unsigned long)mqttAdminOtaSize
    );
    publishMqttAdminResponse(accepted);
    mqttClient.loop();
    delay(REMOTE_OTA_ACK_SETTLE_MS);

    markCrashStage(CRASH_STAGE_REMOTE_OTA_PREP, ESP.getFreeHeap());

    server.stop();

    if (mqttClient.connected()) {
        mqttClient.publish(MQTT_STATUS_TOPIC, "offline", true);
        mqttClient.disconnect();
        delay(30);
    }
    mqttNet.stop();
    delay(100);
    yield();

    mqttClient.setBufferSize(128);
    delay(20);
    yield();

    uint32_t heapBefore = 0;
    uint32_t maxBlockBefore = 0;
    uint8_t fragBefore = 0;
    platformGetHeapStats(&heapBefore, &maxBlockBefore, &fragBefore);

    if (heapBefore < MQTT_TLS_MIN_HEAP_BYTES || maxBlockBefore < MQTT_TLS_MIN_BLOCK_BYTES) {
        remoteOtaFailAndRestart("heap before range OTA");
        return;
    }

    markCrashStage(CRASH_STAGE_REMOTE_OTA_TLS, heapBefore);

    WiFiClientSecure otaNet;
    otaNet.useBuiltinCACertBundle();
    otaNet.setConnectionTimeout(REMOTE_OTA_IO_TIMEOUT_MS);
    otaNet.setTimeout(REMOTE_OTA_IO_TIMEOUT_MS);
    otaNet.setHandshakeTimeout((REMOTE_OTA_IO_TIMEOUT_MS + 999UL) / 1000UL);

    HTTPClient http;
    http.setConnectTimeout(REMOTE_OTA_IO_TIMEOUT_MS);
    http.setTimeout(REMOTE_OTA_IO_TIMEOUT_MS);
    http.setReuse(true);
    http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
    http.setRedirectLimit(3);
    http.setUserAgent(String("" DEVICE_LABEL "/") + VERSION);

    markCrashStage(CRASH_STAGE_REMOTE_OTA_HTTP, ESP.getFreeHeap());

    if (!http.begin(otaNet, String(mqttAdminOtaUrl))) {
        remoteOtaFailAndRestart("HTTP begin");
        return;
    }

    String rangeValue;
    rangeValue.reserve(48);

    uint32_t offset = 0;
    uint32_t totalWritten = 0;
    bool updateStarted = false;

    drawRemoteOtaStatus("Preparing HTTPS...");
    const unsigned long otaStartedMs = millis();
    uint8_t lastDisplayedPercent = 255;

    while (offset < mqttAdminOtaSize) {
        if (millis() - otaStartedMs >= REMOTE_OTA_TOTAL_TIMEOUT_MS) {
            http.end();
            otaNet.stop();
            if (updateStarted) Update.abort();
            remoteOtaFailAndRestart("OTA total timeout");
            return;
        }
        uint32_t endOffset = offset + REMOTE_OTA_RANGE_BYTES - 1UL;
        if (endOffset >= mqttAdminOtaSize) {
            endOffset = mqttAdminOtaSize - 1UL;
        }
        uint32_t expected = endOffset - offset + 1UL;

        char rangeText[48];
        snprintf(
            rangeText,
            sizeof(rangeText),
            "bytes=%lu-%lu",
            (unsigned long)offset,
            (unsigned long)endOffset
        );
        rangeValue = rangeText;
        http.addHeader("Range", rangeValue, false, true);

        int code = http.GET();
        if (code != HTTP_CODE_PARTIAL_CONTENT) {
            http.end();
            otaNet.stop();
            if (updateStarted) Update.abort();
            remoteOtaFailAndRestart("HTTP range GET");
            return;
        }

        int contentLength = http.getSize();
        if (contentLength <= 0 || (uint32_t)contentLength != expected) {
            http.end();
            otaNet.stop();
            if (updateStarted) Update.abort();
            remoteOtaFailAndRestart("range size mismatch");
            return;
        }

        if (!updateStarted) {
            if (!Update.begin(mqttAdminOtaSize, U_FLASH)) {
                http.end();
                otaNet.stop();
                remoteOtaFailAndRestart("Update.begin");
                return;
            }
            if (!Update.setMD5(mqttAdminOtaMd5)) {
                http.end();
                otaNet.stop();
                Update.abort();
                remoteOtaFailAndRestart("invalid MD5");
                return;
            }
            updateStarted = true;
            markCrashStage(CRASH_STAGE_REMOTE_OTA_FLASH, ESP.getFreeHeap());
        }

        if (!streamHttpRangeIntoUpdate(http, expected, totalWritten, otaStartedMs)) {
            http.end();
            otaNet.stop();
            Update.abort();
            remoteOtaFailAndRestart("range stream");
            return;
        }

        offset = endOffset + 1UL;

        uint8_t percent = (uint8_t)((offset * 100UL) / mqttAdminOtaSize);
        if (percent != lastDisplayedPercent) {
            char progressLine[32];
            snprintf(
                progressLine,
                sizeof(progressLine),
                "Downloading %u%%",
                (unsigned int)percent
            );
            drawRemoteOtaStatus(progressLine);
            lastDisplayedPercent = percent;
        }
        markCrashStage(CRASH_STAGE_REMOTE_OTA_FLASH, offset);

        yield();
    }

    http.end();
    otaNet.stop();

    if (!updateStarted || totalWritten != mqttAdminOtaSize) {
        if (updateStarted) Update.abort();
        remoteOtaFailAndRestart("short range image");
        return;
    }

    if (!Update.end()) {
        remoteOtaFailAndRestart("MD5 / flash verify");
        return;
    }

    cs();
    doublePrintf(420, 1, 1, "REMOTE OTA OK");
    doublePrintf(420, 2, 1, "Verified MD5");
    doublePrintf(420, 3, 1, "Restarting...");
    clearRtcCrashTrace();
    delay(300);
    ESP.restart();
}


// ===== MQTT ADMIN COMMAND EXECUTION (OUTSIDE CALLBACK) =====
void processMqttAdminCommand() {
    if (!mqttAdminCommandPending) return;

    MqttAdminCommand command = mqttAdminCommand;

    mqttAdminCommandPending = false;
    mqttAdminCommand = MQTT_ADMIN_NONE;

    switch (command) {
        case MQTT_ADMIN_REMOTE_OTA:
            processRemoteHttpsOta();
            break;

        case MQTT_ADMIN_OTA_REJECTED: {
            char reply[128];
            snprintf(reply, sizeof(reply), "{\"ok\":false,\"cmd\":\"ota_https\",\"error\":\"%s\"}", mqttAdminOtaError);
            publishMqttAdminResponse(reply);
            break;
        }

        case MQTT_ADMIN_RESTART:
            publishMqttAdminResponse(
                "{\"ok\":true,\"cmd\":\"restart_esp\",\"state\":\"restarting\"}"
            );
            mqttAdminRestartPending = true;
            mqttAdminRestartRequestedMs = millis();
            break;

        default:
            if (applicationProcessCommand(command)) break;
            publishMqttAdminResponse(
                "{\"ok\":false,\"error\":\"invalid_command\"}"
            );
            break;
    }

    applicationResetCommand();
}

// ==========================================================
// END MODULE: ADMIN / REMOTE OTA
// ==========================================================

// ==========================================================
// MODULE: RECOVERY / WIFI / NETWORK
// ==========================================================

// =====================================================
// RESET FORENSICS / OTA-ONLY RECOVERY
//
// REASON_DEFAULT_RST (power-up) and normal software/OTA restart are NOT
// treated as faults. Hardware WDT, exception and software WDT are faults.
// ESP32-C3 EN reset is reported as power-on and does not clear recovery. It is
// reported as ESP_RST_EXT by the SDK, including some USB/DTR resets.
// A crash stays latched across power cycles.
// Recovery can be cleared by successful OTA or the
// Recovery-only "Restart normally" action, provided LittleFS deletion succeeds.
// =====================================================
const char* resetReasonLabel(uint32_t reason) {
    switch (reason) {
        case ESP_RST_POWERON: return "POWER ON / EN RESET";
        case ESP_RST_EXT: return "EXTERNAL RESET";
        case ESP_RST_SW: return "SOFTWARE / OTA";
        case ESP_RST_PANIC: return "PANIC / EXCEPTION";
        case ESP_RST_INT_WDT: return "INTERRUPT WATCHDOG";
        case ESP_RST_TASK_WDT: return "TASK WATCHDOG";
        case ESP_RST_WDT: return "WATCHDOG";
        case ESP_RST_DEEPSLEEP: return "DEEP SLEEP";
        case ESP_RST_BROWNOUT: return "BROWNOUT";
        case ESP_RST_SDIO: return "SDIO";
        case ESP_RST_USB: return "USB";
        case ESP_RST_JTAG: return "JTAG";
        case ESP_RST_EFUSE: return "EFUSE ERROR";
        case ESP_RST_PWR_GLITCH: return "POWER GLITCH";
        case ESP_RST_CPU_LOCKUP: return "CPU LOCKUP";
        case RECOVERY_REASON_FORCED_OTA: return "FORCED OTA RECOVERY";
        default: return "UNKNOWN";
    }
}

bool isFaultReset(uint32_t reason) {
    return reason == ESP_RST_PANIC || reason == ESP_RST_INT_WDT ||
           reason == ESP_RST_TASK_WDT || reason == ESP_RST_WDT ||
           reason == ESP_RST_BROWNOUT || reason == ESP_RST_EFUSE ||
           reason == ESP_RST_PWR_GLITCH || reason == ESP_RST_CPU_LOCKUP;
}

uint32_t recoveryChecksum() {
    const RecoveryRecord& rec = recoveryIncident;
    return rec.magic ^ rec.reason ^ rec.exccause ^ rec.epc1 ^
           rec.epc2 ^ rec.epc3 ^ rec.excvaddr ^ rec.depc ^ 0xA25E2F00UL;
}

void captureCurrentReset() {
    currentBootReset = RecoveryRecord();
    currentBootReset.magic = RECOVERY_MAGIC;
    currentBootReset.reason = (uint32_t)esp_reset_reason();
    // ESP-IDF reset_reason does not provide the Xtensa register snapshot.
    // ESP32-C3 is RISC-V; unavailable register fields remain explicitly zero.
}


bool loadRecoveryIncident() {
    if (!littleFsReady || !LittleFS.exists(RECOVERY_FILE)) return false;
    File file = LittleFS.open(RECOVERY_FILE, "r");
    if (!file) return false;
    RecoveryRecord rec = {};
    bool valid = file.size() == (size_t)sizeof(rec) &&
                 file.readBytes((char*)&rec, sizeof(rec)) == sizeof(rec);
    file.close();
    if (!valid) return false;
    recoveryIncident = rec;
    // Accept an external-reset incident written by the initial v2.0
    // diagnostic build as a legacy latched record. NEW external RST events
    // clear the latch instead of ever creating another one.
    bool recognizedSavedReason = isFaultReset(recoveryIncident.reason) ||
                                  recoveryIncident.reason == ESP_RST_EXT ||
                                  recoveryIncident.reason == RECOVERY_REASON_FORCED_OTA;
    if (recoveryIncident.magic != RECOVERY_MAGIC ||
        recoveryIncident.checksum != recoveryChecksum() ||
        !recognizedSavedReason) {
        recoveryIncident = RecoveryRecord();
        return false;
    }
    recoveryIncidentPersistent = true;
    return true;
}

bool saveRecoveryIncident() {
    if (!littleFsReady) return false;
    recoveryIncident.magic = RECOVERY_MAGIC;
    recoveryIncident.checksum = recoveryChecksum();
    File file = LittleFS.open(RECOVERY_FILE, "w");
    if (!file) return false;
    bool ok = file.write((const uint8_t*)&recoveryIncident,
                         sizeof(recoveryIncident)) == sizeof(recoveryIncident);
    file.close();
    recoveryIncidentPersistent = ok;
    return ok;
}

void showRecoveryScreen() {
    if (drawOledDiagnostic(millis())) return;
    cs();
    doublePrintf(420, 1, 1, "RECOVERY / Ver %s", VERSION);
    doublePrintf(420, 2, 1, "%s (%lu)",
                 resetReasonLabel(recoveryIncident.reason),
                 (unsigned long)recoveryIncident.reason);
    if (WiFi.status() == WL_CONNECTED) {
        doublePrintf(420, 3, 1, "IP: %s", WiFi.localIP().toString().c_str());
    } else if (credentialPortalActive) {
        doublePrintf(420, 3, 1, "AP: %s", WiFi.softAPIP().toString().c_str());
    } else {
        doublePrintf(420, 3, 1, "WiFi reconnecting");
    }
    doublePrintf(420, 4, 1, "WEB EXIT / OTA");
}

String buildRecoveryReport() {
    String d;
    d.reserve(1700);
    d += "" DEVICE_LABEL " OTA-ONLY RECOVERY / Ver " + String(VERSION) + "\n";
    d += "Measurements, MQTT, admin commands and statistics: DISABLED\n";
    d += "Firmware=" + String(VERSION) + " build=" + String(BUILD_DATE) + "\n";
    d += "Meter=" + String(DEVICE_KIND) + " OLED=" + String(OLED_TYPE) + "\n";
    d += "Incident reason=" + String(resetReasonLabel(recoveryIncident.reason)) +
         " (" + String(recoveryIncident.reason) + ")\n";
    d += "Incident stored in LittleFS=" +
         String(recoveryIncidentPersistent ? "YES" : "NO (current boot only)") + "\n";
    d += "RTC crash breadcrumb=";
    if (rtcTraceBeforeResetValid) {
        d += String(crashStageLabel(rtcTraceBeforeReset.stage)) +
             " (" + String(rtcTraceBeforeReset.stage) + ")" +
             " detail=" + String(rtcTraceBeforeReset.detail) + "\n";
        d += "RTC trace heap free bytes=" + String(rtcTraceBeforeReset.heapFree) + "\n";
        d += "RTC trace max free block bytes=" + String(rtcTraceBeforeReset.maxFreeBlock) + "\n";
        d += "RTC trace heap fragmentation percent=" +
             String(rtcTraceBeforeReset.heapFragmentation) + "\n";
    } else {
        d += "UNAVAILABLE\n";
    }
    d += "RTC runtime breadcrumb events (previous reset): " +
         String(runtimeBeforeResetCount) + "\n";
    for(uint8_t i=0;i<runtimeBeforeResetCount;++i) {
        const RuntimeBreadcrumbEntry& b=runtimeBeforeReset[i];
        d += "Runtime #" + String(b.sequence) + " @" + String(b.uptimeMs) +
             "ms " + runtimeBreadcrumbLabel(b.code) +
             " detail=" + String(b.detail) + "\n";
    }
    d += "Last runtime event is a clue, not proof of watchdog cause.\n";
    d += "Recovery record deletion failed=" +
         String(recoveryResetClearFailed ? "YES" : "NO") + "\n";
    d += "Recovery exit: successful OTA or /restart-normal; EN reset retains the latch\n";
    d += "ESP-IDF provides reset reason only; register snapshot unavailable on ESP32-C3.\n";
    d += "Legacy exception field=" + String(recoveryIncident.exccause) + "\n";
    char address[20];
    snprintf(address, sizeof(address), "0x%08lX", (unsigned long)recoveryIncident.epc1);
    d += "RISC-V registers: unavailable from reset API; the following legacy fields are unset\n";
    d += "EPC1=" + String(address) + "\n";
    snprintf(address, sizeof(address), "0x%08lX", (unsigned long)recoveryIncident.epc2);
    d += "EPC2=" + String(address) + "\n";
    snprintf(address, sizeof(address), "0x%08lX", (unsigned long)recoveryIncident.epc3);
    d += "EPC3=" + String(address) + "\n";
    snprintf(address, sizeof(address), "0x%08lX", (unsigned long)recoveryIncident.excvaddr);
    d += "EXCVADDR=" + String(address) + "\n";
    snprintf(address, sizeof(address), "0x%08lX", (unsigned long)recoveryIncident.depc);
    d += "DEPC=" + String(address) + "\n";
    d += "Current boot reason=" + String(resetReasonLabel(currentBootReset.reason)) +
         " (" + String(currentBootReset.reason) + ")\n";
    d += "ESP reset information=" + String(resetReasonLabel((uint32_t)esp_reset_reason())) + "\n";
    d += "Chip ID=" + String(platformChipId(), HEX) + "\n";
    d += "CPU MHz=" + String(ESP.getCpuFreqMHz()) + "\n";
    d += "Core version=" + String(ESP.getCoreVersion()) + "\n";
    d += "SDK version=" + String(ESP.getSdkVersion()) + "\n";
    d += "Uptime seconds=" + String(millis()/1000UL) + "\n";
    d += "Free heap bytes=" + String(ESP.getFreeHeap()) + "\n";
#if defined(UMM_INFO)
    d += "Maximum free block bytes=" + String(platformMaxFreeBlock()) + "\n";
    d += "Heap fragmentation percent=" + String(platformHeapFragmentation()) + "\n";
#endif
    d += "Free contiguous stack bytes=" + String(platformFreeStack()) + "\n";
    d += "Free sketch space bytes=" + String(ESP.getFreeSketchSpace()) + "\n";
    d += "Flash configured bytes=" + String(ESP.getFlashChipSize()) + "\n";
    d += "Flash actual bytes=" + String(ESP.getFlashChipSize()) + "\n";
    d += "Wi-Fi status=" + String((int)WiFi.status()) + "\n";
    d += "Static local IP=" + WiFi.localIP().toString() + "\n";
    d += "Gateway=" + WiFi.gatewayIP().toString() + "\n";
    d += "Subnet=" + WiFi.subnetMask().toString() + "\n";
    d += "DNS=" + WiFi.dnsIP().toString() + "\n";
    if (WiFi.status() == WL_CONNECTED) {
        d += "Wi-Fi RSSI dBm=" + String(WiFi.RSSI()) + "\n";
        d += "Web root requests=" + String(webRootRequestCount) + "\n";
        d += "Web rapid reloads retried=" + String(webRootRateLimitedCount) + "\n";
        d += "Web memory busy=" + String(webRootBusyCount) + "\n";
        d += "Web write aborts=" + String(webRootWriteAbortCount) + "\n";
        d += "Web sockets explicitly closed=" + String(webRootForcedCloseCount) + "\n";
        d += "Wi-Fi channel=" + String(WiFi.channel()) + "\n";
    }
    d += "Recovery OTA active=" + String(recoveryOtaInProgress ? "YES" : "WAITING") + "\n";
    d += "OTA recovery clear failed=" + String(recoveryOtaClearFailed ? "YES" : "NO") + "\n";
    d += "AP attempts=" + String(credentialApAttempts) + " last failure=" +
         String(credentialApLastFailure) + " (1=radio,2=IP,3=start,4=PHY,5=readback,6=lost), heap=" +
         String(credentialApHeapAtAttempt) + "\n";
    d += "AP diagnostic snapshot verified=" + String(credentialApStateVerified ? "YES" : "NO") +
         " stage=" + String(credentialApLastCheckStage) + " native error=" +
         String((long)credentialApLastNativeError) + " channel=" + String(credentialApChannel) +
         " protocol=" + String(credentialApProtocol) + " bandwidth=" + String(credentialApBandwidth) + "\n";
    d += "SDK configuration does not prove RF beacon reception.\n";
    d += "AP loss samples=" + String(credentialApLossChecks) + "/" +
         String(CREDENTIAL_AP_LOSS_CONFIRM_CHECKS) + " BOOT requests=" +
         String(credentialApBootRequests) + " last action=" + credentialApLastAction + "\n";
    d += "Configuration diagnostics never power off the setup AP.\n";
    d += "OLED failure hold ms=" + String(OLED_DIAGNOSTIC_HOLD_MS) +
         " pending=" + String(oledDiagnosticCount) + "\n";
    if (oledDiagnosticCount) {
        d += "OLED diagnostic=" + String(oledDiagnostics[0].title) + "\n";
        d += "OLED cause=" + String(oledDiagnostics[0].detail) + "\n";
    }
    d += "Only Recovery HTTP endpoints are available: /, /debug, /debug.txt, /restart-normal.\n";
    d += "Successful OTA or Restart normally clears the recovery incident before reboot.\n";
    d += "If clearing the incident fails, Recovery remains latched. Exit using Restart normally or a successful OTA.\n";
    d += "No passwords are shown in this diagnostic report.\n";
    return d;
}

void handleRecoveryText() {
    WebResponseScope responseScope;
    if (!requireApWebPin()) return;
    server.sendHeader("Cache-Control", "no-store");
    server.send(200, "text/plain; charset=utf-8", buildRecoveryReport());
}

void handleRecoveryPage() {
    WebResponseScope responseScope;
    if (!requireApWebPin()) return;
    String report = buildRecoveryReport();
    String html;
    html.reserve(report.length() + 650);
    html += F("<!doctype html><html><head><meta charset='utf-8'>"
              "<meta name='viewport' content='width=device-width,initial-scale=1'>"
              "<meta http-equiv='refresh' content='10'>"
              "<title>" DEVICE_LABEL " RECOVERY</title>"
              "<style>body{font:22px/1.35 Arial,sans-serif;margin:15px auto;padding:12px;"
              "max-width:900px}pre{white-space:pre-wrap;overflow-wrap:anywhere;"
              "border:1px solid #bbb;padding:12px;background:#f5f5f5}"
              "h1{color:#b00020}</style><style>body{font:22px/1.35 Arial,sans-serif;max-width:760px;margin:auto;padding:10px;color:#142431}h1,h2,small{font-size:inherit}pre{font:inherit;white-space:pre-wrap;overflow-wrap:anywhere}input,button{font:inherit}</style></head><body>"
              "<h1>OTA-ONLY RECOVERY</h1>"
              "<p>Normal measurement mode is locked. IDE_OTA remains available on LAN. "
              "A successful OTA clears Recovery and the new firmware starts normally after reboot. "
              "Exit using Restart normally or a successful OTA.</p>"
              "<p><a href='/restart-normal' "
              "onclick=\"return confirm('Clear the recovery incident and restart normal operation?');\">"
              "<b>Restart normally</b></a></p>"
              "<a href='/debug.txt'>Copy/download plain-text diagnostics</a><pre>");
    for (size_t i=0; i<report.length(); ++i) {
        char ch = report[i];
        if (ch == '&') html += F("&amp;");
        else if (ch == '<') html += F("&lt;");
        else if (ch == '>') html += F("&gt;");
        else html += ch;
    }
    html += F("</pre></body></html>");
    server.sendHeader("Cache-Control", "no-store");
    server.send(200, "text/html; charset=utf-8", html);
}

void handleRecoveryRestartNormal() {
    WebResponseScope responseScope;
    if (!requireApWebPin()) return;
    // Fail closed: normal mode is allowed only after the persistent recovery
    // latch has definitely been removed from LittleFS.
    if (!littleFsReady) {
        queueOledDiagnostic(OLED_DIAG_RECOVERY_CLEAR, "RECOVERY CLEAR FAILED", "LittleFS unavailable; Recovery remains latched");
        server.sendHeader("Cache-Control", "no-store");
        server.send(503, "text/plain; charset=utf-8",
                    "Restart refused: LittleFS unavailable; Recovery remains latched.");
        return;
    }

    bool clearSucceeded = true;
    if (LittleFS.exists(RECOVERY_FILE)) {
        clearSucceeded = LittleFS.remove(RECOVERY_FILE);
    }

    if (!clearSucceeded) {
        recoveryResetClearFailed = true;
        queueOledDiagnostic(OLED_DIAG_RECOVERY_CLEAR, "RECOVERY CLEAR FAILED", "Recovery file removal failed; restart refused");
        server.sendHeader("Cache-Control", "no-store");
        server.send(500, "text/plain; charset=utf-8",
                    "Restart refused: recovery incident could not be cleared.");
        return;
    }

    recoveryIncident = RecoveryRecord();
    recoveryIncidentPersistent = false;

    server.sendHeader("Cache-Control", "no-store");
    server.send_P(
        200,
        PSTR("text/html; charset=utf-8"),
        PSTR("<!doctype html><html><head><meta charset='utf-8'>"
             "<meta name='viewport' content='width=device-width,initial-scale=1'>"
             "<title>Restarting</title><style>body{font:22px/1.35 Arial,sans-serif;max-width:760px;margin:auto;padding:10px;color:#142431}h1,h2,small{font-size:inherit}pre{font:inherit;white-space:pre-wrap;overflow-wrap:anywhere}input,button{font:inherit}</style></head><body>"
             "<h2>Recovery cleared</h2>"
             "<p>Restarting in normal measurement mode...</p>"
             "</body></html>")
    );

    recoveryNormalRestartPending = true;
    recoveryNormalRestartRequestedMs = millis();
}


bool writeCredentialHtmlEscaped(WiFiClient& client, const char* value) {
    if (!value) return true;
    for (const char* p = value; *p; ++p) {
        const char* entity = nullptr;
        size_t entityLen = 0;
        switch (*p) {
            case '&': entity = "&amp;"; entityLen = 5; break;
            case '<': entity = "&lt;"; entityLen = 4; break;
            case '>': entity = "&gt;"; entityLen = 4; break;
            case '"': entity = "&quot;"; entityLen = 6; break;
            case '\'': entity = "&#39;"; entityLen = 5; break;
            default:
                if (!writeRootBytes(client, reinterpret_cast<const uint8_t*>(p), 1)) return false;
                continue;
        }
        if (!writeRootBytes(client, reinterpret_cast<const uint8_t*>(entity), entityLen)) {
            return false;
        }
    }
    return true;
}

bool writeCredentialProgmem(WiFiClient& client, PGM_P text) {
    const size_t len = strlen_P(text);
    return writeRootProgmemViaRam(client, text, len);
}

bool writeWifiFallbackNotice(WiFiClient& client) {
    if (!wifiFallbackActive) return true;
    if (!writeCredentialProgmem(client, PSTR(
        "<div class='card' role='alert'><h2>Δεν μπορώ να συνδεθώ στο Wi-Fi</h2>"
        "<p>Απέτυχαν 3 προσπάθειες σύνδεσης. Το AP λειτουργεί παράλληλα με τις επανασυνδέσεις και θα κλείσει αυτόματα όταν επιστρέψει ο router.</p><p>Αιτία: <b>"))) return false;
    if (!writeCredentialProgmem(client, wifiFailureExplanation())) return false;
    char detail[128];
    int n = snprintf(detail, sizeof(detail),
        "</b></p><p>SDK reason: %u · Wi-Fi status: %d · Failed attempts: 3/3</p></div>",
        (unsigned)wifiFallbackReason, wifiFallbackStatus);
    return n > 0 && n < (int)sizeof(detail) &&
        writeRootBytes(client, reinterpret_cast<const uint8_t*>(detail), (size_t)n);
}

void handleCredentialSetupPage() {
    WebResponseScope responseScope;
    if (!requireApWebPin()) return;
    uint32_t freeHeap = 0;
    uint32_t maxBlock = 0;
    uint8_t fragmentation = 0;
    platformGetHeapStats(&freeHeap, &maxBlock, &fragmentation);
    markCrashStage(CRASH_STAGE_WEB_SETUP, freeHeap);

    // This page is intentionally streamed in small pieces. Do not rebuild it
    // into a String: MQTT/TLS can leave only a few KB of heap on ESP32-C3.
    if (freeHeap < WEB_ROOT_MIN_HEAP_BYTES || maxBlock < WEB_ROOT_MIN_BLOCK_BYTES) {
        WiFiClient client = server.client();
        sendRootBusyAndClose(client,503,freeHeap,maxBlock);
        return;
    }

    DeviceCredentialRecord stored = {};
    bool haveStored = littleFsReady && readDeviceCredentialRecord(stored);

    WiFiClient client = server.client();
    client.setTimeout(1500);

    static const char header[] =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/html; charset=utf-8\r\n"
        "Cache-Control: no-store\r\n"
        "Connection: close\r\n\r\n";
    if (!writeRootBytes(client,reinterpret_cast<const uint8_t*>(header),sizeof(header)-1)) return;

    if (!writeCredentialProgmem(client, PSTR(
        "<!doctype html><html><head><meta charset='utf-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>" DEVICE_LABEL " Setup</title><style>"
        "body{font:22px/1.35 Arial,sans-serif;max-width:760px;margin:auto;padding:18px;color:#142431}"
        "input{font:inherit;width:100%;box-sizing:border-box;padding:10px;margin:5px 0 12px}"
        "h1,h2,small{font-size:inherit}button{font:inherit;padding:11px 16px;margin:6px 4px 6px 0}"
        ".card{border:1px solid #dbe3e8;border-radius:12px;padding:14px;margin:12px 0}"
        "</style><style>body{font:22px/1.35 Arial,sans-serif;max-width:760px;margin:auto;padding:10px;color:#142431}h1,h2,small{font-size:inherit}pre{font:inherit;white-space:pre-wrap;overflow-wrap:anywhere}input,button{font:inherit}</style></head><body><h2>" DEVICE_LABEL " v" VERSION " setup</h2>"))) return;
    if (!writeWifiFallbackNotice(client)) return;
    if (credentialPortalActive) {
        const String apIpText = WiFi.softAPIP().toString();
        if (!writeCredentialProgmem(client, PSTR("<p>Setup AP IP: <b>")) ||
            !writeCredentialHtmlEscaped(client, apIpText.c_str()) ||
            !writeCredentialProgmem(client, PSTR("</b></p>"))) return;
    }
#if PLATFORM_REQUIRE_ROUTER_BEFORE_APPLICATION
    if (!writeCredentialProgmem(client, PSTR(
        "<p>Πρώτα ρυθμίστε και συνδέστε το Wi-Fi του router. Η οθόνη και οι αισθητήρες ξεκινούν μόνο μετά τη σύνδεση. Δεν απαιτείται σύνδεση MQTT.</p>"))) return;
#endif
    if (!writeCredentialProgmem(client, PSTR("<div class='card'><p>Stored credentials: <b>"))) return;

    if (!writeCredentialProgmem(client, haveStored ? PSTR("YES") : PSTR("NO"))) return;

    if (!writeCredentialProgmem(client, PSTR(
        "</b></p><form method='post' action='/setup-save'>"
        "<label>Wi-Fi SSID</label><input name='ssid' maxlength='32' value='"))) return;
    if (haveStored && !writeCredentialHtmlEscaped(client, stored.ssid)) return;

    if (!writeCredentialProgmem(client, PSTR(
        "' required><label>Wi-Fi password</label>"
        "<input name='wifi_password' type='password' maxlength='64' placeholder='"))) return;
    if (!writeCredentialProgmem(client,
        haveStored ? PSTR("leave blank to keep current") : PSTR("required"))) return;

    if (!writeCredentialProgmem(client, PSTR(
        "'><label>Σταθερή IP στον router: 192.168.1.XX</label>"
        "<input name='sta_octet' type='number' min='2' max='254' required value='"))) return;
    char octetText[4];
    snprintf(octetText, sizeof(octetText), "%u", (unsigned)(haveStored ? stored.staOctet : SITE_IP_D));
    if (!writeCredentialHtmlEscaped(client, octetText)) return;
    if (!writeCredentialProgmem(client, PSTR(
        "'><small>Προεπιλογή εφαρμογής: 192.168.1." PLATFORM_STRINGIFY(SITE_IP_D)
        ". Το AP ανοίγει στο 192.168.2.XX (XX = LAN IP).</small>"
        "<label>MQTT username</label><input name='mqtt_user' maxlength='64' value='"))) return;
    if (haveStored && !writeCredentialHtmlEscaped(client, stored.mqttUser)) return;

    if (!writeCredentialProgmem(client, PSTR(
        "'><label>MQTT password</label>"
        "<input name='mqtt_password' type='password' maxlength='64' placeholder='"))) return;
    if (!writeCredentialProgmem(client,
        haveStored ? PSTR("leave blank to keep current") : PSTR("required"))) return;

    writeCredentialProgmem(client, PSTR(
        "'><button type='submit'>Save &amp; restart</button></form>"
        "<p><a href='/network-debug'>Network debug</a> · <a href='/restart-esp'>Restart ESP</a> · <a href='/'>Back</a></p></div>"
        "<div class='card'><form method='get' action='/forget-credentials-confirm'>"
        "<button type='submit'>Clear stored credentials...</button></form>"
        "<small>Saved passwords are kept when left blank. MQTT may be left incomplete: router Wi-Fi and this page remain available.</small>"
        "<small>This action requires a second confirmation page. Passwords are never displayed.</small></div></body></html>"));
}

void handleCredentialSave() {
    WebResponseScope responseScope;
    if (!requireApWebPin()) return;
    if (!littleFsReady) {
        server.send(503, "text/plain; charset=utf-8", "LittleFS unavailable");
        return;
    }

    DeviceCredentialRecord oldRec = {};
    bool haveOld = readDeviceCredentialRecord(oldRec);

    String newSsid = server.arg("ssid");
    String newWifiPassword = server.arg("wifi_password");
    String newHost = MQTT_BROKER_HOST;
    String newUser = server.arg("mqtt_user");
    String newMqttPassword = server.arg("mqtt_password");
    String newStaOctet = server.arg("sta_octet");
    newSsid.trim(); newUser.trim();
    newStaOctet.trim();
    uint8_t selectedOctet = haveOld ? oldRec.staOctet : SITE_IP_D;
    if (newStaOctet.length() && !parseStaOctet(newStaOctet.c_str(), &selectedOctet)) {
        server.send(400, "text/plain; charset=utf-8", "Invalid IP: enter 2-254 for 192.168.1.XX (.1 is the router)");
        return;
    }

    if (newWifiPassword.length() == 0 && haveOld) newWifiPassword = oldRec.wifiPassword;
    if (newMqttPassword.length() == 0 && haveOld) newMqttPassword = oldRec.mqttPassword;

    const uint16_t portValue = MQTT_BROKER_PORT;
    if (newSsid.length() == 0 || newSsid.length() >= sizeof(oldRec.ssid) ||
        newWifiPassword.length() == 0 || newWifiPassword.length() >= sizeof(oldRec.wifiPassword) ||
        newHost.length() == 0 || newHost.length() >= sizeof(oldRec.mqttHost) ||
        newUser.length() >= sizeof(oldRec.mqttUser) ||
        newMqttPassword.length() >= sizeof(oldRec.mqttPassword) ||
        portValue < 1 || portValue > 65535) {
        server.send(400, "text/plain; charset=utf-8", "Invalid credential fields");
        return;
    }

    DeviceCredentialRecord rec = {};
    rec.magic = DEVICE_CREDENTIAL_MAGIC;
    rec.version = DEVICE_CREDENTIAL_VERSION;
    rec.mqttPort = (uint16_t)portValue;
    rec.staOctet = selectedOctet;
    if (!copyCredentialField(rec.ssid, sizeof(rec.ssid), newSsid.c_str()) ||
        !copyCredentialField(rec.wifiPassword, sizeof(rec.wifiPassword), newWifiPassword.c_str()) ||
        !copyCredentialField(rec.mqttHost, sizeof(rec.mqttHost), newHost.c_str()) ||
        !copyOptionalCredentialField(rec.mqttUser, sizeof(rec.mqttUser), newUser.c_str()) ||
        !copyOptionalCredentialField(rec.mqttPassword, sizeof(rec.mqttPassword), newMqttPassword.c_str())) {
        server.send(400, "text/plain; charset=utf-8", "Credential field too long");
        return;
    }
    rec.checksum = deviceCredentialChecksum(rec);

    if (!writeDeviceCredentialRecord(rec)) {
        server.send(500, "text/plain; charset=utf-8", "Could not save credentials");
        return;
    }

    server.sendHeader("Cache-Control", "no-store");
    char reply[768];
    snprintf(reply, sizeof(reply),
        "<!doctype html><html><body><h2>Saved</h2>"
        "<p>Saved and read-back verified. Restarting with the stored Wi-Fi settings.</p>"
        "<p>After connecting to the router, open <a href='http://192.168.1.%u/'>http://192.168.1.%u/</a>.</p>"
        "<p>After 3 failed Wi-Fi attempts, the temporary AP opens at http://192.168.2.%u/ with ongoing Wi-Fi retries.</p>"
        "<p>MQTT rejection will not erase settings or disable router Wi-Fi.</p></body></html>",
        (unsigned)selectedOctet, (unsigned)selectedOctet, (unsigned)selectedOctet);
    server.send(200, "text/html; charset=utf-8", reply);
    credentialRestartPending = true;
    credentialRestartRequestedMs = millis();
}

void handleForgetCredentialsConfirm() {
    WebResponseScope responseScope;
    if (!requireApWebPin()) return;
    server.sendHeader("Cache-Control", "no-store");
    server.send(200, "text/html; charset=utf-8",
                "<!doctype html><html><head><meta charset='utf-8'>"
                "<meta name='viewport' content='width=device-width,initial-scale=1'>"
                "<title>Confirm credential clear</title><style>body{font:22px/1.35 Arial,sans-serif;max-width:760px;margin:auto;padding:10px;color:#142431}h1,h2,small{font-size:inherit}pre{font:inherit;white-space:pre-wrap;overflow-wrap:anywhere}input,button{font:inherit}</style></head><body>"
                "<h2>Confirm credential clear</h2>"
                "<p><b>Warning:</b> this will erase the stored Wi-Fi and MQTT credentials.</p>"
                "<p>After clearing, the ESP32-C3 will restart into Setup AP mode.</p>"
                "<form method='post' action='/forget-credentials'>"
                "<label><input type='checkbox' name='confirm' value='ERASE' required> "
                "I understand and want to erase the stored credentials.</label><br><br>"
                "<button type='submit'>YES - ERASE CREDENTIALS</button>"
                "</form>"
                "<p><a href='/setup'>Cancel and go back</a></p>"
                "</body></html>");
}

void handleForgetCredentials() {
    WebResponseScope responseScope;
    if (!requireApWebPin()) return;
    if (server.arg("confirm") != "ERASE") {
        server.sendHeader("Cache-Control", "no-store");
        server.send(400, "text/html; charset=utf-8",
                    "<!doctype html><html><body><h2>Confirmation required</h2>"
                    "<p>Credentials were NOT erased.</p>"
                    "<p><a href='/forget-credentials-confirm'>Open confirmation page</a></p>"
                    "</body></html>");
        return;
    }

    if (!littleFsReady) {
        server.send(503, "text/plain; charset=utf-8", "LittleFS unavailable");
        return;
    }
    bool ok = true;
#if PLATFORM_LOCAL_CONTROL_APPLICATION
    if (!applicationClearLegacyCredentials()) {
        server.send(500, "text/plain; charset=utf-8", "Could not remove legacy credentials");
        return;
    }
#endif
    if (LittleFS.exists(DEVICE_CREDENTIAL_FILE)) ok &= LittleFS.remove(DEVICE_CREDENTIAL_FILE);
    if (LittleFS.exists(DEVICE_CREDENTIAL_TEMP_FILE)) ok &= LittleFS.remove(DEVICE_CREDENTIAL_TEMP_FILE);
    if (!ok) {
        server.send(500, "text/plain; charset=utf-8", "Could not remove credentials");
        return;
    }
    server.sendHeader("Cache-Control", "no-store");
    server.send(200, "text/html; charset=utf-8",
                "<!doctype html><html><body><h2>Credentials cleared</h2>"
                "<p>Restarting into dedicated Setup AP mode...</p>"
                "<p>The OLED will show the open AP name, web PIN and setup address.</p>"
                "</body></html>");
    credentialRestartPending = true;
    credentialRestartRequestedMs = millis();
}

void handleForceOtaRecoveryConfirm() {
    WebResponseScope responseScope;
    if (!requireApWebPin()) return;
    server.sendHeader("Cache-Control", "no-store");
    server.send(200, "text/html; charset=utf-8",
                "<!doctype html><html><head><meta charset='utf-8'>"
                "<meta name='viewport' content='width=device-width,initial-scale=1'>"
                "<title>Force OTA Recovery</title><style>body{font:22px/1.35 Arial,sans-serif;max-width:760px;margin:auto;padding:10px;color:#142431}h1,h2,small{font-size:inherit}pre{font:inherit;white-space:pre-wrap;overflow-wrap:anywhere}input,button{font:inherit}</style></head><body>"
                "<h2>Force OTA-ONLY Recovery</h2>"
                "<p>This restarts the ESP32-C3 into recovery mode. Measurements and MQTT will be disabled until OTA succeeds or Recovery is exited.</p>"
                "<form method='post' action='/force-ota-recovery'>"
                "<label><input type='checkbox' name='confirm' value='ENTER' required> "
                "Enter OTA-ONLY Recovery after restart.</label><br>"
                "<a href='/restart-esp'>Restart ESP (normal reboot)</a><br><br>"
                "<button type='submit'>ENTER OTA-ONLY RECOVERY</button></form>"
                "<p><a href='/'>Cancel</a></p></body></html>");
}

// Explicit GET confirmation followed by POST: a plain GET link may not reboot
// hardware (e.g. link previews, browsers and network scanners).
void handleRestartEspConfirm() {
    WebResponseScope responseScope;
    if (!requireApWebPin()) return;
    if (recoveryMode) {
        server.send(409, "text/plain; charset=utf-8",
                    "ESP is in Recovery; use /restart-normal instead.");
        return;
    }
    server.sendHeader("Cache-Control", "no-store");
    server.send(200, "text/html; charset=utf-8",
                "<!doctype html><html><head><meta charset='utf-8'>"
                "<meta name='viewport' content='width=device-width,initial-scale=1'>"
                "<title>Restart ESP</title></head><body>"
                "<h2>Restart ESP32-C3</h2>"
                "<p>Normal restart only. Wi-Fi/MQTT settings are preserved. "
                "This does NOT enter OTA-ONLY Recovery.</p>"
                "<form method='post' action='/restart-esp'>"
                "<button type='submit'>CONFIRM ESP RESTART</button></form>"
                "<p><a href='/force-ota-recovery-confirm'>Cancel</a></p>"
                "</body></html>");
}
void handleRestartEsp() {
    WebResponseScope responseScope;
    if (!requireApWebPin()) return;
    if (recoveryMode || forceRecoveryRestartPending || mqttAdminRestartPending) {
        server.send(409, "text/plain; charset=utf-8",
                    "Restart unavailable during Recovery or a pending restart.");
        return;
    }
    server.sendHeader("Cache-Control", "no-store");
    server.send(200, "text/plain; charset=utf-8", "ESP32-C3 restarting normally.");
    // Reuse the existing deferred, network-friendly restart path in loop().
    mqttAdminRestartPending = true;
    mqttAdminRestartRequestedMs = millis();
}

void handleForceOtaRecovery() {
    WebResponseScope responseScope;
    if (!requireApWebPin()) return;
    if (server.arg("confirm") != "ENTER") {
        server.send(400, "text/plain; charset=utf-8", "Confirmation required");
        return;
    }
    if (!littleFsReady) {
        server.send(503, "text/plain; charset=utf-8", "LittleFS unavailable");
        return;
    }

    recoveryIncident = RecoveryRecord();
    recoveryIncident.reason = RECOVERY_REASON_FORCED_OTA;
    if (!saveRecoveryIncident() || !loadRecoveryIncident()) {
        server.send(500, "text/plain; charset=utf-8", "Could not persist Recovery request");
        return;
    }

    clearRtcCrashTrace();
    server.sendHeader("Cache-Control", "no-store");
    server.send(200, "text/html; charset=utf-8",
                "<!doctype html><html><body><h2>OTA-ONLY Recovery requested</h2>"
                "<p>Restarting. IDE_OTA and mDNS will be enabled in Recovery.</p>"
                "</body></html>");
    forceRecoveryRestartPending = true;
#if PLATFORM_LOCAL_CONTROL_APPLICATION
    applicationPrepareOta();
#endif
    forceRecoveryRestartRequestedMs = millis();
}

void handleNetworkDebugPage() {
    WebResponseScope responseScope;
    if (!requireApWebPin()) return;
    uint32_t freeHeap = 0;
    uint32_t maxBlock = 0;
    uint8_t fragmentation = 0;
    platformGetHeapStats(&freeHeap, &maxBlock, &fragmentation);
    markCrashStage(CRASH_STAGE_WEB_NET_DEBUG, freeHeap);

    if (freeHeap < WEB_ROOT_MIN_HEAP_BYTES || maxBlock < WEB_ROOT_MIN_BLOCK_BYTES) {
        WiFiClient client = server.client();
        sendRootBusyAndClose(client,503,freeHeap,maxBlock);
        return;
    }

    DeviceCredentialRecord stored = {};
    bool haveStored = littleFsReady && readDeviceCredentialRecord(stored);

    IPAddress resolved(0, 0, 0, 0);
    int dnsResult = 0;
    const bool dnsAttempted = mqttHost[0] != '\0' && WiFi.status() == WL_CONNECTED;
    if (dnsAttempted) {
        dnsResult = platformResolveHost(mqttHost, resolved, MQTT_DNS_TIMEOUT_MS);
    }

    const bool resolvedAllZero =
        resolved[0] == 0 && resolved[1] == 0 &&
        resolved[2] == 0 && resolved[3] == 0;
    const bool resolvedBroadcast =
        resolved[0] == 255 && resolved[1] == 255 &&
        resolved[2] == 255 && resolved[3] == 255;
    const bool dnsAddressUsable =
        dnsResult == 1 && !resolvedAllZero && !resolvedBroadcast;

    WiFiClient client = server.client();
    client.setTimeout(1500);

    static const char header[] =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/html; charset=utf-8\r\n"
        "Cache-Control: no-store\r\n"
        "Connection: close\r\n\r\n";
    if (!writeRootBytes(client,reinterpret_cast<const uint8_t*>(header),sizeof(header)-1)) return;

    if (!writeCredentialProgmem(client, PSTR(
        "<!doctype html><html><head><meta charset='utf-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>Network debug</title><style>body{font:22px/1.35 Arial,sans-serif;max-width:760px;margin:auto;padding:10px;color:#142431}h1,h2,small{font-size:inherit}pre{font:inherit;white-space:pre-wrap;overflow-wrap:anywhere}input,button{font:inherit}</style></head><body><h2>Network debug</h2><pre>"
        "Firmware: " VERSION "\n"
        "Build: " BUILD_DATE "\n"
        "Device: " DEVICE_ID "\n"
        "Meter: " DEVICE_KIND "\n"
    ))) return;

    char line[256];
    int n = snprintf_P(line, sizeof(line), PSTR("MQTT unique device ID: %s\n"), mqttDeviceId);
    if (n <= 0 || n >= (int)sizeof(line)) return;
    if (!writeRootBytes(client,reinterpret_cast<const uint8_t*>(line),(size_t)n)) return;
    n = snprintf_P(line, sizeof(line), PSTR("OLED type: %u\n"
        "Uptime seconds: %lu\n"
        "Recovery mode: %s\n"
        "Credential portal active: %s\n"
        "Credential portal mode: %s\n"
        "IDE_OTA policy: RECOVERY ONLY\n"
        "Recovery mDNS discovery: ENABLED\n"),
        (unsigned)OLED_TYPE,
        (unsigned long)(millis() / 1000UL),
        recoveryMode ? "YES" : "NO",
        credentialPortalActive ? "YES" : "NO",
        credentialPortalActive
            ? (credentialPortalOnly ? "AP-ONLY" : "AP+STA")
            : "OFF");
    if (n <= 0 || n >= (int)sizeof(line)) return;
    if (!writeRootBytes(client,reinterpret_cast<const uint8_t*>(line),(size_t)n)) return;

    n = snprintf_P(line, sizeof(line), PSTR("AP attempts: %lu; heap at attempt: %lu\nLast AP startup failure: %u ("),
        (unsigned long)credentialApAttempts, (unsigned long)credentialApHeapAtAttempt,
        (unsigned)credentialApLastFailure);
    if (n <= 0 || n >= (int)sizeof(line)) return;
    if (!writeRootBytes(client,reinterpret_cast<const uint8_t*>(line),(size_t)n)) return;
    if (!writeCredentialProgmem(client,apFailureDescription(credentialApLastFailure))) return;
    n = snprintf_P(line, sizeof(line), PSTR(")\nOLED failure hold: %lums; queued: %u\n"),
                 OLED_DIAGNOSTIC_HOLD_MS,(unsigned)oledDiagnosticCount);
    if (n <= 0 || n >= (int)sizeof(line)) return;
    if (!writeRootBytes(client,reinterpret_cast<const uint8_t*>(line),(size_t)n)) return;
    n = snprintf_P(line, sizeof(line), PSTR(
        "AP diagnostic snapshot verified: %s\nAP check stage: %u; native error: %ld\n"
        "AP channel: %u; protocol mask: %u; bandwidth enum: %u\n"
        "SDK configuration does not prove RF beacon reception.\n"),
        credentialApStateVerified ? "YES" : "NO", (unsigned)credentialApLastCheckStage,
        (long)credentialApLastNativeError, (unsigned)credentialApChannel,
        (unsigned)credentialApProtocol, (unsigned)credentialApBandwidth);
    if (n <= 0 || n >= (int)sizeof(line)) return;
    if (!writeRootBytes(client,reinterpret_cast<const uint8_t*>(line),(size_t)n)) return;
    n = snprintf_P(line, sizeof(line), PSTR(
        "AP loss samples: %u/%u\nAP BOOT requests: %lu; last action: %s\n"
        "Configuration diagnostics never power off the setup AP.\n"),
        (unsigned)credentialApLossChecks, (unsigned)CREDENTIAL_AP_LOSS_CONFIRM_CHECKS,
        (unsigned long)credentialApBootRequests, credentialApLastAction);
    if (n <= 0 || n >= (int)sizeof(line)) return;
    if (!writeRootBytes(client,reinterpret_cast<const uint8_t*>(line),(size_t)n)) return;
    if (oledDiagnosticCount) {
        n = snprintf_P(line, sizeof(line), PSTR("OLED diagnostic: %s\nCause: %s\n"),
                     oledDiagnostics[0].title, oledDiagnostics[0].detail);
        if (n <= 0 || n >= (int)sizeof(line)) return;
        if (!writeRootBytes(client,reinterpret_cast<const uint8_t*>(line),(size_t)n)) return;
    }

    n = snprintf_P(line, sizeof(line), PSTR("Boot checkpoints: %s\nLast boot CP: %s (%lu)\n"
        "Wi-Fi attempts: %lu\nWi-Fi retry delay ms: %lu\n"
        "Offline policy: LOCAL SERVICE + SETUP AP AFTER 3 FAILED WI-FI ATTEMPTS\n"),
        bootCheckpointsActive ? "ACTIVE" : "DONE",
        crashStageLabel(bootLastCheckpoint), (unsigned long)bootLastCheckpoint,
        (unsigned long)wifiConnectAttempts, WIFI_RETRY_STEPS_MS[wifiRetryStep]);
    if (n <= 0 || n >= (int)sizeof(line)) return;
    if (!writeRootBytes(client,reinterpret_cast<const uint8_t*>(line),(size_t)n)) return;

    if (!writeCredentialProgmem(client, PSTR("RTC runtime events from previous reset:\n"))) return;
    for(uint8_t i=0;i<runtimeBeforeResetCount;++i) {
        const RuntimeBreadcrumbEntry& b=runtimeBeforeReset[i];
        n=snprintf_P(line,sizeof(line),PSTR("  #%lu @%lums %s detail=%u\n"),
            (unsigned long)b.sequence,(unsigned long)b.uptimeMs,
            runtimeBreadcrumbLabel(b.code),(unsigned)b.detail);
        if(n<=0||n>=(int)sizeof(line) ||
           !writeRootBytes(client,reinterpret_cast<const uint8_t*>(line),(size_t)n)) return;
    }
    if (!writeCredentialProgmem(client, PSTR("Last runtime event is a clue, not proof of cause.\n"))) return;

    if (rtcTraceBeforeResetValid) {
        n = snprintf_P(line, sizeof(line), PSTR("Previous RTC step: %s (%lu), detail=%lu\nPrevious RTC heap: %lu, largest=%lu\n"),
            crashStageLabel(rtcTraceBeforeReset.stage), (unsigned long)rtcTraceBeforeReset.stage,
            (unsigned long)rtcTraceBeforeReset.detail, (unsigned long)rtcTraceBeforeReset.heapFree,
            (unsigned long)rtcTraceBeforeReset.maxFreeBlock);
        if (n <= 0 || n >= (int)sizeof(line)) return;
        if (!writeRootBytes(client,reinterpret_cast<const uint8_t*>(line),(size_t)n)) return;
        if (rtcTraceBeforeReset.stage == CRASH_STAGE_BOOT_OLED_WRITE) {
            n = snprintf_P(line, sizeof(line), PSTR("Checkpoint OLED write pending CP: %s (%lu)\n"),
                         crashStageLabel(rtcTraceBeforeReset.detail), (unsigned long)rtcTraceBeforeReset.detail);
            if (n <= 0 || n >= (int)sizeof(line)) return;
            if (!writeRootBytes(client,reinterpret_cast<const uint8_t*>(line),(size_t)n)) return;
        }
    }

    if (!writeCredentialProgmem(client, PSTR(
        "\n[Credentials]\n"
        "Stored credential record: "))) return;
    if (!writeCredentialProgmem(client, haveStored ? PSTR("YES\n") : PSTR("NO\n"))) return;

    n = snprintf_P(line, sizeof(line), PSTR("Wi-Fi SSID configured: %s\n"
        "Wi-Fi password configured: %s\n"
        "MQTT host configured: %s\n"
        "MQTT host length: %u\n"
        "MQTT port: %u\n"
        "MQTT username configured: %s\n"
        "MQTT username length: %u\n"
        "MQTT password configured: %s\n"),
        ssid[0] != '\0' ? "YES" : "NO",
        password[0] != '\0' ? "YES" : "NO",
        mqttHost[0] != '\0' ? "YES" : "NO",
        (unsigned)strlen(mqttHost),
        (unsigned)mqttPort,
        mqttUser[0] != '\0' ? "YES" : "NO",
        (unsigned)strlen(mqttUser),
        mqttPassword[0] != '\0' ? "YES" : "NO");
    if (n <= 0 || n >= (int)sizeof(line)) return;
    if (!writeRootBytes(client,reinterpret_cast<const uint8_t*>(line),(size_t)n)) return;

    if (!writeCredentialProgmem(client, PSTR("\n[Wi-Fi]\n"))) return;
    n = snprintf(line, sizeof(line), "Application startup: %s\nApplication initialized: %s\nDisplay initialized: %s\n",
        PLATFORM_REQUIRE_ROUTER_BEFORE_APPLICATION ? "ROUTER FIRST" : "LOCAL FIRST",
        normalMeterInitialized ? "YES" : "NO", bootOledReady ? "YES" : "NO");
    if (n <= 0 || n >= (int)sizeof(line) || !writeRootBytes(client, reinterpret_cast<const uint8_t*>(line), (size_t)n)) return;
    n = snprintf(line, sizeof(line), "Configured LAN IP: 192.168.1.%u\nWi-Fi failed attempts: %lu; consecutive: %u/3\nSetup AP active: %s (automatic after 3 failures or manual)\n", (unsigned)configuredStaOctet, (unsigned long)wifiFailedAttempts, (unsigned)wifiConsecutiveFailures, wifiFallbackActive ? "YES" : "NO");
    if (n <= 0 || n >= (int)sizeof(line) || !writeRootBytes(client, reinterpret_cast<const uint8_t*>(line), (size_t)n)) return;
    if (wifiFallbackActive) {
        if (!writeCredentialProgmem(client, wifiFailureExplanation()) || !writeCredentialProgmem(client, PSTR("\n"))) return;
    }


    n = snprintf_P(line, sizeof(line), PSTR("Wi-Fi connected: %s\nWi-Fi status code: %d ("),
        WiFi.status() == WL_CONNECTED ? "YES" : "NO", (int)WiFi.status());
    if (n <= 0 || n >= (int)sizeof(line)) return;
    if (!writeRootBytes(client,reinterpret_cast<const uint8_t*>(line),(size_t)n)) return;
    if (!writeCredentialProgmem(client,wifiStatusDescription((int)WiFi.status()))) return;
    if (!writeCredentialProgmem(client,PSTR(")\nCurrent operation: "))) return;
    if (!writeCredentialProgmem(client,networkOperationDescription())) return;
    n = snprintf_P(line, sizeof(line), PSTR("\nLast recorded Wi-Fi disconnect: %u ("),(unsigned)wifiLastDisconnectReason);
    if (n <= 0 || n >= (int)sizeof(line)) return;
    if (!writeRootBytes(client,reinterpret_cast<const uint8_t*>(line),(size_t)n)) return;
    if (!writeCredentialProgmem(client,wifiDisconnectDescription(wifiLastDisconnectReason))) return;
    n = snprintf_P(line, sizeof(line), PSTR(")\nRSSI dBm: %d\nChannel: %d\nStatic IP profile: %s\n"),
        WiFi.status() == WL_CONNECTED ? (int)WiFi.RSSI() : 0,
        WiFi.status() == WL_CONNECTED ? (int)WiFi.channel() : 0, USE_STATIC_IP ? "YES" : "NO");
    if (n <= 0 || n >= (int)sizeof(line)) return;
    if (!writeRootBytes(client,reinterpret_cast<const uint8_t*>(line),(size_t)n)) return;

    const int txTenths = wifiTxTenths();
    char txLine[384];
    n = snprintf_P(txLine, sizeof(txLine),
        PSTR("Wi-Fi RX RSSI: %d dBm (connected only)\n"
             "Wi-Fi TX requested max: %d.%d dBm (not measured)\n"
             "Adaptive TX hard cap: 15.0 dBm\n"
             "Preconnect scan RSSI: %d dBm (-127 = unknown)\n"
             "TX scans: %lu; set-power failures: %lu\n"
             "Router outage policy: STA retries, no automatic AP\n"),
        WiFi.status() == WL_CONNECTED ? (int)WiFi.RSSI() : 0,
        txTenths / 10, txTenths % 10, wifiTxLastPreconnectRssi,
        (unsigned long)wifiTxScanCount, (unsigned long)wifiTxApplyFailures);
    if (n <= 0 || n >= (int)sizeof(txLine)) return;
    if (!writeRootBytes(client,reinterpret_cast<const uint8_t*>(txLine),(size_t)n)) return;

    if (WiFi.status() == WL_CONNECTED) {
        IPAddress lip = WiFi.localIP();
        IPAddress gw = WiFi.gatewayIP();
        IPAddress mask = WiFi.subnetMask();
        IPAddress dns0 = WiFi.dnsIP(0);
        IPAddress dns1 = WiFi.dnsIP(1);

        n = snprintf_P(line, sizeof(line), PSTR("Local IP: %u.%u.%u.%u\n"
            "Gateway: %u.%u.%u.%u\n"
            "Subnet: %u.%u.%u.%u\n"),
            lip[0], lip[1], lip[2], lip[3],
            gw[0], gw[1], gw[2], gw[3],
            mask[0], mask[1], mask[2], mask[3]);
        if (n <= 0 || n >= (int)sizeof(line)) return;
        if (!writeRootBytes(client,reinterpret_cast<const uint8_t*>(line),(size_t)n)) return;

        n = snprintf_P(line, sizeof(line), PSTR("DNS #1: %u.%u.%u.%u\n"
            "DNS #2: %u.%u.%u.%u\n"),
            dns0[0], dns0[1], dns0[2], dns0[3],
            dns1[0], dns1[1], dns1[2], dns1[3]);
        if (n <= 0 || n >= (int)sizeof(line)) return;
        if (!writeRootBytes(client,reinterpret_cast<const uint8_t*>(line),(size_t)n)) return;
    }

    if (!writeCredentialProgmem(client, PSTR("\n[MQTT / DNS]\n"))) return;

    n = snprintf_P(line, sizeof(line), PSTR("MQTT configured: %s\nMQTT connected: %s\nMQTT client state (last result): %d ("),
        mqttSettingsReady() ? "YES" : "NO",mqttClient.connected() ? "YES" : "NO",mqttLastAttemptState);
    if (n <= 0 || n >= (int)sizeof(line)) return;
    if (!writeRootBytes(client,reinterpret_cast<const uint8_t*>(line),(size_t)n)) return;
    if (!writeCredentialProgmem(client,mqttStateDescription(mqttLastAttemptState))) return;
    n = snprintf_P(line, sizeof(line), PSTR(")\nMQTT client state (current): %d ("), mqttClient.state());
    if (n <= 0 || n >= (int)sizeof(line)) return;
    if (!writeRootBytes(client,reinterpret_cast<const uint8_t*>(line),(size_t)n)) return;
    if (!writeCredentialProgmem(client,mqttStateDescription(mqttClient.state()))) return;
    if (!writeCredentialProgmem(client,PSTR(")\nDNS lookup: "))) return;
    if (!writeCredentialProgmem(client,dnsLookupDescription(dnsAttempted,dnsAddressUsable))) return;
    n = snprintf_P(line, sizeof(line), PSTR("\nDNS resolver return code: %d\nMQTT DNS usable: %s\n"),
                 dnsResult,dnsAddressUsable ? "YES" : "NO");
    if (n <= 0 || n >= (int)sizeof(line)) return;
    if (!writeRootBytes(client,reinterpret_cast<const uint8_t*>(line),(size_t)n)) return;

    n = snprintf_P(line, sizeof(line), PSTR("MQTT resolved IP: %u.%u.%u.%u\n"
        "DNS address class: %s\n"),
        resolved[0], resolved[1], resolved[2], resolved[3],
        resolvedBroadcast ? "INVALID/BROADCAST"
            : (resolvedAllZero ? "INVALID/ZERO"
                               : (dnsAddressUsable ? "USABLE" : "UNRESOLVED")));
    if (n <= 0 || n >= (int)sizeof(line)) return;
    if (!writeRootBytes(client,reinterpret_cast<const uint8_t*>(line),(size_t)n)) return;

    const unsigned long lastAttemptAgeSec =
        mqttLastAttemptMs == 0 ? 0UL : (millis() - mqttLastAttemptMs) / 1000UL;
    n = snprintf_P(line, sizeof(line), PSTR("MQTT connect attempts: %lu\n"
        "MQTT connect failures: %lu\n"
        "MQTT state -2 runtime streak: %u\n"
        "Network stack self-heals: %lu\n"
        "MQTT update retry delay ms: %lu\n"),
        (unsigned long)mqttConnectAttempts,
        (unsigned long)mqttConnectFailures,
        (unsigned)mqttRuntimeConnectFailureStreak,
        (unsigned long)networkStackRecoveryCount,
        (unsigned long)mqttUpdateRetryDelayMs);
    if (n <= 0 || n >= (int)sizeof(line)) return;
    if (!writeRootBytes(client,reinterpret_cast<const uint8_t*>(line),(size_t)n)) return;

    n = snprintf_P(line, sizeof(line), PSTR("Last connect succeeded: %s\n"
        "Last attempt age seconds: %lu\n"
        "Last status publish: %s\n"
        "Request subscribe: %s\n"
        "Admin subscribe: %s\n"),
        mqttLastConnectSucceeded ? "YES" : "NO",
        lastAttemptAgeSec,
        mqttLastStatusPublishSucceeded ? "OK" : "NO/NOT YET",
        mqttLastRequestSubscribeSucceeded ? "OK" : "NO/NOT YET",
        mqttLastAdminSubscribeSucceeded ? "OK" : "NO/NOT YET");
    if (n <= 0 || n >= (int)sizeof(line)) return;
    if (!writeRootBytes(client,reinterpret_cast<const uint8_t*>(line),(size_t)n)) return;

    if (!writeCredentialProgmem(client, PSTR("\n[TLS / ESP32 mbedTLS]\nCertificate verification: built-in CA bundle\n"))) return;
    n = snprintf_P(line, sizeof(line), PSTR("TLS input capacity bytes: %u\nTLS output capacity bytes: %u\nClock ready for certificate verification: %s\n"),
        (unsigned)MQTT_TLS_RX_BYTES, (unsigned)MQTT_TLS_TX_BYTES,
        isSystemTimeValid() ? "YES" : "WAITING FOR NTP");
    if (n <= 0 || n >= (int)sizeof(line) ||
        !writeRootBytes(client,reinterpret_cast<const uint8_t*>(line),(size_t)n)) return;

    n = snprintf_P(line, sizeof(line), PSTR("Last SSL error code: %d\n"
        "Last SSL error text present: %s\n"),
        mqttLastSslErrorCode,
        mqttLastSslErrorText[0] != '\0' ? "YES" : "NO");
    if (n <= 0 || n >= (int)sizeof(line)) return;
    if (!writeRootBytes(client,reinterpret_cast<const uint8_t*>(line),(size_t)n)) return;

    if (mqttLastSslErrorText[0] != '\0') {
        if (!writeCredentialProgmem(client, PSTR("Last SSL error text: "))) return;
        const size_t errLen = strnlen(mqttLastSslErrorText, sizeof(mqttLastSslErrorText));
        if (!writeRootBytes(client, reinterpret_cast<const uint8_t*>(mqttLastSslErrorText), errLen)) return;
        if (!writeCredentialProgmem(client, PSTR("\n"))) return;
    } else if (mqttLastSslErrorCode == 0) {
        if (!writeCredentialProgmem(client,PSTR("TLS result: no TLS error recorded\n"))) return;
    }

    if (!writeCredentialProgmem(client, PSTR("\n[Memory / Runtime]\n"))) return;

    n = snprintf_P(line, sizeof(line), PSTR("TLS preflight free heap bytes: %lu\n"
        "TLS preflight max block bytes: %u\n"
        "TLS preflight deferred attempts: %lu\n"),
        (unsigned long)mqttConnectHeapAtAttempt,
        (unsigned)mqttConnectBlockAtAttempt,
        (unsigned long)mqttConnectPreflightDeferrals);
    if (n <= 0 || n >= (int)sizeof(line)) return;
    if (!writeRootBytes(client,reinterpret_cast<const uint8_t*>(line),(size_t)n)) return;

    n = snprintf_P(line, sizeof(line), PSTR("Free heap bytes: %lu\n"
        "Max free block bytes: %u\n"
        "Heap fragmentation percent: %u\n"
        "Free contiguous stack bytes: %lu\n"
        "Current boot reason: %s (%u)\n"),
        (unsigned long)ESP.getFreeHeap(),
        (unsigned)platformMaxFreeBlock(),
        (unsigned)platformHeapFragmentation(),
        (unsigned long)platformFreeStack(),
        resetReasonLabel(currentBootReset.reason),
        (unsigned)currentBootReset.reason);
    if (n <= 0 || n >= (int)sizeof(line)) return;
    if (!writeRootBytes(client,reinterpret_cast<const uint8_t*>(line),(size_t)n)) return;

    if (!applicationAppendDebug(client)) return;
    writeCredentialProgmem(client, PSTR(
        "\nNo Wi-Fi or MQTT passwords are shown in this report.\n"
        "</pre><p><a href='/setup'>Edit credentials</a> · "
        "<a href='/'>Back</a></p></body></html>"));
}
void handleMqttStatusJson() {
    WebResponseScope responseScope;
    if (!requireApWebPin()) return;
    char body[512];
    int n = snprintf_P(body, sizeof(body), PSTR(
        "{\"device\":\"%s\",\"firmware\":\"%s\",\"wifi_connected\":%s,"
        "\"mqtt_configured\":%s,\"mqtt_connected\":%s,\"mqtt_state\":%d,"
        "\"mqtt_last_state\":%d,\"mqtt_attempts\":%lu,\"mqtt_failures\":%lu,"
        "\"tls_error\":%d,\"heap_free\":%lu,\"heap_largest\":%u,"
        "\"connect_heap\":%lu,\"connect_block\":%u,\"preflight_deferrals\":%lu,\"tls_rx\":%u}"),
        mqttDeviceId, VERSION, WiFi.status() == WL_CONNECTED ? "true" : "false",
        mqttSettingsReady() ? "true" : "false", mqttClient.connected() ? "true" : "false",
        mqttClient.state(), mqttLastAttemptState,
        (unsigned long)mqttConnectAttempts, (unsigned long)mqttConnectFailures,
        mqttLastSslErrorCode, (unsigned long)ESP.getFreeHeap(), (unsigned)platformMaxFreeBlock(),
        (unsigned long)mqttConnectHeapAtAttempt, (unsigned)mqttConnectBlockAtAttempt,
        (unsigned long)mqttConnectPreflightDeferrals, (unsigned)mqttTlsRecvConfiguredBytes);
    server.sendHeader("Cache-Control", "no-store");
    if (n <= 0 || n >= (int)sizeof(body)) {
        server.send(503, "application/json", "{\"error\":\"diagnostics_overflow\"}");
        return;
    }
    server.send(200, "application/json; charset=utf-8", body);
}
void configureCredentialRoutes() {
    if (credentialRoutesConfigured) return;
    const char* headers[] = {"Cookie"};
    server.collectHeaders(headers, 1);
    server.on("/ap-login", HTTP_POST, handleApWebLogin);
    server.on("/setup", HTTP_GET, handleCredentialSetupPage);
    server.on("/setup-save", HTTP_POST, handleCredentialSave);
    server.on("/forget-credentials-confirm", HTTP_GET, handleForgetCredentialsConfirm);
    server.on("/forget-credentials", HTTP_POST, handleForgetCredentials);
    server.on("/network-debug", HTTP_GET, handleNetworkDebugPage);
    server.on("/mqtt-status", HTTP_GET, handleMqttStatusJson);
    credentialRoutesConfigured = true;
}

IPAddress platformSetupApAddress() {
    // E.g. STA 192.168.1.82 -> fallback/manual AP 192.168.2.82.
    return IPAddress(192, 168, 2, configuredStaOctet);
}
bool configureSetupApAddress() {
    const IPAddress setupIp = platformSetupApAddress();
    const IPAddress setupMask(255, 255, 255, 0);
    // Keep DHCP leases away from the gateway even for arbitrary XX.
    const uint8_t leaseOctet = (configuredStaOctet >= 100 && configuredStaOctet < 200)
                                ? 220 : 100;
    const IPAddress leaseStart(192, 168, 2, leaseOctet);
    return WiFi.softAPConfig(setupIp, setupIp, setupMask, leaseStart, setupIp) &&
           WiFi.softAPIP() == setupIp;
}

// These checks confirm the SDK's live interface/configuration, not reception
// of a beacon over the air. Physical RF visibility still requires a client.
bool platformReadSetupApState() {
    // A diagnostic snapshot must never control the lifetime of a working AP.
    credentialApStateVerified = false;
    credentialApChannel = credentialApProtocol = credentialApBandwidth = 0;
    wifi_config_t config = {};
    credentialApLastCheckStage = 10;
    credentialApLastNativeError = esp_wifi_get_config(WIFI_IF_AP, &config);
    if (credentialApLastNativeError != ESP_OK) return false;
    const size_t ssidLength = credentialApName.length();
    // IDF permits either an explicit length or a NUL-terminated SSID with len0.
    const size_t actualLength = config.ap.ssid_len ? config.ap.ssid_len :
        strnlen(reinterpret_cast<const char*>(config.ap.ssid), sizeof(config.ap.ssid));
    credentialApLastCheckStage = 11;
    if (!ssidLength || ssidLength > sizeof(config.ap.ssid) ||
        (!config.ap.ssid_len && actualLength == sizeof(config.ap.ssid)) ||
        actualLength != ssidLength ||
        memcmp(config.ap.ssid, credentialApName.c_str(), ssidLength) != 0 ||
        config.ap.ssid_hidden != 0 || config.ap.authmode != WIFI_AUTH_OPEN ||
        config.ap.channel != CREDENTIAL_AP_CHANNEL) {
        credentialApLastNativeError = ESP_ERR_INVALID_STATE;
        return false;
    }
    // Max clients, beacon interval and PHY values may be normalized by IDF.
    // Observe them without requiring a byte-for-byte configuration echo.
    wifi_second_chan_t secondary = WIFI_SECOND_CHAN_NONE;
    credentialApLastCheckStage = 12;
    credentialApLastNativeError = esp_wifi_get_channel(&credentialApChannel, &secondary);
    if (credentialApLastNativeError != ESP_OK) return false;
    credentialApLastCheckStage = 14;
    credentialApLastNativeError = esp_wifi_get_protocol(WIFI_IF_AP, &credentialApProtocol);
    if (credentialApLastNativeError != ESP_OK) return false;
    wifi_bandwidth_t bandwidth = WIFI_BW_HT20;
    credentialApLastCheckStage = 16;
    credentialApLastNativeError = esp_wifi_get_bandwidth(WIFI_IF_AP, &bandwidth);
    if (credentialApLastNativeError != ESP_OK) return false;
    credentialApBandwidth = (uint8_t)bandwidth;
    credentialApLastCheckStage = 18;
    if (WiFi.softAPIP() != platformSetupApAddress()) {
        credentialApLastNativeError = ESP_ERR_INVALID_STATE;
        return false;
    }
    credentialApLastNativeError = ESP_OK;
    credentialApLastCheckStage = 19;
    credentialApStateVerified = true;
    return true;
}

// Return unknown (-1) separately from an observed stopped interface (0).
// Transient SDK errors and configuration diagnostics cannot close the AP.
int platformSetupApLiveState() {
    wifi_mode_t mode = WIFI_MODE_NULL;
    const esp_err_t result = esp_wifi_get_mode(&mode);
    if (result != ESP_OK) {
        credentialApLastCheckStage = 20;
        credentialApLastNativeError = result;
        return -1;
    }
    if ((mode & WIFI_MODE_AP) == 0) {
        credentialApLastCheckStage = 21;
        credentialApLastNativeError = ESP_ERR_INVALID_STATE;
        return 0;
    }
    if (!WiFi.AP.started()) {
        credentialApLastCheckStage = 22;
        credentialApLastNativeError = ESP_ERR_INVALID_STATE;
        return 0;
    }
    return 1;
}

bool platformStartSetupApRadio() {
    // A manual AP transition may interrupt an in-flight STA RSSI scan.
    if (wifiTxScanning) {
        (void)esp_wifi_scan_stop();
        WiFi.scanDelete();
        wifiTxScanning = false;
        wifiAttemptActive = false;
    }
    credentialApStateVerified = false;
    credentialApLastNativeError = ESP_OK;
    credentialApLastFailure = 1;
    // Follow the native simple AP lifecycle: never recycle a healthy radio.
    // In particular, softAPdisconnect() on an OFF C3 would first start an AP.
    WiFi.persistent(false);
    credentialApLastCheckStage = 2;
    const wifi_mode_t requestedMode =
        wifiFallbackActive && !credentialPortalOnly ? WIFI_AP_STA : WIFI_AP;
    if (WiFi.getMode() != requestedMode) {
        if (!WiFi.mode(requestedMode)) {
            credentialApLastNativeError = ESP_FAIL;
            return false;
        }
    } else if (!WiFi.AP.started()) {
        // mode(AP) is a no-op when the mode is already AP. Recover an actually
        // stopped driver without powering down an interface that is running.
        credentialApLastCheckStage = 3;
        const esp_err_t result = esp_wifi_start();
        if (result != ESP_OK && result != ESP_ERR_WIFI_NOT_STOPPED) {
            credentialApLastNativeError = result;
            return false;
        }
    }
    credentialApLastFailure = 3;
    credentialApLastCheckStage = 5;
    // Safe TX limit on setup AP too, before the SSID is advertised.
    if (!wifiTxApply(0, "setup AP")) {
        credentialApLastNativeError = ESP_FAIL;
        return false;
    }
    // Keep the SDK's normal PHY defaults, as in the legacy working Boiler.
    // Channel1, visible SSID, open Wi-Fi; the page still requires its PIN.
    if (!WiFi.softAP(credentialApName.c_str(), nullptr, CREDENTIAL_AP_CHANNEL,
                     0, CREDENTIAL_AP_MAX_CONNECTIONS)) {
        credentialApLastNativeError = ESP_FAIL;
        return false;
    }
    credentialApLastFailure = 2;
    credentialApLastCheckStage = 6;
    if (!configureSetupApAddress()) {
        credentialApLastNativeError = ESP_FAIL;
        return false;
    }
    // softAP() already waits for native AP_START. Diagnostic getter failures
    // must not turn its successful result into an instruction to stop Wi-Fi.
    (void)platformReadSetupApState();
    credentialApLastFailure = 0;
    return true;
}

void startCredentialAccessPoint(bool portalOnly) {
    if (credentialPortalActive) return;
    credentialPortalLastAttemptMs = millis();
    ++credentialApAttempts;
    credentialApHeapAtAttempt = ESP.getFreeHeap();
    markCrashStage(CRASH_STAGE_BOOT_AP_START, ESP.getFreeHeap());
    bootCheckpoint(CRASH_STAGE_BOOT_AP_START);

    credentialPortalOnly = portalOnly;
    credentialDns.stop();
    credentialApName = String(DEVICE_LABEL "-") + DEVICE_ID + "-" +
                       String(platformChipId() & 0xFFFFFFUL, HEX);
    credentialApName.toUpperCase();
    credentialApPassword = "";
    resetApWebSession();
    credentialApLossChecks = 0;
    credentialApLastAction = "setup_start";

    credentialPortalActive = platformStartSetupApRadio();
    credentialApLastHealthCheckMs = millis();
    credentialApStartedMs = credentialApLastHealthCheckMs;
    credentialApLastDiagnosticMs = credentialApLastHealthCheckMs;
    if (!credentialPortalActive) {
        // Keep any running AP alive. An IP/getter/startup failure must not
        // create repeated beacon-on/radio-off cycles while provisioning.
        credentialApLastAction = "start_retry_pending";
        char detail[64];
        snprintf(detail, sizeof(detail), "Stage:%u err:%ld; retry 30s",
                 (unsigned)credentialApLastCheckStage,
                 (long)credentialApLastNativeError);
        queueOledDiagnostic(OLED_DIAG_SETUP_AP, "SETUP AP RETRY", detail);
        drawOledDiagnostic(millis());
        return;
    }

    credentialApLastAction = "setup_running";
    credentialDns.start(53, "*", WiFi.softAPIP());
    configureCredentialRoutes();
    // The server may already be listening on STA during a router outage.
    if (!networkServicesStarted) server.begin();
    if (drawOledDiagnostic(millis())) return;
    cs();
    doublePrintf(420, 1, 1, "SETUP AP / v%s", VERSION);
    doublePrintf(420, 2, 1, "%s", credentialApName.c_str());
    doublePrintf(420, 3, 1, "WiFi open PIN:%s", AP_WEB_PIN);
    doublePrintf(420, 4, 1, "Open: %s", WiFi.softAPIP().toString().c_str());
}

void serviceCredentialApHealth(unsigned long now) {
    if (!credentialPortalActive ||
        (uint32_t)(now - credentialApLastHealthCheckMs) < CREDENTIAL_AP_HEALTH_CHECK_MS) return;
    credentialApLastHealthCheckMs = now;
    const int liveState = platformSetupApLiveState();
    if (liveState > 0) {
        credentialApLossChecks = 0;
        if ((uint32_t)(now - credentialApLastDiagnosticMs) >= CREDENTIAL_AP_DIAGNOSTIC_MS) {
            credentialApLastDiagnosticMs = now;
            (void)platformReadSetupApState(); // Diagnostic only; never closes AP.
        }
        return;
    }
    if (liveState < 0 ||
        (uint32_t)(now - credentialApStartedMs) < CREDENTIAL_AP_START_GRACE_MS) {
        credentialApLossChecks = 0;
        return;
    }
    if (++credentialApLossChecks < CREDENTIAL_AP_LOSS_CONFIRM_CHECKS) return;

    // Three consecutive stopped-interface observations after startup grace.
    // Revoke stale sessions, then retry normally; never force a radio OFF.
    credentialPortalActive = false;
    credentialApStateVerified = false;
    credentialApLastFailure = 6;
    credentialApLastAction = "confirmed_ap_loss";
    credentialPortalLastAttemptMs = now;
    credentialDns.stop();
    resetApWebSession();
    char detail[64];
    snprintf(detail, sizeof(detail), "Stage:%u err:%ld; retry 30s",
             (unsigned)credentialApLastCheckStage,
             (long)credentialApLastNativeError);
    queueOledDiagnostic(OLED_DIAG_SETUP_AP, "SETUP AP LOST", detail);
    drawOledDiagnostic(millis());
}

void stopCredentialAccessPoint() {
    if (!credentialPortalActive || credentialPortalOnly) return;
    // Remove only AP: do not power off the shared AP+STA radio.
    if (!WiFi.mode(WIFI_STA)) return;
    credentialDns.stop();
    credentialPortalActive = false;
    credentialApStateVerified = false;
    resetApWebSession();
    credentialApName = "";
    credentialApPassword = "";
    wifiFallbackActive = false;
}

// Save/clear can be requested over LAN, AP or Recovery, including failed AP starts.
bool serviceCredentialRestart(unsigned long now) {
    if (!credentialRestartPending ||
        now - credentialRestartRequestedMs < CREDENTIAL_RESTART_DELAY_MS) return false;
    credentialRestartPending = false;
#if PLATFORM_LOCAL_CONTROL_APPLICATION
    applicationPrepareOta();
#endif
    delay(20); // Allow the already-sent response to leave the socket.
    ESP.restart();
    return true;
}

void serviceCredentialPortal(unsigned long now) {
    serviceCredentialApHealth(now);
    if (!credentialPortalActive) {
        // A failed AP start is not a permanent dead end in provisioning/Recovery.
        if ((credentialPortalOnly || wifiFallbackActive) &&
            (uint32_t)(now - credentialPortalLastAttemptMs) >= CREDENTIAL_AP_RETRY_MS) {
            startCredentialAccessPoint(credentialPortalOnly);
        }
        return;
    }
    credentialDns.processNextRequest();
    server.handleClient();

}

void configureProvisioningRoutes() {
    configureCredentialRoutes();
    server.on("/", HTTP_GET, handleCredentialSetupPage);
    server.onNotFound([]() {
        WebResponseScope responseScope;
        if (!requireApWebPin()) return;
        server.sendHeader("Location", String("http://") +
            WiFi.softAPIP().toString() + "/setup", true);
        server.send(302, "text/plain", "");
    });
}

void configureRecoveryRoutes() {
    configureCredentialRoutes();
    server.on("/", HTTP_GET, handleRecoveryPage);
    server.on("/debug", HTTP_GET, handleRecoveryPage);
    server.on("/debug.txt", HTTP_GET, handleRecoveryText);
    server.on("/restart-normal", HTTP_GET, handleRecoveryRestartNormal);
    server.onNotFound([]() {
        WebResponseScope responseScope;
        if (!requireApWebPin()) return;
        server.sendHeader("Cache-Control", "no-store");
        server.send(503, "text/plain; charset=utf-8",
                    "OTA-ONLY RECOVERY: open /debug or /restart-normal.");
    });
}

// The recovery loop never calls applicationReadMeasurements(), PubSubClient,
// tariff, OLED measurement drawing, EEPROM accounting or admin commands.
void serviceRecoveryOnly(unsigned long now) {
    if (recoveryNormalRestartPending &&
        now - recoveryNormalRestartRequestedMs >= RECOVERY_NORMAL_RESTART_DELAY_MS) {
        clearRtcCrashTrace();
        delay(20);
        ESP.restart();
        return;
    }

    // Recovery OTA works on the normal Wi-Fi station or on the dedicated
    // setup AP. In both cases IDE_OTA is advertised through mDNS.
    if (!networkServicesStarted &&
        (WiFi.status() == WL_CONNECTED || credentialPortalActive)) {
        markCrashStage(CRASH_STAGE_NETWORK_SERVICES, ESP.getFreeHeap());
        startNetworkServices();
    }

    if (networkServicesStarted) {
        if (!credentialPortalActive) server.handleClient();
        ArduinoOTA.handle();
    }

    serviceWiFiReconnect(now);

    if (recoveryDisplayMs == 0 || now - recoveryDisplayMs >= 3000UL) {
        recoveryDisplayMs = now;
        if (!recoveryOtaInProgress) showRecoveryScreen();
    }
    yield();
}

void boilerChartsPage();
void boilerChartsData();
void configureWebRoutes() {
    configureCredentialRoutes();
    server.on("/live", HTTP_GET, handleRoot);
    server.on("/", HTTP_GET, []() {
        if (credentialPortalOnly || (wifiFallbackActive && credentialPortalActive))
            handleCredentialSetupPage();
        else handleRoot();
    });
    server.on("/force-ota-recovery-confirm", HTTP_GET, handleForceOtaRecoveryConfirm);
    server.on("/force-ota-recovery", HTTP_POST, handleForceOtaRecovery);
    server.on("/restart-esp", HTTP_GET, handleRestartEspConfirm);
    server.on("/restart-esp", HTTP_POST, handleRestartEsp);
    server.on("/charts", HTTP_GET, boilerChartsPage);
    server.on("/charts-data", HTTP_GET, boilerChartsData);
    server.onNotFound([]() {
        WebResponseScope responseScope;
        if (!requireApWebPin()) return;
        if (credentialPortalOnly || (wifiFallbackActive && credentialPortalActive)) {
            server.sendHeader("Location", String("http://") +
                WiFi.softAPIP().toString() + "/setup", true);
            server.send(302, "text/plain", "");
            return;
        }
        server.send(404, "text/plain; charset=utf-8", "Not found");
    });
#if PLATFORM_LOCAL_CONTROL_APPLICATION && !PLATFORM_REQUIRE_ROUTER_BEFORE_APPLICATION
    applicationConfigureWebRoutes();
#endif
}

void startNetworkServices() {
    const bool networkReady =
        WiFi.status() == WL_CONNECTED || (recoveryMode && credentialPortalActive);
    if (networkServicesStarted || !networkReady) return;

    server.begin();

    if (recoveryMode) {
        // IDE_OTA exists only in OTA-ONLY Recovery. Passing true enables
        // mDNS advertisement so Arduino IDE clients can discover the ESP32-C3.
        otaHostname = mqttDeviceId;
        otaHostname.replace("_", "-");
        ArduinoOTA.setHostname(otaHostname.c_str());
        ArduinoOTA.onStart([]() {
            recoveryOtaInProgress = true;
            cs();
            doublePrintf(420, 1, 1, "RECOVERY / OTA");
            doublePrintf(420, 2, 1, "Uploading firmware");
        });
        ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
            if (total > 0) {
                platformDisplay.font(DisplayFont::Normal10);
                char percentText[24];
                snprintf(percentText, sizeof(percentText), "Progress %u%%",
                         (unsigned)((uint64_t)progress * 100UL / total));
                platformDisplay.text(0, 45, percentText);
                platformDisplay.present();
            }
        });
        ArduinoOTA.onEnd([]() {
            bool clearSucceeded = true;
            if (littleFsReady && LittleFS.exists(RECOVERY_FILE)) {
                clearSucceeded = LittleFS.remove(RECOVERY_FILE);
            }
            recoveryOtaClearFailed = !clearSucceeded;
            if (clearSucceeded) recoveryIncidentPersistent = false;

            if (!clearSucceeded) {
                queueOledDiagnostic(OLED_DIAG_RECOVERY_CLEAR, "RECOVERY CLEAR FAILED", "Firmware received; Recovery record was not cleared");
                drawOledDiagnostic(millis());
                return;
            }

            cs();
            doublePrintf(420, 1, 1, "OTA SUCCESS");
            doublePrintf(420, 2, 1,
                         clearSucceeded ? "Recovery cleared" : "Recovery NOT cleared");
            doublePrintf(420, 3, 1,
                         clearSucceeded ? "Reboot -> NORMAL" : "Use web Recovery exit");
        });
        ArduinoOTA.onError([](ota_error_t error) {
            recoveryOtaInProgress = false;
            recoveryOtaClearFailed = false;
            char detail[64];
            snprintf(detail, sizeof(detail), "IDE_OTA error=%u. Try OTA again", (unsigned)error);
            queueOledDiagnostic(OLED_DIAG_OTA, "RECOVERY OTA FAILED", detail);
            drawOledDiagnostic(millis());
        });
        ArduinoOTA.setMdnsEnabled(true);
        ArduinoOTA.begin();
    } else {
        configTzTime(GREECE_TZ, "pool.ntp.org", "time.nist.gov");
    }

    networkServicesStarted = true;
}

void observeWiFiDisconnect(arduino_event_id_t event, arduino_event_info_t info) {
    if (event != ARDUINO_EVENT_WIFI_STA_DISCONNECTED) return;
    const uint16_t reason = (uint16_t)info.wifi_sta_disconnected.reason;
    if (reason == WIFI_REASON_AUTH_LEAVE || reason == WIFI_REASON_ASSOC_LEAVE) return;
    // Arduino network callbacks run on another FreeRTOS task. Latch only;
    // display/diagnostic queues are processed in the main loop, never here.
    portENTER_CRITICAL(&wifiEventMux);
    if (!wifiDisconnectPending) pendingWiFiDisconnectReason = reason;
    wifiDisconnectPending = true;
    portEXIT_CRITICAL(&wifiEventMux);
}
void servicePendingWiFiDisconnect() {
    portENTER_CRITICAL(&wifiEventMux);
    const bool pending = wifiDisconnectPending;
    const uint16_t reason = pendingWiFiDisconnectReason;
    wifiDisconnectPending = false;
    portEXIT_CRITICAL(&wifiEventMux);
    if (!pending || credentialPortalOnly ||
        (WiFi.getMode() != WIFI_STA && WiFi.getMode() != WIFI_AP_STA) ||
        reason == WIFI_REASON_AUTH_LEAVE || reason == WIFI_REASON_ASSOC_LEAVE) return;
    wifiLastDisconnectReason = reason;
    if (wifiAttemptActive && !wifiAttemptFailedEvent) {
        wifiAttemptEventReason = reason;
        wifiAttemptFailedEvent = true;
    }
}

// After three router failures, open temporary AP+STA for local access.
// Continue STA retries; automatic AP closes when STA reconnects. FLASH keeps manual AP.
void finishWiFiAttemptFailure(uint8_t kind, uint16_t reason, int status) {
    if (!wifiAttemptActive) return; // Only one terminal result per explicit attempt.
    wifiAttemptActive = false;
    wifiAttemptFailedEvent = false;
    ++wifiFailedAttempts;
    if (wifiConsecutiveFailures < WIFI_MAX_FAILED_ATTEMPTS) ++wifiConsecutiveFailures;
    wifiFallbackFailureKind = kind;
    wifiFallbackReason = reason;
    wifiFallbackStatus = status;
    // Permit retry power escalation only within 15 dBm.
    if (wifiTxCurrentLevel < WIFI_TX_LAST_LEVEL)
        wifiTxRetryFloor = wifiTxCurrentLevel == 0 ? 2 : WIFI_TX_LAST_LEVEL;
    if (wifiRetryStep + 1 < WIFI_RETRY_STEP_COUNT) ++wifiRetryStep;
    lastWifiRetry = millis();
    // Three failed connections open an AP+STA (not AP-only) for local recovery.
    if (wifiConsecutiveFailures >= WIFI_MAX_FAILED_ATTEMPTS &&
        !wifiFallbackActive && !credentialPortalOnly && ssid[0]) {
        wifiFallbackActive = true;
        startCredentialAccessPoint(false);
    }
}

void startTrackedWiFiAttempt() {
    if (credentialPortalOnly || !ssid[0]) return;
    WiFi.persistent(false);
    if (WiFi.getMode() == WIFI_STA) WiFi.disconnect(false);
    portENTER_CRITICAL(&wifiEventMux);
    wifiDisconnectPending = false;
    pendingWiFiDisconnectReason = 0;
    portEXIT_CRITICAL(&wifiEventMux);
    wifiAttemptFailedEvent = false;
    wifiAttemptEventReason = 0;
    wifiAttemptActive = true;
    wifiAttemptStartedMs = millis();
    lastWifiRetry = wifiAttemptStartedMs;
    ++wifiConnectAttempts;
    const wifi_mode_t targetMode =
        wifiFallbackActive && credentialPortalActive ? WIFI_AP_STA : WIFI_STA;
    if (WiFi.getMode() != targetMode && !WiFi.mode(targetMode)) {
        finishWiFiAttemptFailure(3, 0, (int)WiFi.status());
        return;
    }
    WiFi.setAutoReconnect(false);
    // Fail closed when requested TX ceiling cannot be installed.
    if (!wifiTxApply(0, "preconnect safe limit")) {
        finishWiFiAttemptFailure(6, 0, (int)WiFi.status());
        return;
    }
    const IPAddress localIp(192, 168, 1, configuredStaOctet);
    const IPAddress gateway(192, 168, 1, 1), subnet(255, 255, 255, 0);
    if (!WiFi.config(localIp, gateway, subnet, gateway)) {
        finishWiFiAttemptFailure(4, 0, (int)WiFi.status());
        return;
    }
    // In AP+STA recovery, avoid explicit all-channel scans on every retry:
    // they interrupt AP service. Use bounded 15 dBm and normal STA join.
    if (wifiFallbackActive && credentialPortalActive) {
        if (!wifiTxApply(WIFI_TX_LAST_LEVEL, "AP+STA recovery")) {
            finishWiFiAttemptFailure(6, 0, (int)WiFi.status());
            return;
        }
        const int result = (int)WiFi.begin(ssid, password);
        if (result == 4 || result == 255)
            finishWiFiAttemptFailure(5, 0, result);
        return;
    }
    // The initial/pre-AP scan is asynchronous.
    wifiTxScanning = true;
    wifiTxScanStartedMs = millis();
    ++wifiTxScanCount;
    const int scanStart = WiFi.scanNetworks(true, true, false, 120);
    if (scanStart != WIFI_SCAN_RUNNING)
        wifiTxScanStartedMs = millis() - WIFI_TX_SCAN_TIMEOUT_MS;
}

void serviceWifiTxPrescan(unsigned long now) {
    if (!wifiTxScanning) return;
    int count = WiFi.scanComplete();
    if (count == WIFI_SCAN_RUNNING &&
        (uint32_t)(now - wifiTxScanStartedMs) < WIFI_TX_SCAN_TIMEOUT_MS) return;
    if (count == WIFI_SCAN_RUNNING) {
        (void)esp_wifi_scan_stop();
        count = WiFi.scanComplete();
    }
    wifiTxLastPreconnectRssi = -127;
    for (int i = 0; i < count; ++i) {
        if (WiFi.SSID(i) == ssid && (int)WiFi.RSSI(i) > wifiTxLastPreconnectRssi)
            wifiTxLastPreconnectRssi = (int)WiFi.RSSI(i);
    }
    WiFi.scanDelete();
    wifiTxScanning = false;

    uint8_t chosen = wifiTxLastPreconnectRssi == -127
        ? 0 : wifiTxLevelForRssi(wifiTxLastPreconnectRssi);
    if (chosen < wifiTxRetryFloor) chosen = wifiTxRetryFloor;
    if (!wifiTxApply(chosen, "preconnect RSSI / bounded retry")) {
        finishWiFiAttemptFailure(6, 0, (int)WiFi.status());
        return;
    }
    wifiAttemptStartedMs = millis(); // association timeout, excluding scan
    const int result = (int)WiFi.begin(ssid, password);
    if (result == 4 || result == 255) finishWiFiAttemptFailure(5, 0, result);
}

void connectWiFiInitial(bool waitForConnection) {
    (void)waitForConnection; // All router attempts are bounded and nonblocking.
    bootCheckpoint(CRASH_STAGE_WIFI_CONNECT);
    if (!wifiDisconnectHandler) {
        wifiDisconnectHandler = WiFi.onEvent(observeWiFiDisconnect, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
    }
    wifiConsecutiveFailures = 0;
    wifiRetryStep = 0;
    wifiWasConnected = false;
    wifiAttemptActive = false;
    startTrackedWiFiAttempt();
}

bool serviceNetworkStackRecovery(unsigned long now) {
    if (!networkStackRecoveryPending || credentialPortalOnly || recoveryMode) return false;
    if (wifiAttemptActive || wifiConsecutiveFailures) return false;

    networkStackRecoveryPending = false;
    ++networkStackRecoveryCount;
    queueOledDiagnostic(OLED_DIAG_MQTT, "NETWORK SELF-HEAL",
                        "MQTT state -2 persisted; recycling Wi-Fi/TCP without reboot");

    // Release TLS and all HTTP/TCP state, then re-associate using the same
    // saved credentials/static IP. Meter/accounting state remains untouched.
    if (mqttClient.connected()) mqttClient.disconnect();
    mqttNet.stop();
    server.stop();
    networkServicesStarted = false;

    WiFi.disconnect(false);
    WiFi.mode(WIFI_OFF);
    delay(50);

    wifiWasConnected = false;
    wifiOfflineSinceMs = now;
    lastMqttReconnectMs = 0;
    mqttRetryStep = 0;
    mqttRetryDelayMs = MQTT_RUNTIME_RETRY_STEPS_MS[0];
    mqttTlsRecvConfiguredBytes = MQTT_TLS_RX_BYTES;
    resetMqttRuntimeFailureStreak();

    connectWiFiInitial(false);
    return true;
}

void serviceWiFiReconnect(unsigned long now) {
    servicePendingWiFiDisconnect();
    if (credentialPortalOnly || !ssid[0]) return;
    if (wifiTxScanning) {
        serviceWifiTxPrescan(now);
        return;
    }
    if (WiFi.status() == WL_CONNECTED) {
        // Also close automatic AP in OTA-only Recovery, where the ordinary
        // NetworkPriority loop is not entered. FLASH-owned AP is protected.
        if (credentialPortalActive && !credentialPortalOnly) {
            stopCredentialAccessPoint();
            if (credentialPortalActive) return; // retry AP close next loop
        }
        if (!wifiWasConnected) {
            lastMqttReconnectMs = 0;
            mqttRetryStep = 0;
            mqttRetryDelayMs = mqttEverConnected ? MQTT_RUNTIME_RETRY_STEPS_MS[0] : MQTT_RETRY_STEPS_MS[0];
        }
        wifiWasConnected = true;
        wifiAttemptActive = false;
        wifiAttemptFailedEvent = false;
        wifiConsecutiveFailures = 0;
        wifiFallbackActive = false;
        wifiFallbackFailureKind = 0;
        wifiFallbackReason = 0;
        wifiRetryStep = 0;
        wifiOfflineSinceMs = 0;
        wifiLastDisconnectReason = 0;
        wifiTxRetryFloor = 0;
        if ((uint32_t)(now - wifiTxLastAdjustedMs) >= WIFI_TX_REEVALUATE_MS) {
            const uint8_t desired = wifiTxLevelForRssi((int)WiFi.RSSI());
            if (desired != wifiTxCurrentLevel)
                (void)wifiTxApply(desired, "associated RSSI");
            else
                wifiTxLastAdjustedMs = now;
        }
        return;
    }
    if (wifiWasConnected) {
        wifiWasConnected = false;
        mqttNet.stop();
        wifiConsecutiveFailures = 0;
        wifiAttemptActive = false;
        startTrackedWiFiAttempt();
        return;
    }
    if (!wifiOfflineSinceMs) wifiOfflineSinceMs = now;
    if (wifiAttemptActive) {
        if (wifiAttemptFailedEvent) {
            finishWiFiAttemptFailure(1, wifiAttemptEventReason, (int)WiFi.status());
        } else if ((uint32_t)(now - wifiAttemptStartedMs) >= WIFI_ATTEMPT_TIMEOUT_MS) {
            finishWiFiAttemptFailure(2, 0, (int)WiFi.status());
        }
        return;
    }
    if ((uint32_t)(now - lastWifiRetry) >= WIFI_RETRY_STEPS_MS[wifiRetryStep]) startTrackedWiFiAttempt();
}

// Both millis() and digitalRead() are IRAM-safe in the pinned ESP32-C3 core.
// Latch a debounced PRESS+RELEASE even while a bounded TLS/Modbus call is busy.
// No Wi-Fi, display, allocation or flash operation is allowed in this ISR.
void ARDUINO_ISR_ATTR flashSetupButtonIsr() {
    const uint32_t now = (uint32_t)(esp_timer_get_time() / 1000ULL);
    if (gpio_get_level((gpio_num_t)FLASH_SETUP_PIN) == LOW) {
        if (!flashPressActive) { flashPressStartedMs = now; flashPressActive = true; }
    } else if (flashPressActive) {
        if ((uint32_t)(now - flashPressStartedMs) >= FLASH_DEBOUNCE_MS) flashSetupRequested = true;
        flashPressActive = false;
    }
}

void configureFlashSetupButton() {
#if PLATFORM_FLASH_BUTTON_ENABLED
    pinMode(FLASH_SETUP_PIN, INPUT_PULLUP);
    flashPressActive = false;
    flashSetupRequested = false;
    attachInterrupt(digitalPinToInterrupt(FLASH_SETUP_PIN), flashSetupButtonIsr, CHANGE);
#endif
}

void exitManualSetupAccessPoint() {
    if (recoveryMode || (!normalMeterInitialized && !PLATFORM_REQUIRE_ROUTER_BEFORE_APPLICATION) || !credentialPortalOnly) return;
    // Automatic first provisioning must not be dismissed into an empty STA.
    if (ssid[0] == '\0' || password[0] == '\0') {
        credentialApLastAction = "boot_setup_retained";
        return;
    }
    credentialApLastAction = "boot_exit_to_sta";
    credentialDns.stop();
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_OFF);
    credentialPortalActive = false;
    credentialApStateVerified = false;
    resetApWebSession();
    credentialPortalOnly = false;
    credentialApName = "";
    credentialApPassword = "";
    wifiWasConnected = false;
    wifiLastDisconnectReason = 0;
    wifiOfflineSinceMs = millis();
    networkServicesStarted = false;
    wifiHintVisible = false;
    wifiHintDrawPending = false;
    wifiHintStartedMs = millis();
    connectWiFiInitial(false);
    if (normalMeterInitialized && (!PLATFORM_REQUIRE_ROUTER_BEFORE_APPLICATION || WiFi.status() == WL_CONNECTED))
        applicationRenderMeasurements(lastMeterReadOk);
}

void serviceFlashSetupButton(unsigned long now) {
#if !PLATFORM_FLASH_BUTTON_ENABLED
    (void)now;
    return;
#endif
    if (!normalMeterInitialized && !recoveryMode && !PLATFORM_REQUIRE_ROUTER_BEFORE_APPLICATION) return;
    noInterrupts();
    const bool requested = flashSetupRequested;
    flashSetupRequested = false;
    interrupts();
    if (!requested) return;
    if (flashActionTaken && now - flashLastActionMs < FLASH_ACTION_COOLDOWN_MS) return;

    if (credentialRestartPending || forceRecoveryRestartPending) return;
    ++credentialApBootRequests;
    if (recoveryMode) {
        if (recoveryOtaInProgress || recoveryNormalRestartPending) return;
        // Retry/open setup without clearing the Recovery latch or enabling meters.
        if (!credentialPortalActive) startCredentialAccessPoint(true);
        showRecoveryScreen();
        credentialApLastAction = "boot_recovery_setup";
        flashActionTaken = true;
        flashLastActionMs = millis();
        return;
    }
    if (credentialPortalOnly) {
        exitManualSetupAccessPoint();
    } else if (wifiFallbackActive && credentialPortalActive) {
        // FLASH takes ownership: do not automatically close this manual AP.
        wifiFallbackActive = false;
        credentialPortalOnly = true;
        wifiAttemptActive = false;
        if (wifiTxScanning) {
            (void)esp_wifi_scan_stop();
            WiFi.scanDelete();
            wifiTxScanning = false;
        }
        WiFi.mode(WIFI_AP);
        credentialApLastAction = "manual_takeover";
    } else {
        // Manual setup pauses STA/MQTT and stays open until FLASH / restart.
        // Local reads/accounting remain enabled; saved credentials are untouched.
        mqttNet.stop();
        wifiWasConnected = false;
        networkServicesStarted = false;
        wifiFallbackActive = false;
        startCredentialAccessPoint(true);
        credentialApLastAction = "boot_open_setup";
        wifiHintVisible = true;
        wifiHintDrawPending = true;
        wifiHintStartedMs = millis();
    }
    flashActionTaken = true;
    flashLastActionMs = millis(); // Cooldown begins after the radio change finishes.
}

const char* wifiFailureLabel() {
    if (wifiLastDisconnectReason == WIFI_REASON_NO_AP_FOUND ||
        WiFi.status() == WL_NO_SSID_AVAIL) return "SSID=Not found";
    if (wifiLastDisconnectReason == WIFI_REASON_AUTH_FAIL ||
        wifiLastDisconnectReason == WIFI_REASON_802_1X_AUTH_FAILED) {
        return "WiFi auth failed";
    }
    if (wifiLastDisconnectReason == WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT ||
        wifiLastDisconnectReason == WIFI_REASON_HANDSHAKE_TIMEOUT) {
        return "WiFi handshake timeout";
    }
    return "WiFi offline";
}

void drawWiFiStatusHint(unsigned long now) {
    char line[32];
    platformDisplay.clear();
    platformDisplay.font(DisplayFont::Medium6x12);
    if (credentialPortalOnly) {
        platformDisplay.text(0, 12, credentialPortalActive ? "AP SETUP / LOCAL" : "SETUP AP retry");
        if (credentialPortalActive) {
            platformDisplay.font(DisplayFont::Tiny4x6); // Long JSY SSID still fits all 128 pixels.
            platformDisplay.text(0, 28, credentialApName.c_str());
            platformDisplay.font(DisplayFont::Small5x7);
            snprintf(line, sizeof(line), "WiFi open PIN:%s", AP_WEB_PIN);
            platformDisplay.text(0, 44, line);
            platformDisplay.font(DisplayFont::Tiny4x6);
            snprintf(line, sizeof(line), "AP: %s", WiFi.softAPIP().toString().c_str());
            platformDisplay.text(0, 60, line);
        } else {
            platformDisplay.font(DisplayFont::Small5x7);
            snprintf(line, sizeof(line), "Stage:%u err:%ld", (unsigned)credentialApLastCheckStage, (long)credentialApLastNativeError);
            platformDisplay.text(0, 28, line);
            platformDisplay.text(0, 44, "Local meter active");
            platformDisplay.text(0, 60, "FLASH: return to WiFi");
        }
    } else {
        platformDisplay.font(DisplayFont::Small5x7);
        platformDisplay.text(0, 12, wifiFallbackActive ? "AP+STA router retry" : wifiFailureLabel());
        const bool checkPassword =
            wifiLastDisconnectReason == WIFI_REASON_AUTH_FAIL ||
            wifiLastDisconnectReason == WIFI_REASON_802_1X_AUTH_FAILED ||
            wifiLastDisconnectReason == WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT ||
            wifiLastDisconnectReason == WIFI_REASON_HANDSHAKE_TIMEOUT;
        platformDisplay.text(0, 28, checkPassword ? "Check WiFi/password" : "Local meter active");
        const unsigned long age = now - lastWifiRetry;
        const unsigned long gap = WIFI_RETRY_STEPS_MS[wifiRetryStep];
        const unsigned long remaining = age >= gap ? 0 : gap - age;
        snprintf(line, sizeof(line), "Retry in %lus", (remaining + 999UL) / 1000UL);
        platformDisplay.text(0, 44, line);
        if (wifiFallbackActive && credentialPortalActive) {
            snprintf(line, sizeof(line), "AP: %s", WiFi.softAPIP().toString().c_str());
            platformDisplay.text(0, 60, line);
        } else {
            platformDisplay.text(0, 60, "FLASH: AP setup");
        }
    }
    platformDisplay.present();
}

void serviceWiFiStatusDisplay(unsigned long now, bool meterDisplayUpdated) {
    if (!normalMeterInitialized || recoveryMode || bootCheckpointsActive) return;
    if (oledDiagnosticCount) return; // A sticky failure takes precedence over hints.
    if (!credentialPortalOnly && WiFi.status() == WL_CONNECTED) {
        if (wifiHintVisible && !meterDisplayUpdated) applicationRenderMeasurements(lastMeterReadOk);
        wifiHintVisible = false;
        wifiHintDrawPending = false;
        wifiHintStartedMs = now;
        return;
    }

    if (wifiHintVisible && now - wifiHintStartedMs >= WIFI_HINT_DURATION_MS) {
        wifiHintVisible = false;
        wifiHintDrawPending = false;
        if (!meterDisplayUpdated) applicationRenderMeasurements(lastMeterReadOk);
        return;
    }
    if (!wifiHintVisible && now - wifiHintStartedMs >= WIFI_HINT_INTERVAL_MS) {
        wifiHintVisible = true;
        wifiHintDrawPending = true;
        wifiHintStartedMs = now;
    }
    if (wifiHintVisible && (wifiHintDrawPending || meterDisplayUpdated ||
        now - wifiHintLastFrameMs >= 1000UL)) {
        drawWiFiStatusHint(now);
        wifiHintLastFrameMs = millis();
        wifiHintDrawPending = false;
    }
}

void serviceOledDiagnostic(unsigned long now) {
    if (!oledDiagnosticCount || !bootOledReady) return;
    if (!oledDiagnosticStarted || now - oledDiagnosticLastFrameMs >= 1000UL) {
        if (!drawOledDiagnostic(now)) {
            if (recoveryMode) showRecoveryScreen();
            else if (credentialPortalOnly) drawWiFiStatusHint(now);
            else if (normalMeterInitialized) applicationRenderMeasurements(lastMeterReadOk);
        }
    }
}

// ==========================================================
// END MODULE: RECOVERY / WIFI / NETWORK
// ==========================================================

// Shared dashboard presentation and request lifecycle. Application modules
// contribute only their domain content, never a second page shell or footer.
bool applicationRenderWebContent(WiFiClient& client);
bool applicationWebLiveUpdates();

bool platformWebWriteHttpHeader(WiFiClient& client) {
    PGM_P header = PSTR(
        "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\n"
        "Cache-Control: no-store\r\nConnection: close\r\n\r\n");
    return writeRootProgmemViaRam(client, header, strlen_P(header));
}

bool platformWebWritePageStart(WiFiClient& client, PGM_P title) {
    PGM_P head = PSTR(
        "<!doctype html><html><head><meta charset='utf-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>");
    if (!writeRootProgmemViaRam(client, head, strlen_P(head)) ||
        !writeRootProgmemViaRam(client, title, strlen_P(title))) return false;
    PGM_P style = PSTR(
        "</title><style>"
        "body{font:22px/1.35 Arial,sans-serif;max-width:760px;margin:auto;padding:4px;color:#142431;background:#fff}"
        "h1,h2{font-size:34px;margin:0 0 6px}h3{font-size:inherit;margin:16px 0 6px}"
        ".dim{color:#64727c}pre{font:inherit;white-space:pre-wrap;overflow-wrap:anywhere;margin:6px 0}"
        ".utility-title{display:flex;flex-wrap:wrap;align-items:baseline;gap:6px 16px;margin-top:10px}"
        ".utility-title h3{margin:0}.utility-grid{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:12px;margin-top:0}"
        ".utility-grid div{overflow-wrap:anywhere}.table-wrap,.scroll{overflow-x:auto}"
        "table{border-collapse:collapse;width:100%;max-width:400px;font:inherit}"
        ".scroll table{max-width:none}canvas{max-width:100%;background:#fff}"
        "th,td{padding:5px 7px;border-bottom:1px solid #e5eaed;text-align:right;white-space:nowrap}"
        "th:first-child,td:first-child{text-align:left}a{margin-right:10px}p{margin:10px 0}"
        "button,input{font:inherit;padding:6px;max-width:100%;box-sizing:border-box}"
        "button{margin:4px 0}form{margin:9px 0}fieldset{margin:13px 0;min-width:0}"
        "@media(max-width:480px){.utility-grid{grid-template-columns:1fr}}"
        "</style></head><body><h2>");
    if (!writeRootProgmemViaRam(client, style, strlen_P(style)) ||
        !writeRootProgmemViaRam(client, title, strlen_P(title))) return false;
    PGM_P end = PSTR("</h2>");
    return writeRootProgmemViaRam(client, end, strlen_P(end));
}

bool platformWebWriteStatus(WiFiClient& client) {
    PGM_P links = PSTR(
        "<section class='platform-status'><p><a href='/network-debug'>Network Debug</a>"
        "<a href='/setup'>Network Setup</a></p>");
    if (!writeRootProgmemViaRam(client, links, strlen_P(links))) return false;
    char line[256];
    const int n = snprintf_P(line, sizeof(line),
        PSTR("<pre>Wi-Fi: %s\nMQTT: %s\nRX RSSI: %d dBm\nTX max: %.1f dBm (configured, not measured)\nAdaptive TX cap: 15.0 dBm\nFree heap: %lu bytes</pre></section>"),
        WiFi.status() == WL_CONNECTED ? "CONNECTED" : "OFFLINE",
        mqttClient.connected() ? "CONNECTED" : "DISCONNECTED",
        WiFi.status() == WL_CONNECTED ? (int)WiFi.RSSI() : 0,
        (double)wifiTxTenths() / 10.0, (unsigned long)ESP.getFreeHeap());
    return n > 0 && n < (int)sizeof(line) &&
        writeRootBytes(client, reinterpret_cast<const uint8_t*>(line), (size_t)n);
}

bool platformWebWriteFooter(WiFiClient& client, const char* dateTime, bool liveUpdates) {
    PGM_P footer = PSTR(
        "<p><a href='/force-ota-recovery-confirm'>Force OTA-ONLY Recovery...</a></p>"
        "<div class='dim'>Device: " DEVICE_ID " · Meter: " DEVICE_KIND "</div>");
    if (!writeRootProgmemViaRam(client, footer, strlen_P(footer))) return false;
    char line[192];
    int n = snprintf_P(line, sizeof(line), PSTR("<div class='dim'>MQTT ID: %s</div>"), mqttDeviceId);
    if (n <= 0 || n >= (int)sizeof(line) ||
        !writeRootBytes(client, reinterpret_cast<const uint8_t*>(line), (size_t)n)) return false;
    n = snprintf_P(line, sizeof(line),
        PSTR("<div id='device-time' class='dim'>ESP32-C3 date/time: %s</div>"), dateTime);
    if (n <= 0 || n >= (int)sizeof(line) ||
        !writeRootBytes(client, reinterpret_cast<const uint8_t*>(line), (size_t)n)) return false;
    // Poll only read-only application content. Ventilation forms must retain
    // operator edits until submission, so its application opts out of polling.
    if (liveUpdates) {
        PGM_P script = PSTR(
            "<script>(function(){"
            "const readings=document.getElementById('live-readings'),state=document.getElementById('live-state'),clock=document.getElementById('device-time');"
            "async function update(){const started=Date.now();"
            "if(!document.hidden){const controller=new AbortController();"
            "const timeout=setTimeout(()=>controller.abort(),4000);"
            "try{const response=await fetch('/live',{cache:'no-store',signal:controller.signal});"
            "const text=await response.text();"
            "if(!response.ok||!text.endsWith('<!--live-end-->'))throw new Error('incomplete');"
            "const stamp=text.match(/<!--device-time:([^<]*)-->/);if(!stamp)throw new Error('clock');readings.innerHTML=text.slice(0,-15).replace(stamp[0],'');clock.textContent='ESP32-C3 date/time: '+stamp[1];state.textContent='';"
            "}catch(error){state.textContent='Waiting for live measurements...';}"
            "finally{clearTimeout(timeout);}}"
            "setTimeout(update,Math.max(0,5000-(Date.now()-started)));}"
            "setTimeout(update,5000);})();</script>");
        if (!writeRootProgmemViaRam(client, script, strlen_P(script))) return false;
    }
    PGM_P end = PSTR("</body></html>");
    return writeRootProgmemViaRam(client, end, strlen_P(end));
}

void handleRoot() {
    WebResponseScope responseScope;
    if (!requireApWebPin()) return;
#if PLATFORM_REQUIRE_ROUTER_BEFORE_APPLICATION
    if (!normalMeterInitialized || credentialPortalOnly || WiFi.status() != WL_CONNECTED) {
        server.send(503, "text/plain; charset=utf-8", "Router connection required; application paused. Open /setup or /network-debug.");
        return;
    }
#endif
    const bool liveRequest = server.uri() == "/live";
    WiFiClient client = server.client();
    client.setTimeout(700);
        client.setConnectionTimeout(700);
    client.setNoDelay(true);
    ++webRootRequestCount;
    const unsigned long now = millis();
    if (webRootRequestCount > 1 && now - webRootLastRequestMs < WEB_ROOT_MIN_REQUEST_GAP_MS) {
        ++webRootRateLimitedCount;
        sendRootBusyAndClose(client,429,ESP.getFreeHeap(),platformMaxFreeBlock());
        return;
    }
    webRootLastRequestMs = now;
    uint32_t freeHeap = 0;
    uint32_t maxBlock = 0;
    if (!webRootHeapHealthy(&freeHeap, &maxBlock, nullptr) ||
        freeHeap < PLATFORM_WEB_MIN_HEAP_BYTES || maxBlock < PLATFORM_WEB_MIN_BLOCK_BYTES) {
        ++webRootBusyCount;
        markCrashStage(CRASH_STAGE_WEB_ROOT, freeHeap);
        sendRootBusyAndClose(client,503,freeHeap,maxBlock);
        return;
    }
    markCrashStage(CRASH_STAGE_WEB_ROOT, freeHeap);
    if (!platformWebWriteHttpHeader(client)) return;
    if (!liveRequest) {
        if (!platformWebWritePageStart(client, PSTR(DEVICE_LABEL " v" VERSION))) return;
        PGM_P start = PSTR("<div id='live-readings'>");
        if (!writeRootProgmemViaRam(client, start, strlen_P(start))) return;
    }
    if (!applicationRenderWebContent(client) || !platformWebWriteStatus(client)) return;
    char dateTime[32];
    formatCurrentDateTime(dateTime, sizeof(dateTime));
    if (liveRequest) {
        char line[80];
        const int n = snprintf_P(line, sizeof(line), PSTR("<!--device-time:%s-->"), dateTime);
        if (n <= 0 || n >= (int)sizeof(line) ||
            !writeRootBytes(client, reinterpret_cast<const uint8_t*>(line), (size_t)n)) return;
        PGM_P end = PSTR("<!--live-end-->");
        if (!writeRootProgmemViaRam(client, end, strlen_P(end))) return;
    } else {
        PGM_P end = PSTR("</div><div id='live-state' class='dim' aria-live='polite'></div>");
        if (!writeRootProgmemViaRam(client, end, strlen_P(end)) ||
            !platformWebWriteFooter(client, dateTime, applicationWebLiveUpdates())) return;
    }
    closeRootClient(client, true);
}

// BEGIN APPLICATION IMPLEMENTATION
// END APPLICATION IMPLEMENTATION

#if !PLATFORM_CUSTOM_APPLICATION
// Boiler v3.05 application only. Shared AP/Wi-Fi, Recovery, OTA,
// boot checkpoints and networking are original frozen CommonPlatform 1.04.
static const uint8_t btPins[4] = {5, 6, 7, 21};
static OneWire btWire1, btWire2, btWire3, btWire4;
static OneWire* const btWires[4] = {&btWire1, &btWire2, &btWire3, &btWire4};
static DallasTemperature btDS1(&btWire1), btDS2(&btWire2), btDS3(&btWire3), btDS4(&btWire4);
static DallasTemperature* const btSensors[4] = {&btDS1, &btDS2, &btDS3, &btDS4};
static DeviceAddress btAddresses[4] = {};
static float btTemps[4] = {NAN, NAN, NAN, NAN};
static bool btValid[4] = {};
static bool btAddressKnown[4] = {};
static bool btRequested[4] = {};
static uint8_t btFailures[4] = {};
static bool btInitialized = false;
static bool btConversionPending = false;
static uint32_t btCycleStartMs = 0;
static uint32_t btConversionStartMs = 0;
static const uint32_t BT_SAMPLE_INTERVAL_MS = 2500UL;
static const uint32_t BT_CONVERSION_WAIT_MS = 800UL;
static const uint8_t BT_MAX_CONSECUTIVE_FAILURES = 3;

// Four-probe Boiler archive: 96 x 15-minute averages for today/yesterday,
// plus 30 daily min/max/mean records for each physical thermometer.
// Missing/failed conversions contribute no samples; no stale values.
static const int16_t BH_EMPTY = -32768;
static const uint32_t BH_MAGIC = 0x42333035UL;
struct BhDaily {
    uint32_t dayKey;
    int16_t lo[4], hi[4];
    int32_t sum[4];
    uint16_t count[4];
};
struct BhArchive {
    uint32_t magic, seq, dayKey;
    uint16_t format, reserved;
    int16_t quarter[2][96][4];
    BhDaily daily[30]; // newest first
    uint32_t checksum;
};
static BhArchive bhData={},bhReadBuffer={};
static bool bhReady=false, bhStreaming=false;
static const char* BH_A="/boiler-hist-a.bin";
static const char* BH_B="/boiler-hist-b.bin";
static char bhTopic[96]={}, bhRange[10]={};
static uint8_t bhCursor=0;
static uint32_t bhLastPublish=0;
static int16_t bhActiveQuarter=-1;
static int32_t bhSum[4]={};
static uint16_t bhCount[4]={};
static bool bhQuarterDirty=false;

static uint32_t bhHash(const BhArchive& h) {
    uint32_t x=2166136261UL;
    const uint8_t* raw=reinterpret_cast<const uint8_t*>(&h);
    for(size_t i=0;i<offsetof(BhArchive,checksum);++i){x^=raw[i];x*=16777619UL;}
    return x;
}
static bool bhAccept(const BhArchive& h) {
    return h.magic==BH_MAGIC && h.format==1 && h.checksum==bhHash(h);
}
static void bhDailyBlank(BhDaily& d) {
    memset(&d,0,sizeof(d));
    for(uint8_t p=0;p<4;p++){d.lo[p]=BH_EMPTY;d.hi[p]=BH_EMPTY;}
}
static void bhNewArchive() {
    memset(&bhData,0,sizeof(bhData));
    bhData.magic=BH_MAGIC; bhData.format=1;
    for(uint8_t day=0;day<2;day++)
        for(uint8_t q=0;q<96;q++)
            for(uint8_t p=0;p<4;p++)bhData.quarter[day][q][p]=BH_EMPTY;
    for(uint8_t d=0;d<30;d++)bhDailyBlank(bhData.daily[d]);
}
static void bhLoad() {
    bhNewArchive();
    bhReady=littleFsReady;
    if(!bhReady)return;
    for(uint8_t idx=0;idx<2;idx++){
        const char* file=idx?BH_B:BH_A;
        File f=LittleFS.open(file,"r");
        if(!f)continue;
        const bool goodSize=f.size()==sizeof(BhArchive);
        const size_t count=goodSize?f.read((uint8_t*)&bhReadBuffer,sizeof(BhArchive)):0;
        f.close();
        if(count!=sizeof(BhArchive)||!bhAccept(bhReadBuffer))continue;
        if(!bhData.seq || (int32_t)(bhReadBuffer.seq-bhData.seq)>0)bhData=bhReadBuffer;
    }
}
static void bhSave() {
    if(!bhReady)return;
    runtimeBreadcrumb(RB_LITTLEFS_BEGIN,1);
    bhData.seq++;
    bhData.checksum=bhHash(bhData);
    File f=LittleFS.open((bhData.seq&1)?BH_A:BH_B,"w");
    if(!f) { runtimeBreadcrumb(RB_LITTLEFS_END,0); return; }
    const size_t wrote=f.write((const uint8_t*)&bhData,sizeof(BhArchive));
    f.flush();f.close();
    runtimeBreadcrumb(RB_LITTLEFS_END,wrote==sizeof(BhArchive)?1:0);
    if(wrote!=sizeof(BhArchive))return;
}
static int16_t bhFixed(float t) {
    return isfinite(t)&&t>-55.0f&&t<125.0f?(int16_t)lroundf(t*10.0f):BH_EMPTY;
}
static void bhUpdateQuarter(){
    if(bhActiveQuarter<0||bhActiveQuarter>=96)return;
    for(uint8_t p=0;p<4;p++)
        bhData.quarter[0][bhActiveQuarter][p]=bhCount[p]?
            (int16_t)lroundf((float)bhSum[p]/bhCount[p]):BH_EMPTY;
}
static bool bhAdjacent(uint32_t before,uint32_t after){
    return (before/400==after/400 && after%400==before%400+1) ||
           (before/400+1==after/400 && after%400==0 && before%400>=364);
}
static void bhNewDay(uint32_t dayKey){
    if(bhData.dayKey){
        bhUpdateQuarter();
        const bool following=bhAdjacent(bhData.dayKey,dayKey);
        if(following)memcpy(bhData.quarter[1],bhData.quarter[0],sizeof(bhData.quarter[0]));
        else for(uint8_t q=0;q<96;q++)
            for(uint8_t p=0;p<4;p++)bhData.quarter[1][q][p]=BH_EMPTY;
        memmove(&bhData.daily[1],&bhData.daily[0],29*sizeof(BhDaily));
    }
    bhDailyBlank(bhData.daily[0]);bhData.daily[0].dayKey=dayKey;
    bhData.dayKey=dayKey;
    for(uint8_t q=0;q<96;q++)
        for(uint8_t p=0;p<4;p++)bhData.quarter[0][q][p]=BH_EMPTY;
    bhActiveQuarter=-1;
    memset(bhSum,0,sizeof(bhSum));memset(bhCount,0,sizeof(bhCount));
    bhSave();
}
static void bhRecordSample() {
    if(!bhReady)return;
    const time_t t=time(nullptr);
    if(t<1700000000)return; // Wait for NTP/sane local RTC, avoid fictional dates.
    struct tm local={};
    if(!localtime_r(&t,&local))return;
    const uint32_t dayKey=(uint32_t)local.tm_year*400U+(uint32_t)local.tm_yday;
    const uint8_t quarter=local.tm_hour*4+local.tm_min/15;
    if(dayKey!=bhData.dayKey)bhNewDay(dayKey);
    if(bhActiveQuarter!=-1 && bhActiveQuarter!=quarter){
        bhUpdateQuarter();bhSave();
        memset(bhSum,0,sizeof(bhSum));memset(bhCount,0,sizeof(bhCount));
    }
    if(bhActiveQuarter!=quarter) {
        bhActiveQuarter=quarter;
        // Retain the prior persisted estimate if this quarter spans a reboot.
        for(uint8_t p=0;p<4;p++){
            const int16_t n=bhData.quarter[0][quarter][p];
            bhSum[p]=n==BH_EMPTY?0:n;bhCount[p]=n==BH_EMPTY?0:1;
        }
    }
    BhDaily& d=bhData.daily[0];
    for(uint8_t p=0;p<4;p++){
        if(!btValid[p]||btFailures[p])continue; // Actual successful DS18B20 read only.
        const int16_t n=bhFixed(btTemps[p]);
        if(n==BH_EMPTY)continue;
        bhSum[p]+=n;
        if(bhCount[p]<65535)bhCount[p]++;
        if(d.lo[p]==BH_EMPTY||n<d.lo[p])d.lo[p]=n;
        if(d.hi[p]==BH_EMPTY||n>d.hi[p])d.hi[p]=n;
        // 34,560 samples/day x 1250 deci-degrees fits signed 32-bit.
        d.sum[p]+=n;
        if(d.count[p]<65535)d.count[p]++;
    }
    bhUpdateQuarter();
}
static void bhNumber(int16_t n,char* dst,size_t cap) {
    if(n==BH_EMPTY)snprintf(dst,cap,"null");
    else snprintf(dst,cap,"%.1f",(double)n/10.0);
}
static void bhPublish(uint32_t now) {
    if(!bhStreaming||!mqttClient.connected()||now-bhLastPublish<90UL)return;
    bhLastPublish=now;
    const bool days=strcmp(bhRange,"days30")==0;
    const uint8_t cap=days?30:96;
    char out[900]={};
    if(bhCursor>=cap){
        snprintf(out,sizeof(out),"E|%s",bhRange);
        if(mqttClient.publish(bhTopic,out,false))bhStreaming=false;
        return;
    }
    int used=days?
        snprintf(out,sizeof(out),"D|%u",(unsigned)bhCursor):
        snprintf(out,sizeof(out),"R|%s|%u",bhRange,(unsigned)bhCursor);
    if(used<0||used>=(int)sizeof(out)){bhStreaming=false;return;}
    const uint8_t perFrame=days?3:8;
    for(uint8_t k=0;k<perFrame;k++){
        const unsigned idx=bhCursor+k;
        int n=0;
        if(days){
            const BhDaily& day=bhData.daily[idx];
            n=snprintf(out+used,sizeof(out)-used,"|%lu",(unsigned long)day.dayKey);
            if(n<0||n>=(int)(sizeof(out)-used)){bhStreaming=false;return;}used+=n;
            for(uint8_t p=0;p<4;p++){
                char lo[18],hi[18],avg[18];
                bhNumber(day.lo[p],lo,sizeof(lo));bhNumber(day.hi[p],hi,sizeof(hi));
                if(day.count[p])snprintf(avg,sizeof(avg),"%.1f",(double)day.sum[p]/(10.0*day.count[p]));
                else strcpy(avg,"null");
                n=snprintf(out+used,sizeof(out)-used,",%s,%s,%s",lo,hi,avg);
                if(n<0||n>=(int)(sizeof(out)-used)){bhStreaming=false;return;}used+=n;
            }
        }else{
            n=snprintf(out+used,sizeof(out)-used,"|");
            if(n<0||n>=(int)(sizeof(out)-used)){bhStreaming=false;return;}used+=n;
            const uint8_t day=strcmp(bhRange,"previous")==0?1:0;
            for(uint8_t p=0;p<4;p++){
                char val[18];bhNumber(bhData.quarter[day][idx][p],val,sizeof(val));
                n=snprintf(out+used,sizeof(out)-used,"%s%s",p?",":"",val);
                if(n<0||n>=(int)(sizeof(out)-used)){bhStreaming=false;return;}used+=n;
            }
        }
    }
    if(mqttClient.publish(bhTopic,out,false))bhCursor+=perFrame;
}


static void btFailure(uint8_t index) {
    if (btFailures[index] < 255) ++btFailures[index];
    if (btFailures[index] >= BT_MAX_CONSECUTIVE_FAILURES) {
        btValid[index] = false;
        btTemps[index] = NAN;
        btAddressKnown[index] = false;  // Rediscover the ROM on the next cycle.
    }
}
static void btSample(uint32_t now) {
    if (!btInitialized || recoveryMode || credentialPortalOnly) return;
    if (!btConversionPending) {
        if ((uint32_t)(now - btCycleStartMs) < BT_SAMPLE_INTERVAL_MS) return;
        btCycleStartMs = now;
        for (uint8_t i = 0; i < 4; ++i) {
            if (!btAddressKnown[i]) {
                btAddressKnown[i] = btSensors[i]->getAddress(btAddresses[i], 0);
                if (btAddressKnown[i]) btSensors[i]->setResolution(btAddresses[i], 12);
            }
            btRequested[i] = btAddressKnown[i] &&
                btSensors[i]->requestTemperaturesByAddress(btAddresses[i]);
            if (!btRequested[i]) btFailure(i);
        }
        btConversionStartMs = millis();
        btConversionPending = true;
        return;
    }
    if ((uint32_t)(now - btConversionStartMs) < BT_CONVERSION_WAIT_MS) return;
    btConversionPending = false;
    for (uint8_t i = 0; i < 4; ++i) {
        if (!btRequested[i]) continue;
        const float reading = btSensors[i]->getTempC(btAddresses[i]);
        const bool good = isfinite(reading) && reading > -55.0f && reading < 125.0f;
        if (!good) { btFailure(i); continue; }
        btTemps[i] = reading;
        btValid[i] = true;
        btFailures[i] = 0;
    }
    bhRecordSample();
}
static void btAsJson(uint8_t i, char* out, size_t capacity) {
    if (btValid[i]) snprintf(out, capacity, "%.1f", (double)btTemps[i]);
    else snprintf(out, capacity, "null");
}

// Callbacks called by the original CommonPlatform 1.04 testedOK lifecycle.
// Important: applicationInitialize() is NOT called on an unconfigured AP boot.
void applicationInitialize() {
    if (btInitialized) return;
    for (uint8_t i = 0; i < 4; ++i) {
        btWires[i]->begin(btPins[i]);
        btSensors[i]->begin();
        btSensors[i]->setWaitForConversion(false);
    }
    bhLoad();
    btInitialized = true;
    btCycleStartMs = millis() - BT_SAMPLE_INTERVAL_MS;
}
void applicationInitializeTopics() {
    snprintf(bhTopic,sizeof(bhTopic),MQTT_TOPIC_PREFIX "%s/boiler/history",mqttDeviceId);
}
bool applicationReadMeasurements() {
    btSample(millis());
    // An absent probe is a per-sensor state, not a platform crash.
    return true;
}
bool applicationDualZoneEnabled() { return false; }
void applicationBackground(unsigned long now) { btSample(now); bhPublish(now); }
bool applicationStageCommand(const String& cmd) {
    if(!cmd.startsWith("boiler_history|"))return false;
    const String range=cmd.substring(strlen("boiler_history|"));
    if(range!="current"&&range!="previous"&&range!="days30")return true;
    range.toCharArray(bhRange,sizeof(bhRange));
    bhCursor=0;bhLastPublish=millis()-90UL;bhStreaming=bhReady;
    return true;
}
bool applicationProcessCommand(MqttAdminCommand) { return false; }
void applicationResetCommand() {}
bool applicationAppendDebug(WiFiClient&) { return true; }
void applicationFormatSummary(char* out, size_t capacity) {
    snprintf(out, capacity, "BOILER 4xDS18B20");
}
void applicationRenderMeasurements(bool) {
    // This callback is never invoked by the tested platform's initial Setup
    // AP-only loop; the platform keeps its own AP screen, PIN and URL.
    if (drawOledDiagnostic(millis())) return;
    if (credentialPortalOnly) { drawWiFiStatusHint(millis()); return; }
    platformDisplay.clear();
    platformDisplay.font(DisplayFont::Medium6x12);
    for (uint8_t i = 0; i < 4; ++i) {
        char line[32];
        if (!btValid[i]) {
            snprintf(line, sizeof(line), "TH%u G%u: DISCONNECTED",
                     (unsigned)(i + 1), (unsigned)btPins[i]);
        } else {
            snprintf(line, sizeof(line), "TH%u G%u: %.1f%sC",
                     (unsigned)(i + 1), (unsigned)btPins[i],
                     (double)btTemps[i], btFailures[i] ? "~" : " ");
        }
        platformDisplay.text(0, (i + 1) * 12 - 1, line);
    }
    platformDisplay.present();
}
bool publishMqttSample() {
    if (!mqttClient.connected()) return false;
    char t[4][20];
    for (uint8_t i = 0; i < 4; ++i) btAsJson(i, t[i], sizeof(t[i]));
    char stamp[32] = {};
    formatCurrentDateTime(stamp, sizeof(stamp));
    const char* clock = strlen(stamp) > 11 ? stamp + 11 : "00:00:00";
    const bool allOk = btValid[0] && btValid[1] && btValid[2] && btValid[3];
    char payload[900];
    const int n = snprintf(payload, sizeof(payload),
        "{\"meter\":\"BOILER\",\"device_type\":\"BOILER\","
        "\"device\":\"%s\",\"firmware\":\"%s\","
        "\"temp_inside\":%s,\"temp_outside\":%s,\"temp_middle\":%s,"
        "\"temp_1\":%s,\"temp_2\":%s,\"temp_3\":%s,\"temp_4\":%s,"
        "\"temp_1_status\":\"%s\",\"temp_2_status\":\"%s\","
        "\"temp_3_status\":\"%s\",\"temp_4_status\":\"%s\","
        "\"humidity_inside\":null,\"humidity_outside\":null,"
        "\"dht_temp_inside\":null,\"dht_temp_outside\":null,"
        "\"fan_on\":false,\"speed\":\"low\",\"mode\":\"auto\",\"season\":\"winter\","
        "\"time\":\"%.8s\",\"sensors_ok\":%s,\"ota_supported\":true,\"rssi\":%d,"
        "\"wifi_rssi_dbm\":%d,\"wifi_tx_max_dbm\":%.1f}",
        mqttDeviceId, VERSION,
        t[2], t[0], t[1], t[0], t[1], t[2], t[3],
        btValid[0] ? (btFailures[0] ? "retrying" : "connected") : "not connected",
        btValid[1] ? (btFailures[1] ? "retrying" : "connected") : "not connected",
        btValid[2] ? (btFailures[2] ? "retrying" : "connected") : "not connected",
        btValid[3] ? (btFailures[3] ? "retrying" : "connected") : "not connected",
        clock, allOk ? "true" : "false", (int)WiFi.RSSI(),
        WiFi.status() == WL_CONNECTED ? (int)WiFi.RSSI() : 0,
        (double)wifiTxTenths() / 10.0);
    return n > 0 && n < (int)sizeof(payload) &&
           mqttClient.publish(MQTT_STATE_TOPIC, payload, false);
}
bool applicationWebLiveUpdates() { return true; }
bool applicationRenderWebContent(WiFiClient& client) {
    static const char intro[] = "<h3>Boiler temperatures</h3><p><a href='/charts?view=24'>Γραφήματα θ1–θ4 · 24 ώρες / 48 ώρες / 30 ημέρες</a></p>";
    if (!writeRootBytes(client, reinterpret_cast<const uint8_t*>(intro),
                        sizeof(intro) - 1)) return false;
    for (uint8_t i = 0; i < 4; ++i) {
        char line[140] = {};
        if (btValid[i]) {
            snprintf(line, sizeof(line), "<p>TH%u (GPIO%u): %.1f &deg;C%s</p>",
                     (unsigned)(i + 1), (unsigned)btPins[i],
                     (double)btTemps[i], btFailures[i] ? " (last valid)" : "");
        } else {
            snprintf(line, sizeof(line), "<p>TH%u (GPIO%u): not connected</p>",
                     (unsigned)(i + 1), (unsigned)btPins[i]);
        }
        if (!writeRootBytes(client, reinterpret_cast<const uint8_t*>(line),
                            strlen(line))) return false;
    }
    return true;
}

// Local charts use the existing four-probe archive (no separate history format).
// Keep the shared CommonPlatform 1.05 shell and normal restart functionality.
void boilerChartsPage() {
    WebResponseScope responseScope;
    if (!requireApWebPin()) return;
    if (!btInitialized || !bhReady) {
        server.send(503, "text/plain; charset=utf-8", "Boiler history is not initialized");
        return;
    }
    WiFiClient client = server.client();
    if (!platformWebWriteHttpHeader(client) ||
        !platformWebWritePageStart(client, PSTR("Boiler - \xce\xb81 / \xce\xb82 / \xce\xb83 / \xce\xb84"))) return;
    static const char page[] = R"BOILERCHART(
<nav><a href="/">Αρχική</a><a href="/charts?view=24">24 ώρες</a><a href="/charts?view=48">48 ώρες</a><a href="/charts?view=30">30 ημέρες</a></nav>
<div class="scroll"><canvas id="graph" width="1060" height="525" role="img" aria-label="Γράφημα θερμοκρασιών"></canvas></div>
<p id="legend">θ1 <span style="color:#dc2626">━━</span> · θ2 <span style="color:#2563eb">━━</span> · θ3 <span style="color:#16a34a">━━</span> · θ4 <span style="color:#9333ea">━━</span> · Σήμερα συνεχής / Χθες διακεκομμένη · °C</p>
<div id="readings" class="scroll">Φόρτωση καταγραφών…</div>
<script>
(async function(){
const params=new URLSearchParams(location.search),view=['24','48','30'].includes(params.get('view'))?params.get('view'):'24';
const holder=document.getElementById('readings'),g=document.getElementById('graph').getContext('2d');
const colors=['#dc2626','#2563eb','#16a34a','#9333ea'];
const fmt=v=>v===null||!Number.isFinite(v)?'—':v.toLocaleString('el-GR',{minimumFractionDigits:1,maximumFractionDigits:1});
const date=k=>{if(!k)return '—';const year=Math.floor(k/400)+1900,day=k%400;return new Date(Date.UTC(year,0,1)+day*86400000).toISOString().slice(0,10)};
const time=i=>String(Math.floor(i/4)).padStart(2,'0')+':'+String((i%4)*15).padStart(2,'0');
try {
 const response=await fetch('/charts-data?view='+view,{cache:'no-store'});
 if(!response.ok)throw Error('HTTP '+response.status);
 const data=await response.json(),today=data.today||[],previous=data.previous||[],days=data.days||[];
 const series=[];
 if(view==='30'){
   for(let p=0;p<4;p++) {
      series.push({points:days.map(d=>d[p*3+3]),color:colors[p],dash:[]});
      series.push({points:days.map(d=>d[p*3+1]),color:colors[p],dash:[4,4]});
      series.push({points:days.map(d=>d[p*3+2]),color:colors[p],dash:[4,4]});
   }
   document.getElementById('legend').append(' · 30 ημέρες: μέση συνεχής, min/max διακεκομμένες');
 }else{
   for(let p=0;p<4;p++){
     series.push({points:today.map(d=>d[p]),color:colors[p],dash:[]});
     if(view==='48')series.push({points:previous.map(d=>d[p]),color:colors[p],dash:[7,5]});
   }
 }
 const values=series.flatMap(s=>s.points).filter(Number.isFinite);
 const L=65,T=30,W=960,H=440;g.clearRect(0,0,1060,525);g.font='12px Arial';g.textAlign='right';
 if(values.length){
  let lo=Math.floor(Math.min(...values)*2)/2,hi=Math.ceil(Math.max(...values)*2)/2;
  if(lo===hi){lo-=.5;hi+=.5}
  const y=v=>T+H*(hi-v)/(hi-lo),x=i=>L+W*i/(view==='30'?29:96);
  for(let half=Math.round(lo*2);half<=Math.round(hi*2);half++){
   const v=half/2,Y=y(v);g.strokeStyle='#dde3e8';g.setLineDash([]);g.lineWidth=1;
   g.beginPath();g.moveTo(L,Y);g.lineTo(L+W,Y);g.stroke();g.fillStyle='#374151';g.fillText(fmt(v),L-7,Y+3);
  }
  g.textAlign='center';
  if(view==='30'){
    for(let i=0;i<30;i+=3){const X=x(i);g.fillStyle='#374151';g.fillText(date(days[i]?.[0]).slice(5),X,T+H+22)}
  }else for(let h=0;h<=24;h++){
    const X=x(h*4);g.strokeStyle='#d6dde4';g.beginPath();g.moveTo(X,T);g.lineTo(X,T+H);g.stroke();
    if(h<24){g.fillStyle='#374151';g.fillText(String(h).padStart(2,'0'),X,T+H+22)}
  }
  for(const s of series){
    g.strokeStyle=s.color;g.lineWidth=2.3;g.setLineDash(s.dash);g.beginPath();let started=false;
    s.points.forEach((v,i)=>{if(!Number.isFinite(v)){started=false;return}const X=x(i);started?g.lineTo(X,y(v)):g.moveTo(X,y(v));started=true});g.stroke();
  }
  g.setLineDash([]);
 }else{g.fillStyle='#374151';g.fillText('Δεν υπάρχουν διαθέσιμες καταγραφές',L+20,T+50)}
 const labels=['θ1','θ2','θ3','θ4'];
 const head=view==='30'?['Ημερομηνία',...labels.flatMap(n=>[n+' min °C',n+' max °C',n+' μέση °C'])]:
 ['Ώρα',...labels.map(n=>n+' σήμερα °C'),...(view==='48'?labels.map(n=>n+' χθες °C'):[])];
 const rows=view==='30'?days.map(d=>[date(d[0]),...d.slice(1).map(fmt)]):
 Array.from({length:96},(_,i)=>[time(i),...(today[i]||[null,null,null,null]).map(fmt),...(view==='48'?(previous[i]||[null,null,null,null]).map(fmt):[])]);
 holder.replaceChildren();
 const table=document.createElement('table'),thead=table.createTHead(),hr=thead.insertRow();
 head.forEach(n=>{const th=document.createElement('th');th.textContent=n;hr.append(th)});
 const tbody=table.createTBody();
 rows.forEach(r=>{const tr=tbody.insertRow();r.forEach(v=>{const td=tr.insertCell();td.textContent=v})});
 holder.append(table);
}catch(e){holder.textContent='Αδυναμία φόρτωσης ιστορικού: '+e.message}
})();
</script>
)BOILERCHART";
    if (!writeRootBytes(client, reinterpret_cast<const uint8_t*>(page), sizeof(page)-1)) return;
    const char end[] = "</body></html>";
    writeRootBytes(client, reinterpret_cast<const uint8_t*>(end), sizeof(end)-1);
    closeRootClient(client, true);
}
void boilerChartsData() {
    WebResponseScope responseScope;
    if (!requireApWebPin()) return;
    if (!btInitialized || !bhReady) {
        server.send(503, "application/json", "{\"error\":\"history_unavailable\"}");
        return;
    }
    String view=server.arg("view");
    if(view!="24"&&view!="48"&&view!="30")view="24";
    WiFiClient client=server.client();
    static const char header[]="HTTP/1.1 200 OK\r\nContent-Type: application/json; charset=utf-8\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n";
    if(!writeRootBytes(client,(const uint8_t*)header,sizeof(header)-1))return;
    if(!client.print("{\"today\":["))return;
    if(view!="30")for(uint8_t q=0;q<96;q++){
        if(q&&!client.print(","))return;
        if(!client.print("["))return;
        for(uint8_t p=0;p<4;p++){
            if(p&&!client.print(","))return;
            char v[20];bhNumber(bhData.quarter[0][q][p],v,sizeof(v));
            if(!client.print(v))return;
        }
        if(!client.print("]"))return;
        if((q&7)==7)yield();
    }
    if(!client.print("],\"previous\":["))return;
    if(view=="48")for(uint8_t q=0;q<96;q++){
        if(q&&!client.print(","))return;
        if(!client.print("["))return;
        for(uint8_t p=0;p<4;p++){
            if(p&&!client.print(","))return;
            char v[20];bhNumber(bhData.quarter[1][q][p],v,sizeof(v));
            if(!client.print(v))return;
        }
        if(!client.print("]"))return;
        if((q&7)==7)yield();
    }
    if(!client.print("],\"days\":["))return;
    if(view=="30")for(uint8_t i=0;i<30;i++){
        if(i&&!client.print(","))return;
        const BhDaily &d=bhData.daily[29-i]; // chronological left to right
        if(!client.printf("[%lu",(unsigned long)d.dayKey))return;
        for(uint8_t p=0;p<4;p++){
            char lo[20],hi[20],mean[24];
            bhNumber(d.lo[p],lo,sizeof(lo));bhNumber(d.hi[p],hi,sizeof(hi));
            if(d.count[p])snprintf(mean,sizeof(mean),"%.1f",(double)d.sum[p]/(10.0*d.count[p]));
            else strcpy(mean,"null");
            if(!client.printf(",%s,%s,%s",lo,hi,mean))return;
        }
        if(!client.print("]"))return;
        if((i&3)==3)yield();
    }
    client.print("]}");
    closeRootClient(client,true);
}

#endif

// First USB installation leaves LittleFS erased. Never auto-format a
// nonempty/corrupt partition: retain its settings for manual recovery.
bool platformMountLittleFs() {
    if (LittleFS.begin(false, "/littlefs", 10, "spiffs")) return true;
    const esp_partition_t* partition = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, "spiffs");
    if (!partition) return false;
    uint8_t block[256];
    for (size_t offset = 0; offset < partition->size; offset += sizeof(block)) {
        const size_t length = std::min(sizeof(block), (size_t)partition->size - offset);
        if (esp_partition_read(partition, offset, block, length) != ESP_OK) return false;
        for (size_t i = 0; i < length; ++i) if (block[i] != 0xFF) return false;
        yield();
    }
    return LittleFS.format() && LittleFS.begin(false, "/littlefs", 10, "spiffs");
}

// ==========================================================
// MODULE: MAIN SETUP / LOOP
// ==========================================================

// =====================================================
// SETUP / LOOP EXECUTION MODEL
//
// Router-first opt-in profiles defer display and application hardware until
// station connection + HTTP startup. The default local-first policy follows:
// setup():
//   reset forensics -> OLED/button -> LittleFS/settings -> accounting/history
//   -> selected meter driver -> Web/MQTT configuration -> non-blocking Wi-Fi.
//
// loop():
//   1. due physical meter read + accounting + OLED
//   2. setup/recovery controls and Wi-Fi maintenance
//   3. Web -> MQTT -> Web network service
//   4. queued MQTT response/admin work
//   5. health/history background work and diagnostics
//
// Η σειρά είναι σκόπιμη: το όργανο πρέπει να συνεχίζει να μετρά ακόμη και
// όταν router, DNS, broker ή TLS δεν είναι διαθέσιμα.
// =====================================================
void platformInitializeDisplay() {
    if (bootOledReady) return;
    bootCheckpoint(CRASH_STAGE_BOOT_OLED_INIT);
    platformDisplay.begin();
    bootOledReady = true;
#if OLED_TYPE == 2 || OLED_TYPE == 3
    platformDisplay.contrast(255);
#endif
}

bool serviceRouterFirstApplication(unsigned long now) {
    (void)now;
#if PLATFORM_REQUIRE_ROUTER_BEFORE_APPLICATION
    if (recoveryMode || credentialPortalOnly || WiFi.status() != WL_CONNECTED) return false;
    if (normalMeterInitialized) return true;
    // Bring up the platform's HTTP/debug endpoints before touching peripherals.
    startNetworkServices();
    server.handleClient();
    if (credentialRestartPending || forceRecoveryRestartPending) return false;
    platformInitializeDisplay();
#if PLATFORM_LOCAL_CONTROL_APPLICATION
    applicationEarlyInitialize();
#endif
    bootCheckpoint(CRASH_STAGE_BOOT_SERIAL);
    applicationInitialize();
#if PLATFORM_LOCAL_CONTROL_APPLICATION
    applicationConfigureWebRoutes();
#endif
    normalMeterInitialized = true;
    bootFirstMeterDone = false;
#endif
    return true;
}

void setup() {
#if PLATFORM_LOCAL_CONTROL_APPLICATION && !PLATFORM_REQUIRE_ROUTER_BEFORE_APPLICATION
    // GPIO actuators must be safe before display, filesystem or network setup.
    applicationEarlyInitialize();
#endif
    // Read the SDK reset structure before any network or sensor startup.
    captureCurrentReset();
    loadRtcCrashTrace();
    loadRtcRuntimeBreadcrumbs();
    loadOledDiagnostic();
    initializeMqttIdentity();
    const bool externalReset = (currentBootReset.reason == ESP_RST_EXT);
    const bool hardwareOrWatchdogFault = isFaultReset(currentBootReset.reason);

#if !PLATFORM_REQUIRE_ROUTER_BEFORE_APPLICATION
    platformInitializeDisplay();
#endif
    configureFlashSetupButton(); // FLASH also works in Recovery and setup-only boot.

    // Start LittleFS early so an incident can be latched across power cycles.
    // Existing v1.20 energy/tariff/daily EEPROM addresses are untouched.
    bootCheckpoint(CRASH_STAGE_BOOT_FILESYSTEM);
    littleFsReady = platformMountLittleFs();
    if (!littleFsReady) queueOledDiagnostic(OLED_DIAG_FILESYSTEM, "LITTLEFS FAILED", "Settings/history storage could not be mounted");

    // Credentials are stored in one validated LittleFS record. Meter profile
    // metadata is no longer part of credential loading.
    bool deviceCredentialsReady = false;
    if (littleFsReady) {
        bootCheckpoint(CRASH_STAGE_BOOT_CREDENTIALS);
#if PLATFORM_LOCAL_CONTROL_APPLICATION
        if (!LittleFS.exists(DEVICE_CREDENTIAL_FILE)) applicationImportLegacyCredentials();
#endif
        deviceCredentialsReady = loadDeviceCredentials();
        if (!deviceCredentialsReady) queueOledDiagnostic(OLED_DIAG_CREDENTIALS, "SAVED SETTINGS INVALID", "Settings missing or invalid; use setup AP");
    }

    bootCheckpoint(CRASH_STAGE_BOOT_RECOVERY_CHECK);
    bool hasPreviousIncident = loadRecoveryIncident();

    if (externalReset) {
        bootCheckpoint(CRASH_STAGE_BOOT_RECOVERY_CLEAR);
        // External RST is one explicit way out of a crash recovery latch.
        // The Recovery-only /restart-normal action and successful OTA can
        // also clear the persistent incident deliberately.
        bool clearSucceeded = true;
        if (littleFsReady && LittleFS.exists(RECOVERY_FILE)) {
            clearSucceeded = LittleFS.remove(RECOVERY_FILE);
        }
        if (clearSucceeded) {
            recoveryIncident = RecoveryRecord();
            recoveryIncidentPersistent = false;
            hasPreviousIncident = false;
            recoveryMode = false;
        } else {
            // Fail closed on filesystem deletion errors: a stale fault
            // record must not silently unlock normal metering.
            recoveryResetClearFailed = true;
            queueOledDiagnostic(OLED_DIAG_RECOVERY_CLEAR, "RECOVERY CLEAR FAILED", "Stored Recovery could not be removed");
            recoveryMode = hasPreviousIncident;
        }
    } else if (hardwareOrWatchdogFault && !hasPreviousIncident) {
        recoveryIncident = currentBootReset;
        recoveryMode = true;
        saveRecoveryIncident();
    } else if (hasPreviousIncident) {
        recoveryMode = true;
    }

    if (recoveryMode) {
        // Fail closed: no meter, MQTT, EEPROM accounting or normal admin
        // initialization. Only LCD, Wi-Fi, OTA and read-only HTTP debug.
        showRecoveryScreen();
        configureRecoveryRoutes();
        if (deviceCredentialsReady) {
            connectWiFiInitial(false);
            if (WiFi.status() == WL_CONNECTED) startNetworkServices();
        } else {
            startCredentialAccessPoint(true);
        }
        showRecoveryScreen();
        finishBootCheckpoints(CRASH_STAGE_BOOT_RECOVERY_IDLE);
        return;
    }

#if PLATFORM_LOCAL_CONTROL_APPLICATION && !PLATFORM_REQUIRE_ROUTER_BEFORE_APPLICATION
    // Local automation also runs without saved Wi-Fi credentials, in setup AP.
    applicationInitialize();
    normalMeterInitialized = true;
#endif
    if (!deviceCredentialsReady) {
        configureProvisioningRoutes();
        startCredentialAccessPoint(true);
        finishBootCheckpoints(credentialPortalActive ? CRASH_STAGE_BOOT_AP_IDLE : CRASH_STAGE_BOOT_AP_FAILED);
        return;
    }

    markCrashStage(CRASH_STAGE_SETUP, ESP.getFreeHeap());
    cs();
    doublePrintf(420, 1, 1, "Initialize...");
    doublePrintf(420, 2, 1, "Ver: %s", VERSION);
    doublePrintf(420, 3, 1, "Date: %s", BUILD_DATE);
    // Checkpoints replace the old five-second cosmetic startup delay.

#if !PLATFORM_LOCAL_CONTROL_APPLICATION && !PLATFORM_REQUIRE_ROUTER_BEFORE_APPLICATION
    applicationInitialize();
#endif

    bootCheckpoint(CRASH_STAGE_BOOT_WEB_CONFIG);
    configureWebRoutes();
    bootCheckpoint(CRASH_STAGE_BOOT_MQTT_CONFIG);
    configureMqtt();
#if !PLATFORM_REQUIRE_ROUTER_BEFORE_APPLICATION
    normalMeterInitialized = true;
#endif

    markCrashStage(CRASH_STAGE_WIFI_CONNECT, ESP.getFreeHeap());
    connectWiFiInitial(false);

    if (WiFi.status() == WL_CONNECTED) {
        bootCheckpoint(CRASH_STAGE_NETWORK_SERVICES);
        startNetworkServices();
        doublePrintf(420, 3, 1, "Wifi connected!");
        const IPAddress ip = WiFi.localIP();
        char ipText[16];
        snprintf(ipText, sizeof(ipText), "%u.%u.%u.%u",
                 ip[0], ip[1], ip[2], ip[3]);
        doublePrintf(420, 4, 1, "%s", ipText);
    } else {
        doublePrintf(420, 3, 1, "Offline / local meter");
        wifiOfflineSinceMs = millis();
    }

    bootCheckpoint(CRASH_STAGE_BOOT_LOOP_ENTER);
}

// Retry indefinitely without reboot while router/Internet is unavailable.

// =====================================================
// NETWORK PRIORITY SERVICE
// Προτεραιότητα: Web -> MQTT -> Web ξανά.
// Καλείται περισσότερες από μία φορές ανά loop και πάντα
// μετά από την τοπική μέτρηση που είναι ήδη due.
// =====================================================
void serviceNetworkPriority(unsigned long now) {
    if (credentialPortalOnly || WiFi.status() != WL_CONNECTED) return;
    // A router may return long after an offline boot. Diagnose that FIRST
    // MQTT attempt too, rather than lose it when local startup already finished.
    if (!bootFirstMqttAttempted && mqttSettingsReady()) bootCheckpointsActive = true;

    if (credentialPortalActive && !credentialPortalOnly) {
        stopCredentialAccessPoint();
    }

    bootCheckpoint(CRASH_STAGE_NETWORK_SERVICES);
    startNetworkServices();

    // 1. Web πρώτα, ώστε η σελίδα να απαντά όσο πιο άμεσα γίνεται.
    bootCheckpoint(CRASH_STAGE_BOOT_FIRST_WEB);
    runtimeBreadcrumb(RB_WEB_BEGIN);
    server.handleClient();
    runtimeBreadcrumb(RB_WEB_END);
    if (credentialRestartPending) return;

    // 2. MQTT αμέσως μετά: keepalive, incoming requests, reconnect.
    runtimeBreadcrumb(RB_MQTT_BEGIN);
    maintainMqtt(now);
    runtimeBreadcrumb(RB_MQTT_END);

    // 3. Δεύτερη ευκαιρία στο Web μετά από πιθανό MQTT reconnect.
    runtimeBreadcrumb(RB_WEB_BEGIN);
    server.handleClient();
    runtimeBreadcrumb(RB_WEB_END);
    if (credentialRestartPending) return;

}

void loop() {
    runtimeBreadcrumb(RB_LOOP_BEGIN);
    unsigned long now = millis();
    if (serviceCredentialRestart(now)) return;

    if (mqttAdminRestartPending &&
        now - mqttAdminRestartRequestedMs >= MQTT_ADMIN_RESTART_DELAY_MS) {
        mqttAdminRestartPending = false;
        if (mqttClient.connected()) mqttClient.disconnect();
        mqttNet.stop();
        delay(20);
#if PLATFORM_LOCAL_CONTROL_APPLICATION
        applicationPrepareOta();
#endif
        ESP.restart();
        return;
    }

    if (forceRecoveryRestartPending &&
        now - forceRecoveryRestartRequestedMs >= FORCE_RECOVERY_RESTART_DELAY_MS) {
        delay(20);
        ESP.restart();
        return;
    }

    if (recoveryMode) {
        serviceFlashSetupButton(now);
        if (credentialPortalActive || credentialPortalOnly) serviceCredentialPortal(now);
        serviceRecoveryOnly(now);
        serviceOledDiagnostic(millis());
        return;
    }

#if PLATFORM_REQUIRE_ROUTER_BEFORE_APPLICATION
    // Setup/reconnection is a platform-only loop. No OLED/sensor/history work
    // runs until the router has supplied a usable station connection.
    serviceFlashSetupButton(now);
    if (credentialPortalActive || credentialPortalOnly) serviceCredentialPortal(now);
    serviceWiFiReconnect(millis());
    if (!serviceRouterFirstApplication(millis())) {
        serviceOledDiagnostic(millis());
        yield();
        return;
    }
#endif

    if (credentialPortalOnly && !normalMeterInitialized) {
        serviceCredentialPortal(now);
        serviceOledDiagnostic(millis());
        yield();
        return;
    }

#if PLATFORM_LOCAL_CONTROL_APPLICATION
    applicationServiceLocal(now);
#endif

    bool meterDisplayUpdated = false;
    // LOCAL METER FIRST: neither a missing router nor a slow broker may defer
    // the first read/display behind a synchronous network connection attempt.
    if (!bootFirstMeterDone || now - lastRead >= interval) {
        lastRead = now;
        bootCheckpoint(CRASH_STAGE_BOOT_FIRST_METER);
        lastMeterReadOk = readMeterWithHealth();
        bootCheckpoint(CRASH_STAGE_BOOT_FIRST_DISPLAY);
        runtimeBreadcrumb(RB_OLED_BEGIN);
        applicationRenderMeasurements(lastMeterReadOk);
        runtimeBreadcrumb(RB_OLED_END);
        bootFirstMeterDone = true;
        meterDisplayUpdated = true;
    }
    serviceFlashSetupButton(millis());
    if (credentialPortalActive || credentialPortalOnly) serviceCredentialPortal(millis());
    runtimeBreadcrumb(RB_WIFI_STACK_BEGIN);
    const bool stackRecovered=serviceNetworkStackRecovery(millis());
    runtimeBreadcrumb(RB_WIFI_STACK_END,stackRecovered ? 1 : 0);
    if (stackRecovered) {
        serviceOledDiagnostic(millis());
        yield();
        return;
    }
    runtimeBreadcrumb(RB_WIFI_BEGIN);
    serviceWiFiReconnect(millis());
    runtimeBreadcrumb(RB_WIFI_END);

    // =====================================================
    // Network service after the due local measurement.
    // =====================================================
    serviceNetworkPriority(millis());

    if (!credentialPortalOnly && WiFi.status() == WL_CONNECTED) {

        // MQTT update: φρέσκια μέτρηση μόνο όταν ζητηθεί.
        if (mqttUpdateResponseDue(millis())) {
            mqttUpdateRequested = false;

            // Reuse the just-completed local read rather than repeating a timeout.
            if (!meterReadAttempted || millis() - lastRead >= interval) {
                lastRead = millis();
                readMeterWithHealth();
            }
            // Rate-limit replies, not acceptance of requests. Failed publishes
            // stay pending, but use 5/10/30 s retry backoff so a low-memory or
            // broken transport condition cannot continuously pressure TCP/TLS.
            const bool updatePublished = publishMqttMeasurements();
            if (!updatePublished) {
                mqttUpdateRequested = true;
                scheduleMqttUpdateRetry();
            } else {
                resetMqttUpdateRetry();
            }
            lastMqttUpdatePublishMs = millis();

            // Αμέσως μετά το Modbus read/publish εξυπηρετούμε πάλι δίκτυο.
            serviceNetworkPriority(millis());
        }

        // MQTT admin commands.
        if (mqttAdminCommandPending) {
            processMqttAdminCommand();

            // Τα admin commands μπορεί να κάνουν meter read / EEPROM / FS.
            // Επιστρέφουμε αμέσως σε Web/MQTT πριν συνεχίσουμε.
            serviceNetworkPriority(millis());
        }
    }

    // =====================================================
    // BACKGROUND HEALTH / APPLICATION SERVICE
    // =====================================================
    serviceMeterHealth(millis());
    runtimeBreadcrumb(RB_BACKGROUND_BEGIN);
    applicationBackground(millis());
    runtimeBreadcrumb(RB_BACKGROUND_END);

    // Application-specific storage/history work runs in applicationBackground().

    if (bootCheckpointsActive && bootFirstMeterDone &&
        (WiFi.status() != WL_CONNECTED || bootFirstMqttAttempted || !mqttSettingsReady())) {
        bootCheckpoint(CRASH_STAGE_BOOT_FIRST_DISPLAY);
        applicationRenderMeasurements(lastMeterReadOk); // Clear a completed network CP frame.
        finishBootCheckpoints(CRASH_STAGE_BOOT_RUNNING);
        meterDisplayUpdated = true;
    }
    serviceWiFiStatusDisplay(millis(), meterDisplayUpdated);
    serviceOledDiagnostic(millis());
    runtimeBreadcrumb(RB_LOOP_IDLE);
    yield();
}

// ==========================================================
// END MODULE: MAIN SETUP / LOOP
// ==========================================================
