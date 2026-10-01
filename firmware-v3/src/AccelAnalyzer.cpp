#include "AccelAnalyzer.h"

// ═══════════════════════════════════════════════════════════════
//  AccelAnalyzer 實作
// ═══════════════════════════════════════════════════════════════

// ── 建構子 ──────────────────────────────────────────────────────
AccelAnalyzer::AccelAnalyzer()
    : _x(0.0f)
    , _y(0.0f)
    , _z(0.0f)
    , _magnitude(0.0f)
    , _lastMagnitude(0.0f)
    , _delta(0.0f)
    , _maxAccelInWindow(0.0f)
    , _event(AccelEvent::NORMAL)
{}

// ── update() ────────────────────────────────────────────────────
// 每個 loop 呼叫一次，傳入 IMU 原始值
// 執行順序：換算 → 合成 → 差值 → 事件分類
void AccelAnalyzer::update(int16_t ax, int16_t ay, int16_t az) {
    // 步驟 1：原始值 → G 值（來源：main.cpp 的 ax / 16384.0f）
    _x = static_cast<float>(ax) / LSB_PER_G;
    _y = static_cast<float>(ay) / LSB_PER_G;
    _z = static_cast<float>(az) / LSB_PER_G;

    // 步驟 2：計算合成加速度
    _calcMagnitude();

    // 步驟 3：計算差值並更新窗口最大值
    _calcDelta();

    // 步驟 4：事件分類
    _classifyEvent();
}

// ── resetWindow() ───────────────────────────────────────────────
// 重設窗口最大差值（對應 main.cpp 的 maxAccelInWindow = 0.0f）
void AccelAnalyzer::resetWindow() {
    _maxAccelInWindow = 0.0f;
}

// ── eventToString() ─────────────────────────────────────────────
const char* AccelAnalyzer::eventToString(AccelEvent event) {
    switch (event) {
        case AccelEvent::NORMAL:    return "NORMAL";
        case AccelEvent::BRAKE:     return "BRAKE";
        case AccelEvent::COLLISION: return "COLLISION";
        default:                    return "UNKNOWN";
    }
}

// ── _calcMagnitude() ────────────────────────────────────────────
// 合成加速度 = sqrt(x² + y² + z²)
// 來源：main.cpp 的 sqrtf(accelX*accelX + accelY*accelY + accelZ*accelZ)
void AccelAnalyzer::_calcMagnitude() {
    _magnitude = sqrtf(_x * _x + _y * _y + _z * _z);
}

// ── _calcDelta() ────────────────────────────────────────────────
// 計算本次與上次合成加速度的差值，並追蹤窗口內最大值
// 來源：main.cpp 的 abs(accelMagnitude - lastAccelMagnitude)
void AccelAnalyzer::_calcDelta() {
    _delta = fabsf(_magnitude - _lastMagnitude);

    if (_delta > _maxAccelInWindow) {
        _maxAccelInWindow = _delta;
    }

    _lastMagnitude = _magnitude; // 更新歷史值，供下次計算使用
}

// ── _classifyEvent() ────────────────────────────────────────────
// 根據合成加速度大小判斷事件等級
// 來源：main.cpp 的 if (accelMagnitude > ACCEL_THRESHOLD_COLLISION) ...
// 注意：COLLISION 優先於 BRAKE（先判斷較嚴重的情況）
void AccelAnalyzer::_classifyEvent() {
    if (_magnitude > COLLISION_THRESHOLD) {
        _event = AccelEvent::COLLISION;
    } else if (_magnitude > BRAKE_THRESHOLD) {
        _event = AccelEvent::BRAKE;
    } else {
        _event = AccelEvent::NORMAL;
    }
}