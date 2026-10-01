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
    void begin(HardwareSerial& serial, uint32_t baud, int rxPin = 21, int txPin = 47);

    // ── 更新資料（需在 loop 中持續呼叫） ────────────────────────
    void update();

    // ── 資料存取介面 ────────────────────────────────────────────
    double getLatitude() const;
    double getLongitude() const;
    double getAltitude() const;
    double getSpeed() const; // 公里/小時
    String getDateTime() const; // 格式: YYYY-MM-DD HH:MM:SS（UTC）
    time_t getEpoch();          // GPS 衛星 UTC 轉 Unix epoch；無效回 0（離線校時用）
                                // 註：TinyGPSPlus 的 year()/hour() 非 const，故本方法不能宣告 const

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