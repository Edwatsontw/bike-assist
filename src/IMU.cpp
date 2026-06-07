#include "IMU.h"

bool IMUManager::begin(uint8_t sda, uint8_t scl, uint8_t intPin) {
    (void)intPin;
    Wire.begin(sda, scl);
    _mpu.initialize();

    if (!_mpu.testConnection()) {
        Serial.println("[IMU] MPU-6050 連線失敗");
        return false;
    }

    Serial.println("[IMU] MPU-6050 連線成功");

    // 設定 DLPF 截止頻率 ~44Hz，過濾高頻震動
    _mpu.setDLPFMode(MPU6050_DLPF_BW_42);

    calibrate();
    _lastTime = millis();
    return true;
}

void IMUManager::calibrate() {
    Serial.println("[IMU] 開始零點校正，請保持靜止...");

    const int samples = 200;
    long sumGX = 0, sumGY = 0, sumGZ = 0;

    for (int i = 0; i < samples; i++) {
        int16_t ax, ay, az, gx, gy, gz;
        _mpu.getMotion6(&ax, &ay, &az, &gx, &gy, &gz);
        sumGX += gx;
        sumGY += gy;
        sumGZ += gz;
        delay(5); // 總共約 1 秒
    }

    _offsetGX = sumGX / samples;
    _offsetGY = sumGY / samples;
    _offsetGZ = sumGZ / samples;

    Serial.printf("[IMU] 校正完成 | 偏移 GX:%d GY:%d GZ:%d\n",
                  _offsetGX, _offsetGY, _offsetGZ);
}

void IMUManager::update() {
    unsigned long now = millis();
    float dt = (now - _lastTime) / 1000.0f; // 轉換為秒
    _lastTime = now;

    // 讀取原始數值
    _mpu.getMotion6(&_ax, &_ay, &_az, &_gx, &_gy, &_gz);

    // 套用零點偏移
    float gxCal = (_gx - _offsetGX) / 131.0f; // 轉換為 °/s
    float gyCal = (_gy - _offsetGY) / 131.0f;

    // 加速度計計算角度（單位：度）
    float accelPitch = atan2f(_ay, _az) * 180.0f / PI;
    float accelRoll  = atan2f(-_ax, _az) * 180.0f / PI;

    // 互補濾波器：α=0.98 陀螺儀 + (1-α) 加速度計
    _pitch = ALPHA * (_pitch + gxCal * dt) + (1.0f - ALPHA) * accelPitch;
    _roll  = ALPHA * (_roll  + gyCal * dt) + (1.0f - ALPHA) * accelRoll;
}
