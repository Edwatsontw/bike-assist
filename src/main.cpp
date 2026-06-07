#include <Arduino.h>
#include "IMU.h"
#include "Audio.h"
#include "AccelAnalyzer.h"
#include "GPS.h"
#include "Network.h"
#include "IndicatorModule.h"

#define AP_SSID     "BikeAssist"
#define AP_PASSWORD "12345678"

#define IMU_SEND_INTERVAL 1000   // IMU + 陀螺儀：每 1 秒送出
#define GPS_SEND_INTERVAL 5000   // GPS 座標：每 5 秒送出

// ── GPIO ─────────────────────────────────────
// Camera 佔用 GPIO 4-18（DVP 介面），以下腳位均已迴避
#define IMU_SDA_PIN          1   // I²C SDA（原 8，已讓出給 Camera D2）
#define IMU_SCL_PIN          2   // I²C SCL（原 9，已讓出給 Camera D1）
#define IMU_INT_PIN          3   // 中斷（原 7，已讓出給 Camera HREF）
#define GPS_RX_PIN          41   // UART RX（原 18，已讓出給 Camera D5）
#define GPS_TX_PIN          42   // UART TX（原 17，已讓出給 Camera D6）
#define INDICATOR_LEFT_PIN  43   // 左方向燈 / 警示燈（紅色 LED + 220Ω）
#define INDICATOR_RIGHT_PIN 44   // 右方向燈 / 警示燈（紅色 LED + 220Ω）

// ── 音效頻率 ──────────────────────────────────────────────────────
#define TURN_BEEP_HZ    1000  // 方向燈同步音（短促高音）
#define TURN_BEEP_MS      70
#define HAZARD_BEEP_HZ   600  // 警示燈同步音（較低沉）
#define HAZARD_BEEP_MS   120

// ── 模組 ─────────────────────────────────────
IMUManager      imu;
AudioManager    audio;
AccelAnalyzer   accel;
GPSModule       gps;
NetworkManager  network;
IndicatorModule indicator;

// ── 計時器 ───────────────────────────────────
unsigned long lastImuPrint  = 0;
unsigned long lastImuSend   = 0;
unsigned long lastGpsSend   = 0;

// ── GPS 快取（5s 才更新一次，1s 一起送出）─────
double cachedLat = 0, cachedLon = 0, cachedAlt = 0, cachedSpeed = 0;

// ── 警示燈狀態 ────────────────────────────────
// true  = 碰撞觸發（A）→ 需手動解除
// false = 急煞觸發（E）→ 速度恢復後自動解除
bool hazardPermanent = false;

// ─────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("=== 腳踏車智慧輔助系統 啟動中 ===");

    imu.begin(IMU_SDA_PIN, IMU_SCL_PIN, IMU_INT_PIN);
    audio.begin();
    gps.begin(Serial1, 9600, GPS_RX_PIN, GPS_TX_PIN);
    network.begin(AP_SSID, AP_PASSWORD);
    indicator.begin(INDICATOR_LEFT_PIN, INDICATOR_RIGHT_PIN);

    audio.beep(440, 300);
    Serial.println("=== 系統就緒 ===");
}

