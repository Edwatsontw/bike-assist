// ══════════════════════════════════════════════════════════════════
//  main.cpp — 正式韌體（二代機 GOOUUU ESP32-S3-CAM，N16R8）
//
//  整合全部功能（完整版）：
//    ├─ 配網：NVS 有帳密→STA＋mDNS bike-assist.local；否則開熱點供網
//    ├─ IMU（MPU6050）：roll/pitch＋三軸 G，AccelAnalyzer 判煞車/碰撞
//    ├─ GPS（NEO-6M）：經緯度/速度/高度（只收 RX）
//    ├─ 相機（OV2640）：MJPEG 串流（port 81 獨立埠，避免餓死 API）
//    ├─ SD（板載 SDMMC，CameraManager 管）：行車紀錄事件錄影
//    ├─ 方向燈/警示燈（IndicatorModule）：roll 自動方向燈＋碰撞/急煞警示
//    │    碰撞(COLLISION)→警示燈鎖定，需 /api/led?mode=off 手動解除
//    │    急煞(BRAKE)→警示燈暫亮，逾時自動解除
//    └─ 無後端：陀螺儀/GPS 由 APP 讀 /api/status，串流讀 /stream，路線由 APP 累積
//
//  HTTP API（port 80，皆含 CORS）：
//    GET  /api/status                          全部感測器/系統狀態 JSON
//    GET  /api/led?mode=left|right|hazard|off  手動方向燈（off=恢復自動）
//    GET  /api/night?mode=on|off               夜間模式（軟體低光增強）
//    POST /provision                           供網（僅未連上 STA 時開放）
//  相機（port 81）：
//    GET  /stream                              MJPEG 鏡頭串流
// ══════════════════════════════════════════════════════════════════
#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>   // v3：影像/數據上推伺服器
#include <time.h>
#include <sys/time.h>
#include "esp_sntp.h"
#include "esp_camera.h"
#include "esp_http_server.h"

#include "IMU.h"
#include "AccelAnalyzer.h"
#include "GPS.h"
#include "Camera.h"
#include "IndicatorModule.h"
#include "TurnSwitch.h"
#include "FallDetector.h"
#include <NimBLEDevice.h>   // 倒車示警 BLE 廣播

// ── GPIO（二代機腳位；相機 4-18、板載 SD 38/39/40 由各模組固定）──
#define IMU_SDA_PIN          2   // I2C SDA
#define IMU_SCL_PIN          1   // I2C SCL
#define IMU_INT_PIN         14   // 中斷
#define GPS_RX_PIN          21   // UART RX ← GPS TX
#define GPS_TX_PIN          -1   // 不接
#define INDICATOR_LEFT_PIN  46   // 左方向燈（MOSFET Gate）
#define INDICATOR_RIGHT_PIN 47   // 右方向燈（MOSFET Gate）
#define TURN_SWITCH_LEFT_PIN  41 // 方向燈搖桿開關 左觸點（2026-07-19 新增；原音效腳位，已空出）
#define TURN_SWITCH_RIGHT_PIN 48 // 方向燈搖桿開關 右觸點（原規劃42，板子上42不好接，改48；
                                 // 48與板載WS2812共用但未驅動、非strapping，比42更安全）

// ── 供網常數 ────────────────────────────────────────────────────
static const char* AP_SSID   = "bike-assist-setup";
static const char* AP_PASS   = "bikeassist";
static const char* MDNS_HOST = "bike-assist";

// ── 已知 WiFi 清單（v3, 2026-07-20 新增：多網路自動切換）────────
// 開機時掃描環境，訊號最強的已知網路優先連線；全都不在才開配網熱點。
// 使用情境：在家連電腦熱點/家用網路（開發＋伺服器上傳）、出門連手機熱點
// （看串流）、未來歸還站點 WiFi 直接加一列即可（v3 站點批次上傳）。
// 另外 /provision 配網存進 NVS 的那組會動態併入此清單（優先序最高）。
struct KnownNet { const char* ssid; const char* pass; };
// 2026-10-02：熱點帳密移到 secrets.h（不進 Git）。第一次編譯前：
//   複製 secrets.example.h → secrets.h，填入自己的熱點名稱與密碼。
#include "secrets.h"   // 定義 KNOWN_NETS[]
static const int KNOWN_NETS_COUNT = sizeof(KNOWN_NETS) / sizeof(KNOWN_NETS[0]);

// ── MJPEG 串流常數 ──────────────────────────────────────────────
static const char* STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=frame";
static const char* STREAM_BOUNDARY     = "\r\n--frame\r\n";
static const char* STREAM_PART = "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

// ── 週期 ────────────────────────────────────────────────────────
#define ACCEL_RESET_INTERVAL 1000  // 每 1s 重置加速度事件統計窗口
#define STATUS_PRINT_INTERVAL 5000 // Serial 摘要每 5s
#define RECORD_INTERVAL       66   // 相機取樣間隔（2026-07-20 提到≈15fps上限；
                                    // 若同時有人直連 port81 /stream 會搶 frame buffer，
                                    // 目前用法是走伺服器轉播、無人直連 port81 才安全拉高）
#define BRAKE_HAZARD_MS      5000  // 急煞警示持續時間（煞車後 5 秒回歸正常）

// ── v3 伺服器上推（2026-07-20 新增）─────────────────────────────
// 伺服器位址自動取「WiFi 閘道 IP」：連電腦熱點時閘道就是那台電腦
// （Windows 行動熱點通常是 192.168.137.1），不用寫死 IP。
// 上推失敗（伺服器沒開）自動退避 30s 再試，避免拖慢主迴圈。
#define SERVER_PORT          8000  // FastAPI（uvicorn 預設埠）
#define FRAME_PUSH_INTERVAL   66   // 影格上推間隔 ms（≈15 FPS上限，2026-07-20調整；
                                    // 實際幀率仍受相機硬體編碼＋WiFi頻寬限制，非保證值）
#define DATA_PUSH_INTERVAL   5000  // 感測數據上推間隔 ms
#define PUSH_BACKOFF_MS      30000 // 上推失敗退避時間
unsigned long lastFramePush = 0, lastDataPush = 0, pushBackoffUntil = 0;

