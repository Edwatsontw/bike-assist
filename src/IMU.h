#pragma once
#include <Arduino.h>
#include <MPU6050.h>
#include <Wire.h>

class IMUManager {
public:
    // 互補濾波器權重（陀螺儀佔 98%）
    static constexpr float ALPHA = 0.98f;

    bool begin(uint8_t sda = 8, uint8_t scl = 9, uint8_t intPin = 7);
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
};
