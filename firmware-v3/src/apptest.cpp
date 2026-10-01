// ══════════════════════════════════════════════════════════════════
//  apptest.cpp — APP 整合測試韌體
//  燒錄：pio run -e apptest -t upload
//
//  涵蓋：SoftAP 配網、IMU、GPS、SD 卡、鏡頭串流、方向燈
//
//  流程：
//    開機 → NVS 有帳密且連得上 → STA 模式，mDNS bike-assist.local
//         → 否則開設定熱點 bike-assist-setup（密碼 bikeassist），
//           APP POST http://192.168.4.1/provision {"ssid":"..","password":".."}
//
//  APP 測試介面（port 80，配網後用 ESP32 的 IP 或 bike-assist.local）：
//    GET /stream                          MJPEG 鏡頭串流
//    GET /api/status                      全部感測器狀態 JSON
//    GET /api/led?mode=left|right|hazard|off   控制方向燈（500ms 閃爍）
//
//  /api/status 回傳範例：
//    {"wifi":"sta","ip":"192.168.137.34",
//     "imu":{"ok":true,"roll":1.2,"pitch":-0.5,"ax":0.01,"ay":0.02,"az":0.99},
//     "gps":{"chars":1234,"fix":true,"lat":23.99,"lon":121.60,"speed":0.0},
//     "sd":{"ok":true,"sizeMB":60350},
//     "camera":true,"led":"off"}
// ══════════════════════════════════════════════════════════════════
#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <SD_MMC.h>
#include "esp_camera.h"
#include "esp_http_server.h"
#include "IMU.h"
#include "GPS.h"

// ── 腳位 ──
#define IMU_SDA_PIN   2
#define IMU_SCL_PIN   1
#define IMU_INT_PIN  14
#define GPS_RX_PIN   21
#define PIN_LED_L    46
#define PIN_LED_R    47
#define PIN_SD_CLK   39
#define PIN_SD_CMD   38
#define PIN_SD_D0    40

// ── 配網常數 ──
static const char* AP_SSID   = "bike-assist-setup";
static const char* AP_PASS   = "bikeassist";
static const char* MDNS_HOST = "bike-assist";

// ── MJPEG ──
static const char* STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=frame";
static const char* STREAM_BOUNDARY     = "\r\n--frame\r\n";
static const char* STREAM_PART = "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

// ── 全域 ──
Preferences prefs;
httpd_handle_t webServer = nullptr;
IMUManager imu;
GPSModule  gps;

bool  imuOk = false, sdOk = false, camOk = false, staMode = false;
uint64_t sdSizeMB = 0;

// LED 閃爍狀態（獨立於 IndicatorModule，供 APP 手動測試）
enum class LedMode { OFF, LEFT, RIGHT, HAZARD };
volatile LedMode ledMode = LedMode::OFF;
bool blinkState = false;
unsigned long lastBlink = 0;

// ═══════════════ NVS ═══════════════
bool loadCreds(String& ssid, String& pass) {
    prefs.begin("wifi", true);
    ssid = prefs.getString("ssid", "");
    pass = prefs.getString("pass", "");
    prefs.end();
    return ssid.length() > 0;
}
void saveCreds(const String& ssid, const String& pass) {
    prefs.begin("wifi", false);
    prefs.putString("ssid", ssid);
    prefs.putString("pass", pass);
    prefs.end();
}

// ═══════════════ 相機 ═══════════════
bool cameraInit() {
    camera_config_t config = {
        .pin_pwdn = -1, .pin_reset = -1, .pin_xclk = 15,
        .pin_sccb_sda = 4, .pin_sccb_scl = 5,
        .pin_d7 = 16, .pin_d6 = 17, .pin_d5 = 18, .pin_d4 = 12,
        .pin_d3 = 10, .pin_d2 = 8,  .pin_d1 = 9,  .pin_d0 = 11,
        .pin_vsync = 6, .pin_href = 7, .pin_pclk = 13,
        .xclk_freq_hz = 20000000,
        .ledc_timer = LEDC_TIMER_0, .ledc_channel = LEDC_CHANNEL_0,
        .pixel_format = PIXFORMAT_JPEG, .frame_size = FRAMESIZE_QVGA,
        .jpeg_quality = 12, .fb_count = 2,
        .fb_location = CAMERA_FB_IN_PSRAM,
        .grab_mode = CAMERA_GRAB_WHEN_EMPTY
    };
    return esp_camera_init(&config) == ESP_OK;
}