// 伺服器位址：
//  - 站點/正式部署：設 SERVER_HOST_OVERRIDE 為家用伺服器對外網址
//    （例如 Cloudflare Tunnel 的網域，含 http(s)://，不需再加埠）
//  - 開發測試（車機連電腦熱點，閘道=電腦=伺服器）：留空，自動用「WiFi 閘道 IP:8000」
#define SERVER_HOST_OVERRIDE ""   // 例："https://bike.yourdomain.com"
String serverBase() {
    if (strlen(SERVER_HOST_OVERRIDE) > 0) return String(SERVER_HOST_OVERRIDE);
    return "http://" + WiFi.gatewayIP().toString() + ":" + String(SERVER_PORT);
}

// ── 站點批次上傳（2026-07-20 v3）────────────────────────────────
// 到站連上 WiFi 後，把 SD /dashcam 的 log.csv 與影像批次 POST 到伺服器，
// 上傳成功的影像即從 SD 刪除（釋放空間、避免重複上傳）。log.csv 上傳為
// 冪等（伺服器依 filename upsert），故保留不刪。
// STATION_SSID 非空且開機連上的就是它 → 自動觸發；否則可打 GET /upload 手動觸發。
#define STATION_SSID ""   // 設為歸還站服務機的 WiFi 名稱以啟用自動上傳
bool stationUploadDone = false;

// 上傳單一目錄下所有 .jpg，成功者刪除。回傳成功張數。
int uploadDirImages(const String& dir, const String& base) {
    int ok = 0;
    File d = SD_MMC.open(dir);
    if (!d || !d.isDirectory()) return 0;
    File f;
    while ((f = d.openNextFile())) {
        if (f.isDirectory()) { f.close(); continue; }
        String name = f.name();
        String bn = name.substring(name.lastIndexOf('/') + 1);
        if (!bn.endsWith(".jpg")) { f.close(); continue; }
        String full = dir + "/" + bn;
        File up = SD_MMC.open(full, FILE_READ);
        if (!up) { f.close(); continue; }
        HTTPClient http;
        http.setConnectTimeout(2000); http.setTimeout(8000);
        if (http.begin(base + "/api/upload/frame/" + bn)) {
            http.addHeader("Content-Type", "image/jpeg");
            int code = http.sendRequest("POST", &up, up.size());  // 串流上傳，不佔大記憶體
            http.end();
            up.close();
            if (code == 200) { SD_MMC.remove(full); ok++; }
        } else { up.close(); }
        f.close();
        delay(5);  // 讓出時間避免看門狗
    }
    d.close();
    return ok;
}

// 執行整趟批次上傳；回傳簡短摘要字串
extern CameraManager camera;   // 定義在後面（181行附近），這裡先前向宣告供本函式使用
String uploadDashcamBatch() {
    if (!camera.isSDReady()) return "SD 未就緒";
    String base = serverBase();

    // 1) 先傳 log.csv（影像↔GPS 對應）
    bool logOk = false;
    File lf = SD_MMC.open("/dashcam/log.csv", FILE_READ);
    if (lf) {
        HTTPClient http;
        http.setConnectTimeout(2000); http.setTimeout(8000);
        if (http.begin(base + "/api/upload/log")) {
            http.addHeader("Content-Type", "text/csv");
            int code = http.sendRequest("POST", &lf, lf.size());
            http.end();
            logOk = (code == 200);
        }
        lf.close();
    }

    // 2) 傳影像（normal + event），成功者刪除
    int n1 = uploadDirImages("/dashcam/normal", base);
    int n2 = uploadDirImages("/dashcam/event",  base);

    char buf[96];
    snprintf(buf, sizeof(buf), "log=%s normal=%d event=%d 已上傳",
             logOk ? "ok" : "fail", n1, n2);
    Serial.printf("[Upload] 站點批次上傳完成：%s\n", buf);
    return String(buf);
}

// ── 模組 ────────────────────────────────────────────────────────
IMUManager      imu;
AccelAnalyzer   accel;
GPSModule       gps;
CameraManager   camera;
IndicatorModule indicator;
TurnSwitch      turnSwitch;
FallDetector    fallDetector;
Preferences     prefs;

// ── 倒車示警 BLE 廣播（2026-07-20 v3 新增）─────────────────────
// 倒車滿5分鐘未扶正 → BLE 廣播事件（含GPS座標），附近裝app的手機掃到後
// 代傳座標給伺服器。用製造商自訂資料(manufacturer data)攜帶 payload，
// 手機端只需掃描、不需配對連線。BLE 延後到第一次觸發才初始化（省記憶體）。
#define FALL_ANGLE_DEG      70.0f      // 傾倒判定角度
#define FALL_NOTICE_MS      10000UL    // 倒下10秒 → 一般倒車通知
#define FALL_EMERGENCY_MS   300000UL   // 倒下持續到5分鐘仍未扶正 → 升級為緊急事故
#define BLE_MFG_ID          0xFFFF     // 測試用製造商ID（量產應申請正式ID）
#define BLE_EVT_FALLEN      0x01       // 事件類型：倒車（一般通知，10秒觸發）
#define BLE_EVT_EMERGENCY   0x02       // 事件類型：緊急事故（5分鐘未扶正升級）
#define BLE_ADV_HOLD_MS     120000UL   // 觸發後廣播持續時間（2分鐘，給手機掃描時間）
bool          bleInited      = false;
unsigned long bleAdvUntil    = 0;   // 廣播到期時刻（0=未廣播）

// 觸發倒車 BLE 廣播：payload = [evt(1) lat(4) lon(4) epoch(4)]
// evtType：BLE_EVT_FALLEN(0x01)=一般倒車通知，BLE_EVT_EMERGENCY(0x02)=緊急事故
// （app 端解析 payload 第0byte 即可分辨是哪一種，決定通知的緊急程度/文案）
void broadcastFallenBLE(double lat, double lon, uint32_t epoch, uint8_t evtType) {
    if (!bleInited) {
        NimBLEDevice::init("bike-assist-fall");
        NimBLEDevice::setPower(ESP_PWR_LVL_P9);   // 最大發射功率，拉長可掃到的距離
        bleInited = true;
    }
    uint8_t p[13];
    p[0] = evtType;
    float flat = (float)lat, flon = (float)lon;
    memcpy(&p[1], &flat, 4);
    memcpy(&p[5], &flon, 4);
    memcpy(&p[9], &epoch, 4);

    std::string mfg;
    mfg.push_back((char)(BLE_MFG_ID & 0xFF));
    mfg.push_back((char)((BLE_MFG_ID >> 8) & 0xFF));
    mfg.append((char*)p, sizeof(p));

    NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
    adv->stop();
    NimBLEAdvertisementData ad;
    ad.setManufacturerData(mfg);
    adv->setAdvertisementData(ad);
    adv->setName("bike-assist-fall");
    adv->start();
    bleAdvUntil = millis() + BLE_ADV_HOLD_MS;
    Serial.printf("[Fall] BLE廣播啟動 evt=0x%02X lat=%.6f lon=%.6f\n", evtType, lat, lon);
}

