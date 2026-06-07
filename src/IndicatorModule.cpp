#include "IndicatorModule.h"

// ═══════════════════════════════════════════════════════════════
//  IndicatorModule 實作
// ═══════════════════════════════════════════════════════════════

// ── 建構子 ──────────────────────────────────────────────────────
IndicatorModule::IndicatorModule()
    : _pinLeft(-1)
    , _pinRight(-1)
    , _blinkState(false)
    , _lastBlink(0)
    , _blinkJustOn(false)
    , _currentDirection(TurnDirection::NONE)
{}

// ── begin() ─────────────────────────────────────────────────────
void IndicatorModule::begin(int pinLeft, int pinRight) {
    _pinLeft  = pinLeft;
    _pinRight = pinRight;

    pinMode(_pinLeft,  OUTPUT);
    pinMode(_pinRight, OUTPUT);
    digitalWrite(_pinLeft,  LOW);
    digitalWrite(_pinRight, LOW);
}

// ── update() ────────────────────────────────────────────────────
// 每個 loop 呼叫一次，根據 roll 角度判斷方向並驅動閃爍
// 注意：警示燈（HAZARD）啟動後 roll 不會覆蓋，需呼叫 off() 才能退出
void IndicatorModule::update(float roll) {
    // 警示燈模式優先，roll 偵測不介入
    if (_currentDirection == TurnDirection::HAZARD) {
        _handleBlink(TurnDirection::HAZARD);
        return;
    }

    if (roll < -TURN_DEG) {
        _currentDirection = TurnDirection::LEFT;
    } else if (roll > TURN_DEG) {
        _currentDirection = TurnDirection::RIGHT;
    } else {
        _currentDirection = TurnDirection::NONE;
    }

    _handleBlink(_currentDirection);
}

// ── activateLeft() ──────────────────────────────────────────────
void IndicatorModule::activateLeft() {
    _currentDirection = TurnDirection::LEFT;
    _handleBlink(_currentDirection);
}

// ── activateRight() ─────────────────────────────────────────────
void IndicatorModule::activateRight() {
    _currentDirection = TurnDirection::RIGHT;
    _handleBlink(_currentDirection);
}

// ── activateHazard() ────────────────────────────────────────────
// 啟動雙閃警示燈，兩側 LED 同步閃爍
// 呼叫 off() 才能退出此模式
void IndicatorModule::activateHazard() {
    _currentDirection = TurnDirection::HAZARD;
    _handleBlink(TurnDirection::HAZARD);
}

// ── off() ───────────────────────────────────────────────────────
// 關閉兩側方向燈並重設閃爍狀態
void IndicatorModule::off() {
    _currentDirection = TurnDirection::NONE;
    _blinkState       = false;
    digitalWrite(_pinLeft,  LOW);
    digitalWrite(_pinRight, LOW);
}

// ── isTiltWarning() ─────────────────────────────────────────────
// 回傳 roll 角度是否超過傾斜警告閾值
bool IndicatorModule::isTiltWarning(float roll) const {
    return fabsf(roll) > TILT_WARN_DEG;
}

const char* IndicatorModule::getDirectionStr() const {
    switch (_currentDirection) {
        case TurnDirection::LEFT:   return "LEFT";
        case TurnDirection::RIGHT:  return "RIGHT";
        case TurnDirection::HAZARD: return "HAZARD";
        default:                    return "NONE";
    }
}

// ── _handleBlink() ──────────────────────────────────────────────
// 內部閃爍邏輯，來源：main.cpp handleBlinker() 的閃爍計時區塊
void IndicatorModule::_handleBlink(TurnDirection dir) {
    // 沒有轉向 -> 兩燈熄滅
    if (dir == TurnDirection::NONE) {
        digitalWrite(_pinLeft,  LOW);
        digitalWrite(_pinRight, LOW);
        _blinkState = false;
        return;
    }

    unsigned long now = millis();

    if (now - _lastBlink >= BLINK_INTERVAL) {
        _lastBlink  = now;
        _blinkState = !_blinkState;

        // LED 剛切換為亮時設旗標，供 justBlinkedOn() 讀取
        _blinkJustOn = _blinkState;

        if (dir == TurnDirection::LEFT) {
            digitalWrite(_pinLeft,  _blinkState ? HIGH : LOW);
            digitalWrite(_pinRight, LOW);
        } else if (dir == TurnDirection::RIGHT) {
            digitalWrite(_pinRight, _blinkState ? HIGH : LOW);
            digitalWrite(_pinLeft,  LOW);
        } else {
            // HAZARD：左右同步閃爍
            digitalWrite(_pinLeft,  _blinkState ? HIGH : LOW);
            digitalWrite(_pinRight, _blinkState ? HIGH : LOW);
        }
    }
}

// ── justBlinkedOn() ─────────────────────────────────────────────
// 一次性旗標：LED 剛切亮時回傳 true，讀取後自動清除
// 每個 BLINK_INTERVAL 週期只會觸發一次，可直接驅動音效
bool IndicatorModule::justBlinkedOn() {
    if (_blinkJustOn) {
        _blinkJustOn = false;
        return true;
    }
    return false;
}