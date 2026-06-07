#include "Audio.h"
#include <math.h>

bool AudioManager::begin() {
    i2s_config_t cfg = {
        .mode                 = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
        .sample_rate          = SAMPLE_RATE,
        .bits_per_sample      = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format       = I2S_CHANNEL_FMT_ONLY_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags     = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count        = 8,
        .dma_buf_len          = 64,
        .use_apll             = false,
        .tx_desc_auto_clear   = true
    };

    i2s_pin_config_t pins = {
        .mck_io_num   = I2S_PIN_NO_CHANGE,
        .bck_io_num   = PIN_BCLK,
        .ws_io_num    = PIN_LRCK,
        .data_out_num = PIN_DATA,
        .data_in_num  = I2S_PIN_NO_CHANGE
    };

    if (i2s_driver_install(I2S_PORT, &cfg, 0, nullptr) != ESP_OK) {
        Serial.println("[Audio] I2S 驅動安裝失敗");
        return false;
    }

    i2s_set_pin(I2S_PORT, &pins);
    Serial.println("[Audio] MAX98357A 初始化成功");
    return true;
}

void AudioManager::beep(uint16_t freqHz, uint32_t durationMs) {
    const uint32_t totalSamples = SAMPLE_RATE * durationMs / 1000;
    const uint32_t bufSize = 256;
    int16_t buf[bufSize];
    size_t bytesWritten = 0;

    uint32_t written = 0;
    while (written < totalSamples) {
        uint32_t chunk = min((uint32_t)bufSize, totalSamples - written);
        for (uint32_t i = 0; i < chunk; i++) {
            // Generate sine wave
            float t = (float)(written + i) / SAMPLE_RATE;
            buf[i] = (int16_t)(sinf(2.0f * PI * freqHz * t) * 16000);
        }
        i2s_write(I2S_PORT, buf, chunk * sizeof(int16_t), &bytesWritten, portMAX_DELAY);
        written += chunk;
    }
}

void AudioManager::stop() {
    i2s_stop(I2S_PORT);
    i2s_zero_dma_buffer(I2S_PORT);
    i2s_start(I2S_PORT);
}

void AudioManager::playSound(SoundType sound) {
    switch (sound) {
        case SoundType::BEEP:
            beep(440, 300);
            break;
        case SoundType::TURN_SIGNAL:
            playTurnSignal();
            break;
        case SoundType::WARNING:
            playWarning();
            break;
        case SoundType::SYSTEM_READY:
            playSystemReady();
            break;
    }
}

void AudioManager::playPattern(uint16_t freq, uint32_t onMs, uint32_t offMs, int count) {
    for (int i = 0; i < count; i++) {
        beep(freq, onMs);
        if (i < count - 1) delay(offMs);
    }
}

void AudioManager::playTurnSignal() { playPattern(1000, 100, 100, 3); }
void AudioManager::playWarning()    { playPattern(880,  300, 200, 2); }

void AudioManager::playSystemReady() {
    beep(660, 200);
    delay(100);
    beep(880, 200);
    delay(100);
    beep(990, 300);
}