httpd_handle_t  webServer    = nullptr;   // port 80：控制＋數據
httpd_handle_t  streamServer = nullptr;   // port 81：鏡頭串流（獨立埠）

// ── 系統狀態 ────────────────────────────────────────────────────
bool imuOk = false, sdOk = false, camOk = false, staMode = false;

// ── 方向燈手動覆蓋（/api/led）；NONE = 交給 roll 自動判斷 ──────
enum class ManualLed { NONE, LEFT, RIGHT, HAZARD };
volatile ManualLed manualLed = ManualLed::NONE;

// ── 警示燈狀態 ──────────────────────────────────────────────────
bool          hazardPermanent = false;  // 碰撞鎖定：需 /api/led?mode=off 解除
unsigned long brakeHazardUntil = 0;      // 急煞暫亮：逾時自動解除

// ── 計時器 ──────────────────────────────────────────────────────
unsigned long lastAccelReset = 0, lastPrint = 0, lastRecord = 0, lastTimeSync = 0;

// ── 時間同步（連線→NTP、離線→GPS 衛星 UTC；系統 RTC 持續走）──────
static const char* TZ_TAIPEI  = "CST-8";           // 台灣 UTC+8，無日光節約
static const time_t TIME_VALID_MIN = 1735689600;   // 2025-01-01：判斷時鐘是否已校時
enum class TimeSource { NONE, GPS, NTP };
volatile TimeSource timeSource = TimeSource::NONE;

// 系統時鐘是否已被校時（NTP 或 GPS 任一）
bool timeIsSet() { return time(nullptr) > TIME_VALID_MIN; }

// 目前時間字串（本地時間 YYYY-MM-DD HH:MM:SS）；未校時回 "----"
String currentTimeStr() {
    if (!timeIsSet()) return String("----");
    time_t now = time(nullptr);
    struct tm t;
    localtime_r(&now, &t);
    char buf[20];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &t);
    return String(buf);
}

const char* timeSourceStr() {
    switch (timeSource) {
        case TimeSource::NTP: return "ntp";
        case TimeSource::GPS: return "gps";
        default:              return "none";
    }
}

// NTP 校時成功回呼（連線時由 SNTP 觸發，NTP 優先於 GPS）
void onNtpSync(struct timeval*) {
    timeSource = TimeSource::NTP;
    Serial.printf("[Time] NTP 校時完成 → %s\n", currentTimeStr().c_str());
}

// 週期呼叫：NTP 尚未校時前／離線時，用 GPS 衛星 UTC 補上系統時間
void maybeSyncTimeFromGPS() {
    if (timeSource == TimeSource::NTP) return;   // NTP 已權威，不覆蓋
    if (!gps.isTimeValid()) return;
    time_t e = gps.getEpoch();
    if (e < TIME_VALID_MIN) return;
    struct timeval tv;
    tv.tv_sec  = e;
    tv.tv_usec = 0;
    settimeofday(&tv, nullptr);
    if (timeSource != TimeSource::GPS) {
        timeSource = TimeSource::GPS;
        Serial.printf("[Time] GPS 校時完成 → %s\n", currentTimeStr().c_str());
    }
}

// ═══════════════ NVS：WiFi 帳密 ═══════════════
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

// ═══════════════ 位置來源（2026-10-01）═══════════════
// 手機定位優先、GPS 天線備援：
//   APP 連上車機時每秒打 GET /api/phoneloc 回報手機座標。手機座標在 PHONE_LOC_STALE_MS
//   內有更新就用手機的；APP 斷線／關閉／手機定位精度太差 → 自動改用 GPS 天線。
//   NEO-6M 只接 RX、電源常開，韌體無法讓它關機；所以 GPS 仍在背景持續解析、保持定位，
//   切回 GPS 時不必重新冷啟動搜星（冷啟動要 30 秒以上，倒車時剛好沒座標就糟了）。
#define PHONE_LOC_STALE_MS   5000UL   // 手機座標超過 5 秒沒更新 → 視為斷線，改用 GPS
#define PHONE_LOC_MAX_ACC_M  100.0f   // 手機精度差於 100 m（室內只靠基地台）時，天線有定位就先用天線

struct PhoneLoc {
    double lat; double lon;
    float speedKmh;      // <0 = 手機沒提供速度
    float accM;          // <0 = 手機沒提供精度
    unsigned long at;    // millis()
    bool has;
};
PhoneLoc phoneLoc = { 0, 0, -1, -1, 0, false };

struct Position {
    bool valid;
    double lat; double lon;
    float speedKmh;
    const char* src;     // "phone" | "gps" | "none"
};

bool phoneLocFresh() {
    return phoneLoc.has && (millis() - phoneLoc.at) < PHONE_LOC_STALE_MS;
}

// 手機精度是否夠好（沒回報精度視為夠好）
bool phoneLocAccurate() {
    return phoneLoc.accM < 0 || phoneLoc.accM <= PHONE_LOC_MAX_ACC_M;
}

// 目前要用的位置：
//   1. 手機（新鮮且精度夠好）
//   2. GPS 天線（有定位）
//   3. 手機（新鮮但精度差）——天線沒接／沒定位時，有總比沒有好（2026-10-02）
//   4. 無
Position currentPosition() {
    bool phoneUsable = phoneLocFresh() && (phoneLocAccurate() || !gps.isLocationValid());
    if (phoneUsable) {
        float spd = phoneLoc.speedKmh >= 0 ? phoneLoc.speedKmh
                  : (gps.isLocationValid() ? (float)gps.getSpeed() : 0.0f);
        Position p = { true, phoneLoc.lat, phoneLoc.lon, spd, "phone" };
        return p;
    }
    if (gps.isLocationValid()) {
        Position p = { true, gps.getLatitude(), gps.getLongitude(), (float)gps.getSpeed(), "gps" };
        return p;
    }
    Position p = { false, 0, 0, 0, "none" };
    return p;
}

