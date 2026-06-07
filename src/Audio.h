#pragma once
#include <Arduino.h>
#include <driver/i2s.h>

class AudioManager {
public:
    // MAX98357A I2S pins
    // 已從 GPIO 4/5/6 移至 38/39/40，讓出給 Camera SCCB SDA/SCL 與 VSYNC
    static constexpr gpio_num_t PIN_BCLK = GPIO_NUM_38;
    static constexpr gpio_num_t PIN_LRCK = GPIO_NUM_39;
    static constexpr gpio_num_t PIN_DATA = GPIO_NUM_40;

    enum class SoundType {
        BEEP,            // Single beep sound
        TURN_SIGNAL,     // Turn signal sound
        WARNING,         // Warning sound
        SYSTEM_READY     // System ready sound
    };

    bool begin();
    void beep(uint16_t freqHz, uint32_t durationMs); // Play a single tone
    void playSound(SoundType sound);                // Play a predefined sound based on SoundType
    void stop();

private:
    static constexpr i2s_port_t I2S_PORT = I2S_NUM_0;
    static constexpr uint32_t SAMPLE_RATE = 16000;

    void generateTone(uint16_t freqHz, uint32_t durationMs);
    void playPattern(uint16_t freq, uint32_t onMs, uint32_t offMs, int count);
    void playTurnSignal();
    void playWarning();
    void playSystemReady();
};