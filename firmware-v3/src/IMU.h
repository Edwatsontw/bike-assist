#pragma once
#include <Arduino.h>
#include <MPU6050.h>
#include <Wire.h>

class IMUManager {
public:
    // 互補濾波器權重（陀螺儀佔 98%）
    static constexpr float ALPHA = 0.98f;

    bool begin(uint8_t sda = 2, uint8_t scl = 1, uint8_t intPin = 14);
    void update();         // 每個 loop 呼叫一次
    void calibrate();      // 靜止 1 秒自動校正零點

    float getPitch() const { return _pitch; }
    float getRoll()  const { return _roll;  }

    float getGX() const { return (_gx - _offsetGX) / 131.0f; }
    float getGY() const { return (_gy - _offsetGY) / 131.0f; }
    float getGZ() const { return (_gz - _offsetGZ) / 131.0f; }

    int16_t getRawAX() const { return _ax; }
    int16_t getRawAY() const { return _ay; }
    int16_t getRawAZ() const { return _az; }

    // v3（2026-07-20）：偵測 I2C 讀取失敗。WiFi 傳輸尖峰電流可能造成瞬間電壓
    // 下降，干擾 I2C 匯流排（已知的 ESP32 常見現象），失敗時 getMotion6()
    // 讀回全 0（靜止狀態下 az 不可能為 0，物理上代表這次讀取無效）。
    // 失敗時 update() 會跳過覆寫，沿用上次有效值，並讓這裡回 true 供
    // main.cpp／AccelAnalyzer 判斷「這次數據不可信」。
    bool isStale() const { return _staleCount > 0; }
    uint32_t getStaleCount() const { return _staleCount; }

private:
    MPU6050 _mpu;

    // 原始數值
    int16_t _ax = 0, _ay = 0, _az = 0;
    int16_t _gx = 0, _gy = 0, _gz = 0;

    // 濾波後角度
    float _pitch = 0.0f;
    float _roll  = 0.0f;

    // 零點偏移
    int16_t _offsetGX = 0, _offsetGY = 0, _offsetGZ = 0;

    unsigned long _lastTime = 0;
    uint32_t _staleCount = 0;   // 累計 I2C 失敗次數（診斷用，不重置）
};