// ═══════════════ HTTP handlers ═══════════════
// 目前方向燈字串（給 /api/status）
const char* currentLedStr() {
    if (hazardPermanent || manualLed == ManualLed::HAZARD) return "hazard";
    if (manualLed == ManualLed::LEFT)  return "left";
    if (manualLed == ManualLed::RIGHT) return "right";
    if (millis() < brakeHazardUntil)   return "hazard";
    return indicator.getDirectionStr();   // 自動（NONE/LEFT/RIGHT/HAZARD）
}

esp_err_t streamHandler(httpd_req_t* req) {
    char part[64];
    if (httpd_resp_set_type(req, STREAM_CONTENT_TYPE) != ESP_OK) return ESP_FAIL;
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    Serial.println("[Stream] 用戶端已連線");
    esp_err_t res = ESP_OK;
    while (true) {
        camera_fb_t* fb = camera.captureFrame();
        if (!fb) { res = ESP_FAIL; break; }
        size_t hlen = snprintf(part, sizeof(part), STREAM_PART, fb->len);
        res = httpd_resp_send_chunk(req, STREAM_BOUNDARY, strlen(STREAM_BOUNDARY));
        if (res == ESP_OK) res = httpd_resp_send_chunk(req, part, hlen);
        if (res == ESP_OK) res = httpd_resp_send_chunk(req, (const char*)fb->buf, fb->len);
        camera.release(fb);
        if (res != ESP_OK) break;
    }
    Serial.println("[Stream] 用戶端離線");
    return res;
}

// APP 回報手機定位：GET /api/phoneloc?lat=23.97&lon=121.60&spd=12.3&acc=8
//   spd = km/h（可省略）、acc = 水平精度 m（可省略）
esp_err_t phoneLocHandler(httpd_req_t* req) {
    char query[128] = {0}, v[24];
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "lat & lon required");
        return ESP_FAIL;
    }
    double lat = NAN, lon = NAN;
    float spd = -1, acc = -1;
    if (httpd_query_key_value(query, "lat", v, sizeof(v)) == ESP_OK) lat = atof(v);
    if (httpd_query_key_value(query, "lon", v, sizeof(v)) == ESP_OK) lon = atof(v);
    if (httpd_query_key_value(query, "spd", v, sizeof(v)) == ESP_OK) spd = atof(v);
    if (httpd_query_key_value(query, "acc", v, sizeof(v)) == ESP_OK) acc = atof(v);
    if (isnan(lat) || isnan(lon) || fabs(lat) > 90 || fabs(lon) > 180 ||
        (lat == 0 && lon == 0)) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid lat/lon");
        return ESP_FAIL;
    }
    httpd_resp_set_type(req, "application/json");
    phoneLoc.lat = lat;
    phoneLoc.lon = lon;
    phoneLoc.speedKmh = spd;
    phoneLoc.accM = acc;
    phoneLoc.at = millis();
    phoneLoc.has = true;
    // 精度差且天線有定位 → 先用天線（回報 low_accuracy 讓 APP 顯示提示）；天線沒定位 → 照用手機
    if (!phoneLocAccurate() && gps.isLocationValid())
        return httpd_resp_send(req, "{\"ok\":false,\"reason\":\"low_accuracy\"}", HTTPD_RESP_USE_STRLEN);
    return httpd_resp_send(req, "{\"ok\":true,\"src\":\"phone\"}", HTTPD_RESP_USE_STRLEN);
}

esp_err_t statusHandler(httpd_req_t* req) {
    Position pos = currentPosition();
    long phoneAge = phoneLoc.has ? (long)(millis() - phoneLoc.at) : -1;
    char body[1024];
    snprintf(body, sizeof(body),
        "{\"wifi\":\"%s\",\"ip\":\"%s\","
        "\"imu\":{\"ok\":%s,\"roll\":%.2f,\"pitch\":%.2f,"
        "\"ax\":%.2f,\"ay\":%.2f,\"az\":%.2f,\"i2cStale\":%lu},"
        "\"accel\":{\"event\":\"%s\",\"g\":%.2f},"
        "\"gps\":{\"chars\":%lu,\"fix\":%s,\"lat\":%.6f,\"lon\":%.6f,\"speed\":%.1f},"
        "\"time\":{\"now\":\"%s\",\"source\":\"%s\"},"
        "\"sd\":{\"ok\":%s},\"camera\":%s,\"night\":%s,"
        "\"led\":\"%s\",\"hazard\":%s,"
        "\"loc\":{\"src\":\"%s\",\"valid\":%s,\"lat\":%.6f,\"lon\":%.6f,"
        "\"speed\":%.1f,\"phoneAgeMs\":%ld}}",
        staMode ? "sta" : "ap",
        staMode ? WiFi.localIP().toString().c_str()
                : WiFi.softAPIP().toString().c_str(),
        imuOk ? "true" : "false", imu.getRoll(), imu.getPitch(),
        accel.getX(), accel.getY(), accel.getZ(), (unsigned long)imu.getStaleCount(),
        accel.getEventStr(), accel.getMagnitude(),
        gps.charsProcessed(), gps.isLocationValid() ? "true" : "false",
        gps.getLatitude(), gps.getLongitude(), gps.getSpeed(),
        currentTimeStr().c_str(), timeSourceStr(),
        sdOk ? "true" : "false", camOk ? "true" : "false",
        camera.isNightMode() ? "true" : "false",
        currentLedStr(), hazardPermanent ? "true" : "false",
        pos.src, pos.valid ? "true" : "false", pos.lat, pos.lon, pos.speedKmh, phoneAge);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    return httpd_resp_send(req, body, HTTPD_RESP_USE_STRLEN);
}

esp_err_t ledHandler(httpd_req_t* req) {
    char query[64] = {0}, mode[16] = {0};
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK)
        httpd_query_key_value(query, "mode", mode, sizeof(mode));

    if      (!strcmp(mode, "left"))   manualLed = ManualLed::LEFT;
    else if (!strcmp(mode, "right"))  manualLed = ManualLed::RIGHT;
    else if (!strcmp(mode, "hazard")) manualLed = ManualLed::HAZARD;
    else if (!strcmp(mode, "off")) {
        // 恢復自動：清除手動覆蓋，並解除碰撞鎖定
        manualLed = ManualLed::NONE;
        hazardPermanent = false;
        brakeHazardUntil = 0;
        indicator.off();
    } else {
        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "mode=left|right|hazard|off");
        return ESP_FAIL;
    }
    Serial.printf("[LED] 手動 → %s\n", mode);

    char body[64];
    snprintf(body, sizeof(body), "{\"ok\":true,\"led\":\"%s\"}", currentLedStr());
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    return httpd_resp_send(req, body, HTTPD_RESP_USE_STRLEN);
}

