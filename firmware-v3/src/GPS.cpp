#include "GPS.h"

// ── 建構子 ──────────────────────────────────────────────────
GPSModule::GPSModule()
    : gpsSerial(nullptr), latitude(0.0), longitude(0.0), altitude(0.0), speed(0.0), dateTime("") {}

// ── 初始化 ──────────────────────────────────────────────────
void GPSModule::begin(HardwareSerial& serial, uint32_t baud, int rxPin, int txPin) {
    gpsSerial = &serial;
    gpsSerial->begin(baud, SERIAL_8N1, rxPin, txPin);
    Serial.printf("[GPS] UART 初始化完成  RX:%d TX:%d Baud:%u\n", rxPin, txPin, baud);
}

// ── 更新資料 ────────────────────────────────────────────────
void GPSModule::update() {
    while (gpsSerial->available() > 0) {
        gps.encode(gpsSerial->read());
    }

    _updateLocation();
    _updateTime();
}

// ── 資料存取介面 ────────────────────────────────────────────
double GPSModule::getLatitude() const {
    return latitude;
}

double GPSModule::getLongitude() const {
    return longitude;
}

double GPSModule::getAltitude() const {
    return altitude;
}

double GPSModule::getSpeed() const {
    return speed;
}

String GPSModule::getDateTime() const {
    return dateTime;
}

// ── getEpoch() ──────────────────────────────────────────────
// 把 GPS 的 UTC 日期時間換成 Unix epoch（秒）。
// 用 Howard Hinnant 的 days_from_civil 演算法，全日期正確、無閏年 bug。
// GPS 給的是 UTC，故直接得到 UTC epoch，不受系統時區影響。
time_t GPSModule::getEpoch() {
    if (!gps.date.isValid() || !gps.time.isValid()) return 0;

    int  y = gps.date.year();
    int  m = gps.date.month();
    int  d = gps.date.day();
    if (y < 2020) return 0;   // 尚未取得有效日期

    // days_from_civil：回傳自 1970-01-01 起的天數
    int yy = y - (m <= 2);
    long era = (yy >= 0 ? yy : yy - 399) / 400;
    unsigned yoe = (unsigned)(yy - era * 400);
    unsigned doy = (153u * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    long days = era * 146097L + (long)doe - 719468;

    return (time_t)days * 86400
         + gps.time.hour()   * 3600
         + gps.time.minute() * 60
         + gps.time.second();
}

bool GPSModule::isLocationValid() const {
    return gps.location.isValid();
}

bool GPSModule::isTimeValid() const {
    return gps.date.isValid() && gps.time.isValid();
}

uint32_t GPSModule::charsProcessed() const {
    return gps.charsProcessed();
}

uint32_t GPSModule::sentencesWithFix() const {
    return gps.sentencesWithFix();
}

// ── 內部更新函數 ────────────────────────────────────────────
void GPSModule::_updateLocation() {
    if (gps.location.isValid()) {
        latitude = gps.location.lat();
        longitude = gps.location.lng();
        altitude = gps.altitude.meters();
        speed = gps.speed.kmph();
    }
}

void GPSModule::_updateTime() {
    if (gps.date.isValid() && gps.time.isValid()) {
        char buffer[20];
        snprintf(buffer, sizeof(buffer), "%04d-%02d-%02d %02d:%02d:%02d",
                 gps.date.year(), gps.date.month(), gps.date.day(),
                 gps.time.hour(), gps.time.minute(), gps.time.second());
        dateTime = String(buffer);
    }
}