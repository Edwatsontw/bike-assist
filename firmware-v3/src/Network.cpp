#include "Network.h"
#include <WiFi.h>
#include <HTTPClient.h>

void NetworkManager::begin(const char* ssid, const char* pass, const char* serverUrl) {
    _ssid = ssid; _pass = pass; _url = serverUrl;
    WiFi.mode(WIFI_STA);
    WiFi.begin(_ssid.c_str(), _pass.c_str());
    Serial.printf("[Net] 連線 WiFi: %s ", _ssid.c_str());
    unsigned long t0 = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t0 < 15000) { delay(500); Serial.print("."); }
    if (WiFi.status() == WL_CONNECTED)
        Serial.printf("\n[Net] 已連線, IP = %s\n", WiFi.localIP().toString().c_str());
    else
        Serial.println("\n[Net] WiFi 逾時，loop 會自動重連");
}

bool NetworkManager::isConnected() const { return WiFi.status() == WL_CONNECTED; }

void NetworkManager::update() {
    if (WiFi.status() != WL_CONNECTED && millis() - _lastReconnect > 5000) {
        _lastReconnect = millis();
        Serial.println("[Net] WiFi 斷線，重連中...");
        WiFi.disconnect(); WiFi.begin(_ssid.c_str(), _pass.c_str());
    }
}

void NetworkManager::broadcast(const SensorPayload& p) {
    // 預留：未來可在此推播給手機（WebSocket / UDP），目前不做事
    (void)p;
}

void NetworkManager::postToServer(const SensorPayload& p) {
    if (WiFi.status() != WL_CONNECTED) return;

    char body[512];
    // 欄位名稱需與 server/models.py 的 SensorPayload 完全一致
    snprintf(body, sizeof(body),
        "{\"roll\":%.2f,\"pitch\":%.2f,\"gx\":%.2f,\"gy\":%.2f,\"gz\":%.2f,"
        "\"accelX\":%.2f,\"accelY\":%.2f,\"accelZ\":%.2f,\"accelEvent\":\"%s\","
        "\"lat\":%.6f,\"lon\":%.6f,\"alt\":%.1f,\"speed\":%.1f}",
        p.roll, p.pitch, p.gx, p.gy, p.gz,
        p.accelX, p.accelY, p.accelZ, p.accelEvent,
        p.latitude, p.longitude, p.altitude, p.speed);

    HTTPClient http;
    // 動態使用 gateway IP：熱點主機（電腦或手機）就是 gateway，
    // 不論熱點 IP 為何都能正確送達（手機熱點 IP 每次可能不同）
    String url = "http://" + WiFi.gatewayIP().toString() + ":5000/api/data";
    http.begin(url);
    http.addHeader("Content-Type", "application/json");
    int code = http.POST((uint8_t*)body, strlen(body));
    http.end();

    if (code <= 0)
        Serial.printf("[Net] POST 失敗: %s\n", http.errorToString(code).c_str());
    else if (code >= 300)
        Serial.printf("[Net] 伺服器回應碼: %d\n", code);
}