esp_err_t nightHandler(httpd_req_t* req) {
    char query[64] = {0}, mode[16] = {0};
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK)
        httpd_query_key_value(query, "mode", mode, sizeof(mode));

    if      (!strcmp(mode, "on"))  camera.setNightMode(true);
    else if (!strcmp(mode, "off")) camera.setNightMode(false);
    else {
        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "mode=on|off");
        return ESP_FAIL;
    }

    char body[64];
    snprintf(body, sizeof(body), "{\"ok\":true,\"night\":%s}",
             camera.isNightMode() ? "true" : "false");
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

// ═══════════════ SD 卡瀏覽／下載（v3, 2026-07-20 新增）═══════════════
// 用途：不拔卡直接從瀏覽器讀 SD 內容（驗證 log.csv、抓照片）。
// 也是 v3 站點批次上傳的前置——同樣是「SD 檔案 → HTTP 送出」路徑。
//   GET /sd            → 列出 /dashcam 根目錄（HTML）
//   GET /sd?dir=/dashcam/normal → 列出子目錄
//   GET /sd/file?path=/dashcam/log.csv → 下載檔案
// 安全：path 必須以 /dashcam 開頭，擋掉任意路徑存取。

// 檔案大小 → 人類可讀字串（B / KB / MB）
static void fmtSize(char* out, size_t outLen, unsigned bytes) {
    if      (bytes >= 1048576) snprintf(out, outLen, "%.1f MB", bytes / 1048576.0f);
    else if (bytes >= 1024)    snprintf(out, outLen, "%.1f KB", bytes / 1024.0f);
    else                       snprintf(out, outLen, "%u B", bytes);
}

esp_err_t sdListHandler(httpd_req_t* req) {
    char query[160] = {0}, dirPath[128] = "/dashcam";
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK)
        httpd_query_key_value(query, "dir", dirPath, sizeof(dirPath));
    if (strncmp(dirPath, "/dashcam", 8) != 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "dir must start with /dashcam");
        return ESP_FAIL;
    }
    File dir = SD_MMC.open(dirPath);
    if (!dir || !dir.isDirectory()) {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "dir not found");
        return ESP_FAIL;
    }
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

    // ── 頁首（靜態 CSS，乾淨簡約風、手機優先）──
    static const char HTML_HEAD[] =
        "<!DOCTYPE html><html><head><meta charset='utf-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>bike-assist SD</title><style>"
        "*{box-sizing:border-box}"
        "body{font-family:system-ui,-apple-system,'Segoe UI',Roboto,sans-serif;"
        "background:#fafafa;color:#1a1a1a;margin:0;padding:24px 16px}"
        "main{max-width:560px;margin:0 auto}"
        "h1{font-size:17px;font-weight:600;margin:0}"
        ".path{color:#999;font-size:13px;margin:4px 0 16px;word-break:break-all}"
        ".list{background:#fff;border:1px solid #e6e6e6;border-radius:12px;overflow:hidden}"
        ".item{display:flex;justify-content:space-between;align-items:center;gap:12px;"
        "padding:13px 16px;border-top:1px solid #f1f1f1;text-decoration:none;color:inherit}"
        ".item:first-child{border-top:none}"
        ".item:active,.item:hover{background:#f4f6fa}"
        ".name{font-size:14px;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}"
        ".size{color:#aaa;font-size:12px;flex-shrink:0}"
        ".dir .name{font-weight:600}"
        ".ico{margin-right:8px}"
        ".empty{padding:24px 16px;color:#aaa;font-size:13px;text-align:center}"
        ".back{display:inline-block;margin-bottom:12px;font-size:13px;color:#4a6cf7;"
        "text-decoration:none}"
        "</style></head><body><main>";
    httpd_resp_send_chunk(req, HTML_HEAD, HTTPD_RESP_USE_STRLEN);

    char buf[320];
    // 標題與路徑；非根目錄時顯示返回連結
    bool isRoot = (strcmp(dirPath, "/dashcam") == 0);
    snprintf(buf, sizeof(buf),
        "<h1>行車紀錄</h1><div class='path'>%s</div>%s<div class='list'>",
        dirPath,
        isRoot ? "" : "<a class='back' href='/sd'>&larr; 回上層</a>");
    httpd_resp_send_chunk(req, buf, HTTPD_RESP_USE_STRLEN);

    int count = 0;
    File entry;
    while ((entry = dir.openNextFile())) {
        const char* name = entry.name();   // 完整路徑或檔名（依核心版本），統一處理
        const char* base = strrchr(name, '/') ? strrchr(name, '/') + 1 : name;
        if (entry.isDirectory()) {
            snprintf(buf, sizeof(buf),
                "<a class='item dir' href='/sd?dir=%s/%s'>"
                "<span class='name'><span class='ico'>&#128193;</span>%s</span>"
                "<span class='size'>&rsaquo;</span></a>",
                dirPath, base, base);
        } else {
            char sizeStr[16];
            fmtSize(sizeStr, sizeof(sizeStr), (unsigned)entry.size());
            bool isJpg = strstr(base, ".jpg") != nullptr;
            snprintf(buf, sizeof(buf),
                "<a class='item' href='/sd/file?path=%s/%s'>"
                "<span class='name'><span class='ico'>%s</span>%s</span>"
                "<span class='size'>%s</span></a>",
                dirPath, base,
                isJpg ? "&#128247;" : "&#128196;",   // 📷 / 📄
                base, sizeStr);
        }
        httpd_resp_send_chunk(req, buf, HTTPD_RESP_USE_STRLEN);
        entry.close();
        count++;
    }
    dir.close();
    if (count == 0)
        httpd_resp_send_chunk(req,
            "<div class='empty'>（此資料夾沒有檔案）</div>", HTTPD_RESP_USE_STRLEN);
    httpd_resp_send_chunk(req, "</div></main></body></html>", HTTPD_RESP_USE_STRLEN);
    httpd_resp_send_chunk(req, nullptr, 0);   // 結束 chunked 回應
    return ESP_OK;
}

