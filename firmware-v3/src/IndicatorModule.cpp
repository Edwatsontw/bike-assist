#include "IndicatorModule.h"

// ================================================================
//  IndicatorModule implementation (ASCII-only comments).
//  LEDC helpers below work on BOTH arduino-esp32 2.x and 3.x:
//    2.x -> ledcSetup(channel) + ledcAttachPin(pin, channel), write by channel
//    3.x -> ledcAttach(pin, freq, res) [auto channel], write by pin
//  We always pass both (pin, channel) so either API compiles.
// ================================================================
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
  #define LED_SETUP(pin, ch)        ledcAttach((pin), PWM_FREQ, PWM_RES)
  #define LED_WRITE(pin, ch, duty)  ledcWrite((pin), (duty))
#else
  #define LED_SETUP(pin, ch)        do { ledcSetup((ch), PWM_FREQ, PWM_RES); \
                                          ledcAttachPin((pin), (ch)); } while (0)
  #define LED_WRITE(pin, ch, duty)  ledcWrite((ch), (duty))
#endif

// ---- constructor ----
IndicatorModule::IndicatorModule()
    : _pinLeft(-1)
    , _pinRight(-1)
    , _blinkState(false)
    , _lastBlink(0)
    , _blinkJustOn(false)
    , _currentDirection(TurnDirection::NONE)
{}

// ---- begin(): set pins, start LEDC PWM, idle at 50% ----
void IndicatorModule::begin(int pinLeft, int pinRight) {
    _pinLeft  = pinLeft;
    _pinRight = pinRight;

    LED_SETUP(_pinLeft,  CH_LEFT);
    LED_SETUP(_pinRight, CH_RIGHT);

    LED_WRITE(_pinLeft,  CH_LEFT,  DUTY_IDLE);
    LED_WRITE(_pinRight, CH_RIGHT, DUTY_IDLE);
}

// ---- update(): decide direction from roll, then blink ----
// HAZARD stays latched until off() is called; roll does not override it.
void IndicatorModule::update(float roll) {
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

// ---- manual control ----
void IndicatorModule::activateLeft() {
    _currentDirection = TurnDirection::LEFT;
    _handleBlink(_currentDirection);
}

void IndicatorModule::activateRight() {
    _currentDirection = TurnDirection::RIGHT;
    _handleBlink(_currentDirection);
}

void IndicatorModule::activateHazard() {
    _currentDirection = TurnDirection::HAZARD;
    _handleBlink(TurnDirection::HAZARD);
}

// ---- off(): back to idle (both sides 50% daytime light) ----
void IndicatorModule::off() {
    _currentDirection = TurnDirection::NONE;
    _blinkState       = false;
    LED_WRITE(_pinLeft,  CH_LEFT,  DUTY_IDLE);
    LED_WRITE(_pinRight, CH_RIGHT, DUTY_IDLE);
}

// ---- isTiltWarning(): roll beyond warning threshold ----
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

// ---- _handleBlink(): NONE = both idle 50%; else blink active side ----
void IndicatorModule::_handleBlink(TurnDirection dir) {
    if (dir == TurnDirection::NONE) {
        LED_WRITE(_pinLeft,  CH_LEFT,  DUTY_IDLE);
        LED_WRITE(_pinRight, CH_RIGHT, DUTY_IDLE);
        _blinkState = false;
        return;
    }

    unsigned long now = millis();
    if (now - _lastBlink >= BLINK_INTERVAL) {
        _lastBlink   = now;
        _blinkState  = !_blinkState;
        _blinkJustOn = _blinkState;   // one-shot flag when LED turns on

        uint8_t active = _blinkState ? DUTY_FULL : DUTY_OFF;

        if (dir == TurnDirection::LEFT) {
            LED_WRITE(_pinLeft,  CH_LEFT,  active);
            LED_WRITE(_pinRight, CH_RIGHT, DUTY_IDLE);
        } else if (dir == TurnDirection::RIGHT) {
            LED_WRITE(_pinRight, CH_RIGHT, active);
            LED_WRITE(_pinLeft,  CH_LEFT,  DUTY_IDLE);
        } else {  // HAZARD: both sides in sync
            LED_WRITE(_pinLeft,  CH_LEFT,  active);
            LED_WRITE(_pinRight, CH_RIGHT, active);
        }
    }
}

// ---- justBlinkedOn(): true once right after LED turns on ----
bool IndicatorModule::justBlinkedOn() {
    if (_blinkJustOn) {
        _blinkJustOn = false;
        return true;
    }
    return false;
}
