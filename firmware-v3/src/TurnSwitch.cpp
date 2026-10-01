#include "TurnSwitch.h"

void TurnSwitch::begin(int pinLeft, int pinRight) {
    _pinLeft  = pinLeft;
    _pinRight = pinRight;
    pinMode(_pinLeft,  INPUT_PULLUP);
    pinMode(_pinRight, INPUT_PULLUP);
}

void TurnSwitch::update() {
    // 2026-07-19 用 Serial 印原始電位實測出的真值表（非一般 SPDT 各側獨立判斷）：
    //   置中：L=H R=L   撥左：L=L R=L   撥右：L=L R=H
    // 也就是 L(41) 只代表「有沒有離開置中」（撥左撥右都是 LOW，只有置中是 HIGH），
    // 真正決定方向的是 R(48)：R=HIGH 才是撥右、R=LOW 是撥左。實際接線內部機構
    // 為何如此未查證，但 7 筆連續實測樣本一致，依真值表判讀。
    bool lLow  = (digitalRead(_pinLeft)  == LOW);
    bool rHigh = (digitalRead(_pinRight) == HIGH);

    TurnDirection raw = TurnDirection::NONE;
    if (lLow) {
        raw = rHigh ? TurnDirection::RIGHT : TurnDirection::LEFT;
    }
    // L=HIGH（置中）一律 NONE；(H,H) 組合實測未出現過，同樣安全預設 NONE

    unsigned long now = millis();
    if (raw != _lastRaw) {
        _lastChangeMs = now;
        _lastRaw = raw;
    }
    if (now - _lastChangeMs >= DEBOUNCE_MS) {
        _direction = raw;
    }
}
