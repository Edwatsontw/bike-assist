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
    void begin(const char* apSSID, const char* apPassword);
    void update();
    void broadcast(const SensorPayload& payload);
};
