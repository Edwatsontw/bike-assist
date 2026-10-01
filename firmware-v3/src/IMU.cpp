#include "IMU.h"

bool IMUManager::begin(uint8_t sda, uint8_t scl, uint8_t intPin) {
    (void)intPin;
    Wire.begin(sda, scl);
    _mpu.initialize();

    // 讀 WHO_AM_I（暫存器 0x75）全 byte 用於診斷。
    // 掃描看得到 0x68 但 testConnection 失敗，多半是相容/仿製晶片
    // （常見 MPU6500 等），WHO_AM_I 不在函式庫白名單（0x34/0x0C/0x3A）。
    Wire.beginTransmission(0x68);
    Wire.write(0x75);
    Wire.endTransmission(false);
    Wire.requestFrom((uint8_t)0x68, (uint8_t)1);
    uint8_t whoami = Wire.available() ? Wire.read() : 0xFF;
    Serial.printf("[IMU] WHO_AM_I(0x75) = 0x%02X\n", whoami);

    if (!_mpu.testConnection()) {
        Serial.println("[IMU] testConnection 失敗 → 改用實際讀值驗證");
        int16_t ax, ay, az, gx, gy, gz;
        _mpu.getMotion6(&ax, &ay, &az, &gx, &gy, &gz);
        bool alive = !(ax == 0 && ay == 0 && az == 0 && gx == 0 && gy == 0 && gz == 0);
        Serial.printf("[IMU] 讀值 ax=%d ay=%d az=%d gx=%d gy=%d gz=%d → %s\n",
                      ax, ay, az, gx, gy, gz,
                      alive ? "有資料，視為連上（相容晶片）" : "全 0，感測器真的沒反應");
        if (!alive) return false;
    } else {
        Serial.println("[IMU] MPU-6050 連線成功");
    }

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

    // 讀取原始數值（讀進暫存變數，失敗時不覆寫既有的 _ax.._gz）
    int16_t ax, ay, az, gx, gy, gz;
    _mpu.getMotion6(&ax, &ay, &az, &gx, &gy, &gz);

    // v3：I2C 失敗偵測——getMotion6() 沒有回傳值可查錯誤，但靜止狀態下
    // az（重力軸）不可能為 0，六軸同時全 0 幾乎必定是這次 I2C 交易失敗
    // （常見成因：WiFi 傳輸尖峰電流造成瞬間電壓下降干擾匯流排）。
    // 失敗就跳過覆寫、沿用上次有效值，避免把 G=0.00 這種不可能的假資料
    // 送進 AccelAnalyzer／狀態輸出／伺服器上傳。
    bool allZero = (ax == 0 && ay == 0 && az == 0 && gx == 0 && gy == 0 && gz == 0);
    if (allZero) {
        _staleCount++;
        // 只在第 1、10、之後每 50 次印一次，避免連續失敗時洗版
        if (_staleCount == 1 || _staleCount == 10 || _staleCount % 50 == 0) {
            Serial.printf("[IMU] I2C 讀取失敗（累計 %lu 次），沿用上次數值\n",
                          (unsigned long)_staleCount);
        }
        return;   // 沿用舊值，pitch/roll/accel 這次不更新
    }
    _ax = ax; _ay = ay; _az = az;
    _gx = gx; _gy = gy; _gz = gz;

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
