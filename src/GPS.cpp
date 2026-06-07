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