esp_err_t sdFileHandler(httpd_req_t* req) {
    char query[192] = {0}, path[160] = {0};
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK ||
        httpd_query_key_value(query, "path", path, sizeof(path)) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "need ?path=/dashcam/...");
        return ESP_FAIL;
    }
    if (strncmp(path, "/dashcam", 8) != 0 || strstr(path, "..")) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "path must start with /dashcam");
        return ESP_FAIL;
    }
    File f = SD_MMC.open(path, FILE_READ);
    if (!f || f.isDirectory()) {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "file not found");
        return ESP_FAIL;
    }
    // Content-Type 依副檔名
    if (strstr(path, ".jpg"))      httpd_resp_set_type(req, "image/jpeg");
    else if (strstr(path, ".csv")) httpd_resp_set_type(req, "text/csv; charset=utf-8");
    else                           httpd_resp_set_type(req, "application/octet-stream");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    static uint8_t chunk[4096];
    size_t n;
    while ((n = f.read(chunk, sizeof(chunk))) > 0) {
        if (httpd_resp_send_chunk(req, (const char*)chunk, n) != ESP_OK) {
            f.close();
            return ESP_FAIL;   // 客戶端中斷
        }
    }
    f.close();
    httpd_resp_send_chunk(req, nullptr, 0);
    return ESP_OK;
}

// 手動觸發站點批次上傳（測試用）：GET /upload
esp_err_t uploadHandler(httpd_req_t* req) {
    String r = uploadDashcamBatch();
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    String body = "{\"result\":\"" + r + "\"}";
    return httpd_resp_send(req, body.c_str(), HTTPD_RESP_USE_STRLEN);
}

// ═══════════════ HTTP 伺服器 ═══════════════
void startWebServer(bool withProvision) {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port    = 80;
    config.ctrl_port      = 32768;
    config.max_uri_handlers = 10;  // v3：SD 瀏覽 2 + 站點上傳 1 + 手機定位 1（共 8）
    if (httpd_start(&webServer, &config) != ESP_OK) {
        Serial.println("[HTTP] port80 啟動失敗");
        return;
    }
    httpd_uri_t status = { .uri = "/api/status", .method = HTTP_GET,
                           .handler = statusHandler, .user_ctx = nullptr };
    httpd_uri_t led    = { .uri = "/api/led", .method = HTTP_GET,
                           .handler = ledHandler, .user_ctx = nullptr };
    httpd_uri_t night  = { .uri = "/api/night", .method = HTTP_GET,
                           .handler = nightHandler, .user_ctx = nullptr };
    httpd_uri_t sdList = { .uri = "/sd", .method = HTTP_GET,
                           .handler = sdListHandler, .user_ctx = nullptr };
    httpd_uri_t sdFile = { .uri = "/sd/file", .method = HTTP_GET,
                           .handler = sdFileHandler, .user_ctx = nullptr };
    httpd_uri_t upload = { .uri = "/upload", .method = HTTP_GET,
                           .handler = uploadHandler, .user_ctx = nullptr };
    httpd_register_uri_handler(webServer, &status);
    httpd_register_uri_handler(webServer, &led);
    httpd_register_uri_handler(webServer, &night);
    httpd_register_uri_handler(webServer, &sdList);
    httpd_register_uri_handler(webServer, &sdFile);
    httpd_register_uri_handler(webServer, &upload);
    httpd_uri_t ploc   = { .uri = "/api/phoneloc", .method = HTTP_GET,
                           .handler = phoneLocHandler, .user_ctx = nullptr };
    httpd_register_uri_handler(webServer, &ploc);
    if (withProvision) {
        httpd_uri_t prov = { .uri = "/provision", .method = HTTP_POST,
                             .handler = provisionHandler, .user_ctx = nullptr };
        httpd_register_uri_handler(webServer, &prov);
    }
}

void startStreamServer() {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port    = 81;
    config.ctrl_port      = 32769;   // 與 port80 伺服器區隔，否則第二台會啟動失敗
    config.max_uri_handlers = 2;
    if (httpd_start(&streamServer, &config) != ESP_OK) {
        Serial.println("[HTTP] port81 串流伺服器啟動失敗");
        return;
    }
    httpd_uri_t stream = { .uri = "/stream", .method = HTTP_GET,
                           .handler = streamHandler, .user_ctx = nullptr };
    httpd_register_uri_handler(streamServer, &stream);
}

