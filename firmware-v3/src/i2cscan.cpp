// ══════════════════════════════════════════════════════════════════
//  i2cscan.cpp — I2C 匯流排掃描（診斷 MPU6050 收不到訊號用）
//  燒錄：pio run -e i2cscan -t upload -t monitor
//
//  每 3 秒掃描一次 0x01–0x7E，印出所有回應的位址。
//  MPU6050：AD0 接 GND（或浮接，模組通常內部下拉）→ 0x68
//           AD0 接 3.3V                          → 0x69
//
//  若完全掃不到任何裝置：檢查 GND 是否共地、SDA/SCL 是否真的接在
//  下面這兩支腳、焊點是否虛焊（可用三用電表量通斷）。
//  若掃得到但位址不是 0x68/0x69：可能是別顆 I2C 裝置或位址腳接錯。
// ══════════════════════════════════════════════════════════════════
#include <Arduino.h>
#include <Wire.h>

#define I2C_SDA_PIN 2   // 依 2026-07-09 實際接線
#define I2C_SCL_PIN 1

void setup() {
    Serial.begin(115200);
    delay(1500);
    Serial.println("\n=== I2C Scanner（IMU 診斷用）===");
    Serial.printf("SDA=GPIO%d  SCL=GPIO%d\n", I2C_SDA_PIN, I2C_SCL_PIN);
    Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
}

void loop() {
    Serial.println("── 掃描中 ──");
    uint8_t count = 0;
    for (uint8_t addr = 1; addr < 127; addr++) {
        Wire.beginTransmission(addr);
        uint8_t err = Wire.endTransmission();
        if (err == 0) {
            Serial.printf("找到裝置：0x%02X", addr);
            if (addr == 0x68) Serial.print("  ← MPU6050 預設位址（AD0=低/浮接）");
            if (addr == 0x69) Serial.print("  ← MPU6050 備用位址（AD0=高）");
            Serial.println();
            count++;
        } else if (err == 4) {
            Serial.printf("位址 0x%02X：讀取錯誤\n", addr);
        }
    }
    if (count == 0)
        Serial.println("=== 沒有找到任何 I2C 裝置！檢查共地／接線／焊點 ===");
    else
        Serial.printf("=== 掃描完成，共 %u 個裝置 ===\n", count);

    delay(3000);
}
