#include "Camera.h"

// ═══════════════════════════════════════════════════════════════
//  CameraManager 實作
// ═══════════════════════════════════════════════════════════════

bool CameraManager::begin() {
    // ── 1. 初始化攝影機 ───────────────────────────────────────
    camera_config_t config = {
        .pin_pwdn     = -1,
        .pin_reset    = -1,
        .pin_xclk     = 15,
        .pin_sccb_sda = 4,
        .pin_sccb_scl = 5,
        .pin_d7 = 16, .pin_d6 = 17,
        .pin_d5 = 18, .pin_d4 = 12,
        .pin_d3 = 10, .pin_d2 = 8,
        .pin_d1 = 9,  .pin_d0 = 11,
        .pin_vsync = 6,
        .pin_href  = 7,
        .pin_pclk  = 13,

        .xclk_freq_hz = 20000000,
        .ledc_timer   = LEDC_TIMER_0,
        .ledc_channel = LEDC_CHANNEL_0,

        .pixel_format = PIXFORMAT_JPEG,
        .frame_size   = FRAMESIZE_QVGA,   // 320x240
        .jpeg_quality = 10,
        .fb_count     = 2,
        .fb_location  = CAMERA_FB_IN_PSRAM,
        .grab_mode    = CAMERA_GRAB_WHEN_EMPTY
    };

    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK) {
        Serial.printf("[Camera] 初始化失敗，錯誤碼: 0x%x\n", err);
        return false;
    }
    Serial.println("[Camera] 攝影機初始化成功");

    // ── 2. 初始化 SD 卡 ───────────────────────────────────────
    _sdReady = _initSD();

    _ready           = true;
    _recording       = true;
    _recordStartTime = millis();

    Serial.println("[Camera] 系統就緒，開始連續錄製");
    return true;
}

// ── _initSD() ────────────────────────────────────────────────────
// 掛載 SD 卡，建立 /dashcam 根目錄
bool CameraManager::_initSD() {
    // 自訂 SPI 腳位
    SPI.begin(PIN_SD_CLK, PIN_SD_MISO, PIN_SD_MOSI, PIN_SD_CS);

    if (!SD.begin(PIN_SD_CS)) {
        Serial.println("[Camera] SD 卡掛載失敗，影像將不會儲存");
        return false;
    }

    // 確認 SD 卡類型
    uint8_t cardType = SD.cardType();
    if (cardType == CARD_NONE) {
        Serial.println("[Camera] 未偵測到 SD 卡");
        return false;
    }

    Serial.printf("[Camera] SD 卡掛載成功，容量: %llu MB\n",
                  SD.cardSize() / (1024 * 1024));

    // 建立根目錄
    if (!SD.exists("/dashcam")) {
        SD.mkdir("/dashcam");
        Serial.println("[Camera] 建立目錄 /dashcam");
    }

    // 建立事件目錄
    if (!SD.exists("/dashcam/event")) {
        SD.mkdir("/dashcam/event");
    }
    if (!SD.exists("/dashcam/normal")) {
        SD.mkdir("/dashcam/normal");
    }

    return true;
}

// ── captureFrame() ───────────────────────────────────────────────
camera_fb_t* CameraManager::captureFrame() {
    if (!_ready) return nullptr;

    camera_fb_t* fb = esp_camera_fb_get();
    if (!fb) {
        Serial.println("[Camera] 影像捕捉失敗");
    }
    return fb;
}

// ── release() ────────────────────────────────────────────────────
void CameraManager::release(camera_fb_t* fb) {
    if (fb) esp_camera_fb_return(fb);
}

// ── recordFrame() ────────────────────────────────────────────────
// 每個 loop 呼叫一次
// 根據事件類型決定是否寫入 SD 卡，並附加 metadata log
void CameraManager::recordFrame(camera_fb_t* fb,
                                 DashcamEvent event,
                                 float roll,
                                 float pitch,
                                 float accel) {
    if (!_recording || !fb) return;

    _frameCount++;

    // ── 只在特定條件下寫入，避免 SD 卡寫入過於頻繁 ──────────
    // 規則：事件幀 → 每幀都存；正常幀 → 每 30 幀存一次
    bool shouldSave = false;

    if (event != DashcamEvent::NORMAL) {
        shouldSave = true;                      // 事件幀：全部儲存
    } else if (_frameCount % 30 == 0) {
        shouldSave = true;                      // 正常幀：每 30 幀取樣一次
    }

    if (!shouldSave || !_sdReady) return;

    // ── 建立檔名 ─────────────────────────────────────────────
    char filename[64];
    _buildFilename(filename, sizeof(filename), event);

    // ── 寫入影像 ─────────────────────────────────────────────
    if (_saveFrame(fb, filename)) {
        // ── 寫入對應的 metadata log ──────────────────────────
        // 格式：/dashcam/log.csv
        File logFile = SD.open("/dashcam/log.csv", FILE_APPEND);
        if (logFile) {
            // 首次寫入時加入 CSV 標頭
            if (logFile.size() == 0) {
                logFile.println("index,event,roll,pitch,accel,filename");
            }
            logFile.printf("%u,%s,%.2f,%.2f,%.2f,%s\n",
                           _fileIndex,
                           _eventToStr(event),
                           roll, pitch, accel,
                           filename);
            logFile.close();
        }
    }
}

// ── onEvent() ────────────────────────────────────────────────────
// 外部觸發事件（例如碰撞、急煞），記錄到 log
void CameraManager::onEvent(DashcamEvent event, float value) {
    _lastEvent    = event;
    _eventEndTime = millis() + 3000;  // 事件持續 3 秒

    Serial.printf("[Camera] 事件觸發: %s (value=%.2f)\n",
                  _eventToStr(event), value);
}

// ── _saveFrame() ─────────────────────────────────────────────────
// 將 JPEG buffer 寫入 SD 卡指定路徑
bool CameraManager::_saveFrame(camera_fb_t* fb, const char* filename) {
    File file = SD.open(filename, FILE_WRITE);
    if (!file) {
        Serial.printf("[Camera] 無法開啟檔案: %s\n", filename);
        return false;
    }

    size_t written = file.write(fb->buf, fb->len);
    file.close();

    if (written != fb->len) {
        Serial.printf("[Camera] 寫入不完整: %u / %u bytes\n",
                      written, fb->len);
        return false;
    }

    return true;
}

// ── _buildFilename() ─────────────────────────────────────────────
// 依事件類型產生檔名
// 格式：/dashcam/event/EVT_000123.jpg
//       /dashcam/normal/NRM_000123.jpg
void CameraManager::_buildFilename(char* buf, size_t bufSize,
                                    DashcamEvent event) {
    _fileIndex++;

    if (event != DashcamEvent::NORMAL) {
        snprintf(buf, bufSize,
                 "/dashcam/event/%s_%06u.jpg",
                 _eventToStr(event), _fileIndex);
    } else {
        snprintf(buf, bufSize,
                 "/dashcam/normal/NRM_%06u.jpg",
                 _fileIndex);
    }
}

// ── _eventToStr() ────────────────────────────────────────────────
const char* CameraManager::_eventToStr(DashcamEvent event) {
    switch (event) {
        case DashcamEvent::EMERGENCY_BRAKE: return "BRAKE";
        case DashcamEvent::SHARP_TURN:      return "TURN";
        case DashcamEvent::COLLISION:       return "COLLISION";
        default:                            return "NORMAL";
    }
}
