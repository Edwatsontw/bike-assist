#pragma once
#include <Arduino.h>

// ================================================================
//  IndicatorModule - turn-signal / hazard module
//  Controls left/right blinkers by roll angle; tilt-warning detect.
//  (Comments kept ASCII-only to avoid source-encoding issues.)
// ================================================================

enum class TurnDirection {
    NONE,   // straight
    LEFT,   // left turn
    RIGHT,  // right turn
    HAZARD  // hazard: both sides blink in sync
};

class IndicatorModule {
public:
    IndicatorModule();

    // Set GPIO pins and output idle.
    void begin(int pinLeft, int pinRight);

    // Call once per loop: auto-decide direction from roll, drive blink.
    void update(float roll);

    // Manual control
    void activateLeft();   // force left blinker
    void activateRight();  // force right blinker
    void activateHazard(); // force hazard (both in sync)
    void off();            // stop all, reset to idle

    // State readers
    TurnDirection getDirection() const { return _currentDirection; }
    const char* getDirectionStr() const;
    bool isHazard() const { return _currentDirection == TurnDirection::HAZARD; }
    bool isTiltWarning(float roll) const;

    // One-shot flag: true right when LED switches ON (auto-cleared on read).
    bool justBlinkedOn();

private:
    void _handleBlink(TurnDirection dir);

    int _pinLeft;
    int _pinRight;

    bool          _blinkState;
    unsigned long _lastBlink;
    bool          _blinkJustOn;   // set once when LED just turned on

    TurnDirection _currentDirection;

    // Constants
    static constexpr float         TURN_DEG       = 15.0f;  // blinker trigger angle
    static constexpr float         TILT_WARN_DEG  = 30.0f;  // tilt-warning threshold
    static constexpr unsigned long BLINK_INTERVAL = 250;    // 亮/滅切換間隔 ms（一個完整循環 = 2×250 = 0.5 秒）

    // LEDC PWM: idle fully off + full-bright blink (traditional blinker).
    // Camera XCLK uses LEDC channel 0 / timer 0, so blinkers use
    // channel 2/3 (channel/2 -> timer 1, no clash with camera timer 0).
    static constexpr int      CH_LEFT   = 2;      // left  LEDC channel
    static constexpr int      CH_RIGHT  = 3;      // right LEDC channel
    static constexpr int      PWM_FREQ  = 5000;   // PWM frequency Hz
    static constexpr int      PWM_RES   = 8;      // resolution 8-bit (0-255)
    static constexpr uint8_t  DUTY_IDLE = 0;      // idle fully off (traditional blinker)
    static constexpr uint8_t  DUTY_FULL = 255;    // blink full-on
    static constexpr uint8_t  DUTY_OFF  = 0;      // blink full-off
};