// ═══════════════ SD（先 camera 再 SD_MMC）═══════════════
void sdInit() {
    SD_MMC.setPins(PIN_SD_CLK, PIN_SD_CMD, PIN_SD_D0);
    if (!SD_MMC.begin("/sdcard", true, false)) {
        Serial.println("[SD] 掛載失敗");
        return;
    }
    sdSizeMB = SD_MMC.cardSize() / (1024 * 1024);
    File f = SD_MMC.open("/apptest.txt", FILE_WRITE);
    sdOk = f && f.print("apptest ok");
    if (f) f.close();
    Serial.printf("[SD] %s，容量 %llu MB\n", sdOk ? "OK" : "寫入失敗", sdSizeMB);
}

// ═══════════════ HTTP handlers ═══════════════
esp_err_t streamHandler(httpd_req_t* req) {
    char part[64];
    if (httpd_resp_set_type(req, STREAM_CONTENT_TYPE) != ESP_OK) return ESP_FAIL;
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    Serial.println("[Stream] 用戶端已連線");
    esp_err_t res = ESP_OK;
    while (true) {
        camera_fb_t* fb = esp_camera_fb_get();
        if (!fb) { res = ESP_FAIL; break; }
        size_t hlen = snprintf(part, sizeof(part), STREAM_PART, fb->len);
        res = httpd_resp_send_chunk(req, STREAM_BOUNDARY, strlen(STREAM_BOUNDARY));
        if (res == ESP_OK) res = httpd_resp_send_chunk(req, part, hlen);
        if (res == ESP_OK) res = httpd_resp_send_chunk(req, (const char*)fb->buf, fb->len);
        esp_camera_fb_return(fb);
        if (res != ESP_OK) break;
    }
    Serial.println("[Stream] 用戶端離線");
    return res;
}

const char* ledModeStr() {
    switch (ledMode) {
        case LedMode::LEFT:   return "left";
        case LedMode::RIGHT:  return "right";
        case LedMode::HAZARD: return "hazard";
        default:              return "off";
    }
}

esp_err_t statusHandler(httpd_req_t* req) {
    // IMU 加速度換算 G（±2G 量程 16384 LSB/G）
    float ax = imu.getRawAX() / 16384.0f;
    float ay = imu.getRawAY() / 16384.0f;
    float az = imu.getRawAZ() / 16384.0f;

    char body[512];
    snprintf(body, sizeof(body),
        "{\"wifi\":\"%s\",\"ip\":\"%s\","
        "\"imu\":{\"ok\":%s,\"roll\":%.2f,\"pitch\":%.2f,"
        "\"ax\":%.2f,\"ay\":%.2f,\"az\":%.2f},"
        "\"gps\":{\"chars\":%lu,\"fix\":%s,\"lat\":%.6f,\"lon\":%.6f,\"speed\":%.1f},"
        "\"sd\":{\"ok\":%s,\"sizeMB\":%llu},"
        "\"camera\":%s,\"led\":\"%s\"}",
        staMode ? "sta" : "ap",
        staMode ? WiFi.localIP().toString().c_str()
                : WiFi.softAPIP().toString().c_str(),
        imuOk ? "true" : "false", imu.getRoll(), imu.getPitch(), ax, ay, az,
        gps.charsProcessed(), gps.isLocationValid() ? "true" : "false",
        gps.getLatitude(), gps.getLongitude(), gps.getSpeed(),
        sdOk ? "true" : "false", sdSizeMB,
        camOk ? "true" : "false", ledModeStr());

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    return httpd_resp_send(req, body, HTTPD_RESP_USE_STRLEN);
}

