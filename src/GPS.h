#pragma once

#include <Arduino.h>
#include <TinyGPS++.h>

// ═══════════════════════════════════════════════════════════════
//  GPSModule — GPS 模組
//  職責：解析來自 U-blox NEO-6M 的 NMEA 資料，並提供位置與時間資訊
// ═══════════════════════════════════════════════════════════════

class GPSModule {
public:
    // ── 建構子 ──────────────────────────────────────────────────
    GPSModule();

    // ── 初始化 ──────────────────────────────────────────────────
    void begin(HardwareSerial& serial, uint32_t baud, int rxPin = 18, int txPin = 17);

    // ── 更新資料（需在 loop 中持續呼叫） ────────────────────────
    void update();

    // ── 資料存取介面 ────────────────────────────────────────────
    double getLatitude() const;
    double getLongitude() const;
    double getAltitude() const;
    double getSpeed() const; // 公里/小時
    String getDateTime() const; // 格式: YYYY-MM-DD HH:MM:SS

    // ── 狀態檢查 ────────────────────────────────────────────────
    bool isLocationValid() const;
    bool isTimeValid() const;
    uint32_t charsProcessed()  const;
    uint32_t sentencesWithFix() const;

private:
    TinyGPSPlus gps;
    HardwareSerial* gpsSerial;

    double latitude;
    double longitude;
    double altitude;
    double speed;
    String dateTime;

    void _updateLocation();
    void _updateTime();
};