// ═══════════════ setup ═══════════════
void setup() {
    Serial.begin(115200);
    delay(1500);
    Serial.println("\n══════ bike-assist 正式韌體 ══════");

    // 1) 方向燈（LEDC PWM，開機即 50% 日行燈）
    indicator.begin(INDICATOR_LEFT_PIN, INDICATOR_RIGHT_PIN);

    // 1b) 方向燈搖桿開關（2026-07-19 新增，取代 roll 自動判斷方向；見下方 loop()）
    turnSwitch.begin(TURN_SWITCH_LEFT_PIN, TURN_SWITCH_RIGHT_PIN);

    // 1c) 倒車偵測（2026-07-20 新增）：70° 持續傾倒、5 分鐘未扶正 → BLE 示警
    fallDetector.begin(FALL_ANGLE_DEG, FALL_NOTICE_MS, FALL_EMERGENCY_MS);

    // 2) IMU（先掃 I2C 匯流排，便於判斷接線/位址）
    Wire.begin(IMU_SDA_PIN, IMU_SCL_PIN);
    int found = 0;
    for (uint8_t addr = 8; addr < 120; addr++) {
        Wire.beginTransmission(addr);
        if (Wire.endTransmission() == 0) {
            Serial.printf("[I2C] 找到裝置 0x%02X%s\n", addr,
                addr == 0x68 ? "（MPU6050 AD0=低，正確）" :
                addr == 0x69 ? "（MPU6050 AD0=高！請把 AD0 接 GND）" : "");
            found++;
        }
    }
    if (found == 0)
        Serial.println("[I2C] 匯流排無裝置 → 檢查 VCC/GND/SDA(G2)/SCL(G1) 接線焊點");
    imuOk = imu.begin(IMU_SDA_PIN, IMU_SCL_PIN, IMU_INT_PIN);
    Serial.printf("[IMU] %s\n", imuOk ? "OK" : "失敗");
    if (imuOk) imu.calibrate();

    // 3) GPS（只收，TX 不接）
    gps.begin(Serial1, 9600, GPS_RX_PIN, GPS_TX_PIN);

    // 4) 相機＋SD（CameraManager 內部先 camera 再 SD_MMC）
    if (!psramFound()) Serial.println("[PSRAM] ✗ 未偵測到！相機會失敗");
    camOk = camera.begin();
    sdOk  = camera.isSDReady();
    Serial.printf("[Camera] %s，SD %s\n", camOk ? "OK" : "失敗", sdOk ? "OK" : "無");

    // 5) WiFi（v3 多網路自動切換）：
    //    掃描環境 → NVS 配網那組優先，其次 KNOWN_NETS 清單中訊號最強者
    //    → 逐一嘗試連線 → 全部失敗才開配網熱點
    {
        String nvsSsid, nvsPass;
        bool hasNvs = loadCreds(nvsSsid, nvsPass);

        WiFi.mode(WIFI_STA);
        WiFi.setAutoReconnect(true);
        Serial.println("[WiFi] 掃描環境網路...");
        int n = WiFi.scanNetworks();

        // 收集「掃描有看到」的已知網路，依 RSSI 由強到弱排序（最多 8 個候選）
        struct Cand { const char* ssid; const char* pass; int rssi; };
        Cand cands[8];
        int candCount = 0;
        for (int i = 0; i < n && candCount < 8; i++) {
            String seen = WiFi.SSID(i);
            int rssi = WiFi.RSSI(i);
            if (hasNvs && seen == nvsSsid) {
                // NVS 配網組給 RSSI 加成，確保優先於同名之外的清單項目
                cands[candCount++] = { nvsSsid.c_str(), nvsPass.c_str(), rssi + 100 };
                continue;
            }
            for (int k = 0; k < KNOWN_NETS_COUNT; k++) {
                if (seen == KNOWN_NETS[k].ssid) {
                    cands[candCount++] = { KNOWN_NETS[k].ssid, KNOWN_NETS[k].pass, rssi };
                    break;
                }
            }
        }
        for (int i = 0; i < candCount - 1; i++)          // 簡單排序（候選數少）
            for (int j = i + 1; j < candCount; j++)
                if (cands[j].rssi > cands[i].rssi) { Cand t = cands[i]; cands[i] = cands[j]; cands[j] = t; }

        for (int i = 0; i < candCount && !staMode; i++) {
            Serial.printf("[WiFi] 嘗試 %s (RSSI %d)", cands[i].ssid, cands[i].rssi);
            WiFi.begin(cands[i].ssid, cands[i].pass);
            unsigned long t0 = millis();
            while (WiFi.status() != WL_CONNECTED && millis() - t0 < 12000) {
                delay(500); Serial.print(".");
            }
            Serial.println();
            staMode = (WiFi.status() == WL_CONNECTED);
            if (!staMode) WiFi.disconnect(true);
        }
        WiFi.scanDelete();
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

    // 6) 時間同步：時區固定台灣（UTC+8）；連線→NTP，離線→GPS 衛星時間
    setenv("TZ", TZ_TAIPEI, 1);
    tzset();
    sntp_set_time_sync_notification_cb(onNtpSync);
    if (staMode) {
        configTzTime(TZ_TAIPEI, "pool.ntp.org", "time.google.com", "time.cloudflare.com");
        Serial.println("[Time] NTP 校時啟動（連線）");
    } else {
        Serial.println("[Time] 離線：待 GPS 衛星時間校時");
    }

    // 7) HTTP 伺服器
    startWebServer(!staMode);
    if (camOk) startStreamServer();

    Serial.println("── APP 介面 ──────────────────────");
    Serial.printf("狀態  GET http://%s/api/status\n",
                  staMode ? WiFi.localIP().toString().c_str() : "192.168.4.1");
    Serial.println("燈    GET /api/led?mode=left|right|hazard|off");
    Serial.println("夜視  GET /api/night?mode=on|off");
    Serial.println("SD    GET /sd（瀏覽/下載行車紀錄）");
    Serial.println("上傳  GET /upload（站點批次上傳到伺服器，測試用）");
    Serial.println("串流  GET http://<ip>:81/stream");
    Serial.println("═════════════════════════════════");
}

// ═══════════════ loop ═══════════════
void loop() {
    unsigned long now = millis();

    // ── 感測更新 ──
    if (imuOk) {
        imu.update();
        accel.update(imu.getRawAX(), imu.getRawAY(), imu.getRawAZ());
    }
    gps.update();               // GPS 天線一律在背景解析，保持定位（見「位置來源」說明）
    turnSwitch.update();
    Position pos = currentPosition();   // 本輪統一使用的位置（手機優先、GPS 備援）
    float roll  = imu.getRoll();
    float pitch = imu.getPitch();

    // ── 倒車偵測（2026-07-20 v3；2026-07-22 改兩段式）──────────────
    // 倒下10秒 → 一般通知；持續到5分鐘仍未扶正 → 升級為緊急事故
    fallDetector.update(roll);
    // 系統時鐘未校時（無NTP網路也無GPS定位）時傳0，避免帶出1970年代的誤導時間；
    // 地圖時間仍以伺服器收到當下時間為準，這個欄位只是輔助參考。
    uint32_t nowEpoch = timeIsSet() ? (uint32_t)time(nullptr) : 0;
    if (fallDetector.noticeAlertPending()) {
        broadcastFallenBLE(pos.lat, pos.lon,
                           nowEpoch, BLE_EVT_FALLEN);
        fallDetector.consumeNoticeAlert();
    }
    if (fallDetector.emergencyAlertPending()) {
        broadcastFallenBLE(pos.lat, pos.lon,
                           nowEpoch, BLE_EVT_EMERGENCY);
        fallDetector.consumeEmergencyAlert();
    }
    // 廣播逾時（或已扶正）→ 停止廣播，省電
    if (bleAdvUntil && (now >= bleAdvUntil || !fallDetector.isFallen())) {
        if (bleInited) NimBLEDevice::getAdvertising()->stop();
        bleAdvUntil = 0;
        Serial.println("[Fall] BLE 廣播停止");
    }

    // ── 事件判定（碰撞鎖定、急煞暫亮）──
    AccelEvent ev = accel.getEvent();
    if (ev == AccelEvent::COLLISION) {
        hazardPermanent = true;
        camera.onEvent(DashcamEvent::COLLISION, accel.getMagnitude());
    } else if (ev == AccelEvent::BRAKE) {
        brakeHazardUntil = now + BRAKE_HAZARD_MS;
        camera.onEvent(DashcamEvent::EMERGENCY_BRAKE, accel.getMagnitude());
    }

    // ── 方向燈/警示燈（每 loop 呼叫一個驅動法以維持閃爍；優先序如下）──
    // 2026-07-19：改用手動搖桿開關為主，移除 roll 自動判斷方向
    // （roll 傾角仍用於 SHARP_TURN 行車紀錄判斷，見下方相機紀錄段落，不受影響）
    TurnDirection swDir = turnSwitch.getDirection();
    if (hazardPermanent || manualLed == ManualLed::HAZARD) {
        indicator.activateHazard();
    } else if (manualLed == ManualLed::LEFT) {
        indicator.activateLeft();
    } else if (manualLed == ManualLed::RIGHT) {
        indicator.activateRight();
    } else if (now < brakeHazardUntil) {
        indicator.activateHazard();
    } else if (swDir == TurnDirection::LEFT) {
        indicator.activateLeft();         // 搖桿撥左
    } else if (swDir == TurnDirection::RIGHT) {
        indicator.activateRight();        // 搖桿撥右
    } else {
        indicator.off();                  // 搖桿置中：不打方向燈
    }

    // ── 相機行車紀錄（限流取樣，降低與串流搶 frame buffer）──
    if (camOk && now - lastRecord >= RECORD_INTERVAL) {
        lastRecord = now;
        DashcamEvent dcEv = DashcamEvent::NORMAL;
        if      (ev == AccelEvent::COLLISION) dcEv = DashcamEvent::COLLISION;
        else if (ev == AccelEvent::BRAKE)     dcEv = DashcamEvent::EMERGENCY_BRAKE;
        else if (fabsf(roll) > 25.0f)         dcEv = DashcamEvent::SHARP_TURN;
        camera_fb_t* fb = camera.captureFrame();
        if (fb) {
            // v3：附 GPS 座標＋時間戳，供伺服器端地圖標記
            camera.recordFrame(fb, dcEv, roll, pitch, accel.getMagnitude(),
                               pos.lat, pos.lon,
                               pos.valid, currentTimeStr());

            // v3：影格上推伺服器（遠端觀看用，共用同一張 fb）
            // 用 static HTTPClient + setReuse 保持 keep-alive 持久連線，
            // 避免每張影格重新 TCP 握手（原本每次新建連線是卡頓主因之一）
            if (staMode && now >= pushBackoffUntil &&
                now - lastFramePush >= FRAME_PUSH_INTERVAL) {
                lastFramePush = now;
                static HTTPClient http;
                static bool httpInited = false;
                if (!httpInited) {
                    http.setReuse(true);            // keep-alive
                    http.setConnectTimeout(1000);   // 伺服器沒開時最多卡 1 秒
                    http.setTimeout(1500);
                    httpInited = true;
                }
                if (http.begin(serverBase() + "/api/frame")) {
                    http.addHeader("Content-Type", "image/jpeg");
                    int code = http.POST(fb->buf, fb->len);
                    // 不呼叫 http.end()：setReuse 下保留連線給下一張
                    if (code <= 0) {
                        http.end();                 // 失敗時才斷線重來
                        pushBackoffUntil = now + PUSH_BACKOFF_MS;
                        Serial.printf("[Push] 伺服器無回應(code=%d)，暫停上推 %ds\n",
                                      code, PUSH_BACKOFF_MS / 1000);
                    }
                }
            }
            camera.release(fb);
        }
    }

    // ── 每 1s 重置加速度事件統計窗口（維持煞車/碰撞偵測靈敏度）──
    if (now - lastAccelReset >= ACCEL_RESET_INTERVAL) {
        lastAccelReset = now;
        accel.resetWindow();
    }

    // ── 每 2s 嘗試用 GPS 校時（NTP 未校時前／離線時生效）──
    if (now - lastTimeSync >= 2000) {
        lastTimeSync = now;
        maybeSyncTimeFromGPS();
    }

    // ── Serial 摘要每 5s ──
    if (now - lastPrint >= STATUS_PRINT_INTERVAL) {
        lastPrint = now;
        Serial.printf("[狀態] %s(%s) | roll=%.1f pitch=%.1f G=%.2f(%s) | 位置=%s spd=%.1f | GPS chars=%lu fix=%d | LED=%s%s%s\n",
            currentTimeStr().c_str(), timeSourceStr(),
            roll, pitch, accel.getMagnitude(), accel.getEventStr(),
            pos.src, pos.speedKmh,
            gps.charsProcessed(), gps.isLocationValid(),
            currentLedStr(), hazardPermanent ? "(鎖定)" : "",
            fallDetector.isFallen()
                ? (fallDetector.fallenForMs() >= FALL_EMERGENCY_MS ? " | 倒車!緊急事故" :
                   fallDetector.fallenForMs() >= FALL_NOTICE_MS    ? " | 倒車!已通知"   :
                                                                      " | 倒車計時中")
                : "");
    }

    // ── v3：到站自動批次上傳（連上 STATION_SSID 後觸發一次）──
    if (staMode && !stationUploadDone && strlen(STATION_SSID) > 0 &&
        WiFi.SSID() == STATION_SSID) {
        stationUploadDone = true;   // 本次連線只跑一次，避免重複
        Serial.println("[Upload] 偵測到歸還站 WiFi，開始批次上傳...");
        uploadDashcamBatch();
    }

    // ── v3：感測數據上推伺服器（/api/data，每 5s）──
    if (staMode && now >= pushBackoffUntil &&
        now - lastDataPush >= DATA_PUSH_INTERVAL) {
        lastDataPush = now;
        char json[360];
        snprintf(json, sizeof(json),
            "{\"roll\":%.2f,\"pitch\":%.2f,"
            "\"gx\":%.2f,\"gy\":%.2f,\"gz\":%.2f,"
            "\"accelX\":%.2f,\"accelY\":%.2f,\"accelZ\":%.2f,"
            "\"accelEvent\":\"%s\","
            "\"lat\":%.6f,\"lon\":%.6f,\"alt\":%.1f,\"speed\":%.1f}",
            roll, pitch,
            imu.getGX(), imu.getGY(), imu.getGZ(),
            accel.getX(), accel.getY(), accel.getZ(),
            accel.getEventStr(),
            pos.lat, pos.lon,
            gps.isLocationValid() ? gps.getAltitude() : 0.0, pos.speedKmh);
        HTTPClient http;
        http.setConnectTimeout(1000);
        http.setTimeout(1500);
        if (http.begin(serverBase() + "/api/data")) {
            http.addHeader("Content-Type", "application/json");
            int code = http.POST((uint8_t*)json, strlen(json));
            http.end();
            if (code <= 0) pushBackoffUntil = now + PUSH_BACKOFF_MS;
        }
    }
}
