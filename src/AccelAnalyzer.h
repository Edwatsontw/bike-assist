#pragma once
#include <Arduino.h>

// ═══════════════════════════════════════════════════════════════
//  AccelAnalyzer — 加速度分析模組
//  職責：原始 IMU 加速度資料換算、合成加速度計算、事件偵測
//  無硬體依賴，純數學計算
// ═══════════════════════════════════════════════════════════════

enum class AccelEvent {
    NORMAL,     // 正常行駛
    BRAKE,      // 急煞車（合成加速度 > 2.0G）
    COLLISION   // 碰撞  （合成加速度 > 3.0G）
};

class AccelAnalyzer {
public:
    // ── 建構子：初始化所有成員 ──────────────────────────────────
    AccelAnalyzer();

    // ── 主要更新介面 ────────────────────────────────────────────
    // 傳入 IMU 原始 int16_t 值（MPU-6050 ±2G 量程，16384 LSB/G）
    void update(int16_t ax, int16_t ay, int16_t az);

    // ── 資料讀取介面 ────────────────────────────────────────────
    float      getX()         const { return _x; }         // 單軸 G 值
    float      getY()         const { return _y; }
    float      getZ()         const { return _z; }
    float      getMagnitude() const { return _magnitude; } // 合成加速度 G
    float      getDelta()     const { return _delta; }     // 本次差值
    float      getMaxInWindow() const { return _maxAccelInWindow; }
    AccelEvent getEvent()     const { return _event; }

    // ── 工具介面 ────────────────────────────────────────────────
    // 重設差值窗口（建議每個統計週期呼叫一次，如每 500ms）
    void resetWindow();

    // 將 AccelEvent 轉為可讀字串（方便 Serial 輸出）
    static const char* eventToString(AccelEvent event);
    const char* getEventStr() const { return eventToString(_event); }

private:
    // ── 換算常數 ────────────────────────────────────────────────
    static constexpr float LSB_PER_G          = 16384.0f; // MPU-6050 ±2G

    // ── 事件閾值 ────────────────────────────────────────────────
    static constexpr float BRAKE_THRESHOLD     = 2.0f;    // G
    static constexpr float COLLISION_THRESHOLD = 3.0f;    // G

    // ── 內部狀態 ────────────────────────────────────────────────
    float      _x;
    float      _y;
    float      _z;
    float      _magnitude;
    float      _lastMagnitude;
    float      _delta;
    float      _maxAccelInWindow;
    AccelEvent _event;

    // ── 私有計算函式 ────────────────────────────────────────────
    void _calcMagnitude();   // 計算合成加速度
    void _calcDelta();       // 計算差值並更新窗口最大值
    void _classifyEvent();   // 根據合成加速度判斷事件
};