esp_err_t ledHandler(httpd_req_t* req) {
    char query[64] = {0}, mode[16] = {0};
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK)
        httpd_query_key_value(query, "mode", mode, sizeof(mode));

    if      (!strcmp(mode, "left"))   ledMode = LedMode::LEFT;
    else if (!strcmp(mode, "right"))  ledMode = LedMode::RIGHT;
    else if (!strcmp(mode, "hazard")) ledMode = LedMode::HAZARD;
    else if (!strcmp(mode, "off"))    ledMode = LedMode::OFF;
    else {
        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
                            "mode=left|right|hazard|off");
        return ESP_FAIL;
    }
    if (ledMode == LedMode::OFF) {
        digitalWrite(PIN_LED_L, LOW);
        digitalWrite(PIN_LED_R, LOW);
    }
    Serial.printf("[LED] 模式 → %s\n", ledModeStr());

    char body[48];
    snprintf(body, sizeof(body), "{\"ok\":true,\"led\":\"%s\"}", ledModeStr());
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    return httpd_resp_send(req, body, HTTPD_RESP_USE_STRLEN);
}

esp_err_t provisionHandler(httpd_req_t* req) {
    int total = req->content_len;
    if (total <= 0 || total > 512) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad length");
        return ESP_FAIL;
    }
    char buf[513];
    int received = 0;
    while (received < total) {
        int r = httpd_req_recv(req, buf + received, total - received);
        if (r <= 0) {
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "recv failed");
            return ESP_FAIL;
        }
        received += r;
    }
    buf[received] = '\0';

    JsonDocument doc;
    if (deserializeJson(doc, buf)) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad json");
        return ESP_FAIL;
    }
    String ssid = doc["ssid"] | "";
    String pass = doc["password"] | "";
    if (ssid.isEmpty()) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "empty ssid");
        return ESP_FAIL;
    }

    Serial.printf("[Provision] 收到帳密 SSID=%s\n", ssid.c_str());
    saveCreds(ssid, pass);

    WiFi.begin(ssid.c_str(), pass.c_str());
    unsigned long t0 = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t0 < 12000) {
        delay(300); Serial.print(".");
    }
    Serial.println();

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    char resp[128];
    if (WiFi.status() == WL_CONNECTED)
        snprintf(resp, sizeof(resp), "{\"status\":\"connected\",\"ip\":\"%s\"}",
                 WiFi.localIP().toString().c_str());
    else
        snprintf(resp, sizeof(resp), "{\"status\":\"saved\"}");
    httpd_resp_send(req, resp, HTTPD_RESP_USE_STRLEN);

    delay(1000);
    ESP.restart();
    return ESP_OK;
}

// ═══════════════ HTTP server ═══════════════
void startHttpServer(bool withProvision) {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;
    config.max_uri_handlers = 6;
    if (httpd_start(&webServer, &config) != ESP_OK) {
        Serial.println("[HTTP] 伺服器啟動失敗");
        return;
    }
    httpd_uri_t stream = { .uri = "/stream", .method = HTTP_GET,
                           .handler = streamHandler, .user_ctx = nullptr };
    httpd_uri_t status = { .uri = "/api/status", .method = HTTP_GET,
                           .handler = statusHandler, .user_ctx = nullptr };
    httpd_uri_t led    = { .uri = "/api/led", .method = HTTP_GET,
                           .handler = ledHandler, .user_ctx = nullptr };
    httpd_register_uri_handler(webServer, &status);
    httpd_register_uri_handler(webServer, &led);
    if (camOk) httpd_register_uri_handler(webServer, &stream);
    if (withProvision) {
        httpd_uri_t prov = { .uri = "/provision", .method = HTTP_POST,
                             .handler = provisionHandler, .user_ctx = nullptr };
        httpd_register_uri_handler(webServer, &prov);
    }
}