// ─────────────────────────────────────────────
void loop() {
    unsigned long now = millis();

    // ── IMU ──────────────────────────────────
    imu.update();
    float roll  = imu.getRoll();
    float pitch = imu.getPitch();
    float gx    = imu.getGX();
    float gy    = imu.getGY();
    float gz    = imu.getGZ();

    accel.update(imu.getRawAX(), imu.getRawAY(), imu.getRawAZ());
    AccelEvent accelEvent = accel.getEvent();

    // ── 方向燈 / 警示燈 + 音效同步 ───────────────
    indicator.update(roll);
    if (indicator.justBlinkedOn()) {
        if (indicator.isHazard()) {
            audio.beep(HAZARD_BEEP_HZ, HAZARD_BEEP_MS);  // 警示：低沉雙閃音
        } else {
            audio.beep(TURN_BEEP_HZ, TURN_BEEP_MS);      // 方向燈：短促高音
        }
    }

    // ── 自動警示燈觸發（A + E）────────────────────
    // A：碰撞（Magnitude > 3G）→ 永久警示，需手動解除
    if (accelEvent == AccelEvent::COLLISION && !indicator.isHazard()) {
        indicator.activateHazard();
        hazardPermanent = true;
        Serial.println("[警示] ⚠ 碰撞偵測 → 啟動警示燈（手動解除）");
    }
    // E：急煞（> 2G）+ GPS 速度 < 5 km/h → 暫時警示
    if (accelEvent == AccelEvent::BRAKE &&
        gps.isLocationValid() && gps.getSpeed() < 5.0 &&
        !indicator.isHazard()) {
        indicator.activateHazard();
        hazardPermanent = false;
        Serial.println("[警示] 急煞+低速 → 啟動警示燈（自動解除）");
    }
    // E 自動解除：速度 > 15 km/h 且加速度正常（碰撞觸發則不解除）
    if (indicator.isHazard() && !hazardPermanent &&
        gps.isLocationValid() && gps.getSpeed() > 15.0 &&
        accelEvent == AccelEvent::NORMAL) {
        indicator.off();
        Serial.println("[警示] 速度恢復正常 → 解除警示燈");
    }

    float accelX = accel.getX();
    float accelY = accel.getY();
    float accelZ = accel.getZ();

    // ── GPS：每次 loop 讀 UART，每 5s 更新快取 ─
    gps.update();
    if (now - lastGpsSend >= GPS_SEND_INTERVAL) {
        lastGpsSend = now;

        if (gps.isLocationValid()) {
            cachedLat   = gps.getLatitude();
            cachedLon   = gps.getLongitude();
            cachedAlt   = gps.getAltitude();
            cachedSpeed = gps.getSpeed();
            Serial.printf("[GPS] Lat: %.6f  Lon: %.6f  Alt: %.1f m  Speed: %.1f km/h\n",
                          cachedLat, cachedLon, cachedAlt, cachedSpeed);
        } else {
            Serial.printf("[GPS] 等待定位... (已收字元: %lu, 有效句子: %lu)\n",
                          gps.charsProcessed(), gps.sentencesWithFix());
        }
    }

    // ── Serial：每 500ms 印出 IMU + 陀螺儀 ───
    if (now - lastImuPrint >= 500) {
        lastImuPrint = now;

        Serial.printf("[IMU]  Pitch: %6.2f°  Roll: %6.2f°\n", pitch, roll);
        Serial.printf("[陀螺] gX: %6.2f°/s  gY: %6.2f°/s  gZ: %6.2f°/s\n", gx, gy, gz);
        Serial.printf("[加速] X: %5.2fG  Y: %5.2fG  Z: %5.2fG  合成: %5.2fG  事件: %s\n",
                      accelX, accelY, accelZ,
                      accel.getMagnitude(), AccelAnalyzer::eventToString(accelEvent));

        // 碰撞即時警示音（警示燈啟動後由閃爍同步音取代，不重複）
        if (accelEvent == AccelEvent::COLLISION && !indicator.isHazard()) {
            audio.beep(880, 200);
        }

        accel.resetWindow();
    }

    // ── 網路：每 1s 廣播 IMU + 最新 GPS 快取給手機 ─
    if (now - lastImuSend >= IMU_SEND_INTERVAL) {
        lastImuSend = now;
        network.update();

        SensorPayload payload = {
            .roll       = roll,
            .pitch      = pitch,
            .gx         = gx,
            .gy         = gy,
            .gz         = gz,
            .accelX     = accelX,
            .accelY     = accelY,
            .accelZ     = accelZ,
            .accelEvent = accel.getEventStr(),
            .latitude   = cachedLat,
            .longitude  = cachedLon,
            .altitude   = cachedAlt,
            .speed      = cachedSpeed
        };

        network.broadcast(payload);
    }

    delay(10);
}
