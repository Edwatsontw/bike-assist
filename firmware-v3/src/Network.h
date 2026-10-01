#pragma once
#include <Arduino.h>

struct SensorPayload {
    float       roll;
    float       pitch;
    float       gx;
    float       gy;
    float       gz;
    float       accelX;
    float       accelY;
    float       accelZ;
    const char* accelEvent;
    double      latitude;
    double      longitude;
    double      altitude;
    double      speed;
};

class NetworkManager {
public:
    // STA 模式：連上家裡/熱點 WiFi，並把感測資料 POST 到 FastAPI 伺服器
    void begin(const char* ssid, const char* pass, const char* serverUrl);
    void update();                                   // 維持／自動重連 WiFi
    void broadcast(const SensorPayload& payload);    // 預留：未來推播給手機
    void postToServer(const SensorPayload& payload); // HTTP POST 到 /api/data
    bool isConnected() const;

private:
    String        _ssid;
    String        _pass;
    String        _url;
    unsigned long _lastReconnect = 0;
};