// ═══════════════ 主流程 ═══════════════
void setup() {
    Serial.begin(115200);
    delay(1500);
    Serial.println("\n══════ APP 整合測試韌體 ══════");

    // LED
    pinMode(PIN_LED_L, OUTPUT);
    pinMode(PIN_LED_R, OUTPUT);
    digitalWrite(PIN_LED_L, LOW);
    digitalWrite(PIN_LED_R, LOW);

    // IMU（先掃 I2C 匯流排，方便判斷接線/位址問題）
    Wire.begin(IMU_SDA_PIN, IMU_SCL_PIN);
    int found = 0;
    for (uint8_t addr = 8; addr < 120; addr++) {
        Wire.beginTransmission(addr);
        if (Wire.endTransmission() == 0) {
            Serial.printf("[I2C] 找到裝置: 0x%02X%s\n", addr,
                addr == 0x68 ? "（MPU6050，AD0=低，正確）" :
                addr == 0x69 ? "（MPU6050，但 AD0=高！請把 AD0 接 GND 或留空）" : "");
            found++;
        }
    }
    if (found == 0)
        Serial.println("[I2C] 匯流排上沒有任何裝置 → 檢查 VCC/GND/SDA(G1)/SCL(G2) 接線與焊點");
    imuOk = imu.begin(IMU_SDA_PIN, IMU_SCL_PIN, IMU_INT_PIN);
    Serial.printf("[IMU] %s\n", imuOk ? "OK" : "失敗（檢查 I2C 接線/焊接）");

    // GPS（只收，TX 不接）
    gps.begin(Serial1, 9600, GPS_RX_PIN, -1);

    // 相機（先 camera 再 SD）
    if (!psramFound()) Serial.println("[PSRAM] ✗ 未偵測到！");
    camOk = cameraInit();
    Serial.printf("[Camera] %s\n", camOk ? "OK" : "初始化失敗");

    // SD
    sdInit();

    // WiFi：STA 優先，失敗開配網熱點
    String ssid, pass;
    if (loadCreds(ssid, pass)) {
        WiFi.mode(WIFI_STA);
        WiFi.begin(ssid.c_str(), pass.c_str());
        Serial.printf("[WiFi] 連線中 (%s)", ssid.c_str());
        unsigned long t0 = millis();
        while (WiFi.status() != WL_CONNECTED && millis() - t0 < 15000) {
            delay(500); Serial.print(".");
        }
        Serial.println();
        staMode = (WiFi.status() == WL_CONNECTED);
    }
    if (!staMode) {
        WiFi.mode(WIFI_AP_STA);
        WiFi.softAP(AP_SSID, AP_PASS);
        Serial.printf("[Setup] 配網熱點 %s（密碼 %s），APP POST http://%s/provision\n",
                      AP_SSID, AP_PASS, WiFi.softAPIP().toString().c_str());
    } else {
        if (MDNS.begin(MDNS_HOST))
            Serial.printf("[mDNS] http://%s.local\n", MDNS_HOST);
        Serial.printf("[WiFi] IP=%s\n", WiFi.localIP().toString().c_str());
    }

    startHttpServer(!staMode);

    Serial.println("── APP 介面 ──────────────────────");
    Serial.printf("狀態  GET http://%s/api/status\n",
                  staMode ? WiFi.localIP().toString().c_str() : "192.168.4.1");
    Serial.println("燈    GET /api/led?mode=left|right|hazard|off");
    Serial.println("串流  GET /stream");
    Serial.println("═════════════════════════════════");
}

void loop() {
    unsigned long now = millis();

    // IMU / GPS 持續更新
    if (imuOk) imu.update();
    gps.update();

    // LED 500ms 閃爍
    if (ledMode != LedMode::OFF && now - lastBlink >= 500) {
        lastBlink = now;
        blinkState = !blinkState;
        digitalWrite(PIN_LED_L,
            (ledMode == LedMode::LEFT  || ledMode == LedMode::HAZARD) && blinkState);
        digitalWrite(PIN_LED_R,
            (ledMode == LedMode::RIGHT || ledMode == LedMode::HAZARD) && blinkState);
    }

    // 每 5 秒印摘要（無 APP 也可監看）
    static unsigned long lastPrint = 0;
    if (now - lastPrint >= 5000) {
        lastPrint = now;
        Serial.printf("[狀態] IMU %s roll=%.1f pitch=%.1f | GPS chars=%lu fix=%d | LED=%s\n",
                      imuOk ? "OK" : "--", imu.getRoll(), imu.getPitch(),
                      gps.charsProcessed(), gps.isLocationValid(), ledModeStr());
    }
}
