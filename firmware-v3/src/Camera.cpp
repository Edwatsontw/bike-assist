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
        .frame_size   = FRAMESIZE_XGA,     // 1024x768（v3, 2026-07-20 再調高）
        // 畫質演進：QVGA → SVGA(800x600,偏燙) → 240x240(降溫但太糊) → VGA(640x480)
        // → XGA(1024x768)。使用者要辨識更多細節（路邊機車/轎車/腳踏車，其中機車/
        // 腳踏車偏小目標吃解析度）。串流已不重要（改離線批次上傳），故不再顧慮串流
        // FPS，只剩：SD 空間、站點上傳時間、拍攝/寫卡發熱。XGA 像素量約 VGA 的 2.56 倍。
        // 若 SD 太快滿或發熱明顯，退回 FRAMESIZE_SVGA(800x600)。
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
    // 板載 microSD 卡槽，使用 SDMMC 1-bit 模式（CLK=39 CMD=38 D0=40）
    SD_MMC.setPins(PIN_SD_CLK, PIN_SD_CMD, PIN_SD_D0);

    // 第二參數 true = 1-bit 模式（板上只接 D0），第三參數 false = 不格式化
    if (!SD_MMC.begin("/sdcard", true, false)) {
        Serial.println("[Camera] SD 卡掛載失敗，影像將不會儲存");
        return false;
    }

    // 確認 SD 卡類型
    uint8_t cardType = SD_MMC.cardType();
    if (cardType == CARD_NONE) {
        Serial.println("[Camera] 未偵測到 SD 卡");
        return false;
    }

    Serial.printf("[Camera] SD 卡掛載成功，容量: %llu MB\n",
                  SD_MMC.cardSize() / (1024 * 1024));

    // 建立根目錄
    if (!SD_MMC.exists("/dashcam")) {
        SD_MMC.mkdir("/dashcam");
        Serial.println("[Camera] 建立目錄 /dashcam");
    }

    // 建立事件目錄
    if (!SD_MMC.exists("/dashcam/event")) {
        SD_MMC.mkdir("/dashcam/event");
    }
    if (!SD_MMC.exists("/dashcam/normal")) {
        SD_MMC.mkdir("/dashcam/normal");
    }

    return true;
}

// ── setNightMode() ───────────────────────────────────────────────
// 軟體低光增強。無光環境無法無中生有，僅在有微光（路燈/車燈/黃昏）
// 時把畫面提亮；代價是雜訊增加、FPS 下降。
//   夜間：增益天花板拉到 128X、開夜間 AEC、亮度 +2、對比 +1
//   日間：還原預設（2X、關夜間 AEC、亮度/對比 0）
void CameraManager::setNightMode(bool on) {
    sensor_t* s = esp_camera_sensor_get();
    if (!s) {
        Serial.println("[Camera] 取得 sensor 失敗，夜間模式未套用");
        return;
    }

    if (on) {
        s->set_gain_ctrl(s, 1);                     // 自動增益 ON
        s->set_exposure_ctrl(s, 1);                 // 自動曝光 ON
        s->set_gainceiling(s, GAINCEILING_128X);    // 增益天花板拉滿
        s->set_aec2(s, 1);                          // 夜間 AEC（DSP）
        s->set_brightness(s, 2);                    // 亮度 +2（-2..2）
        s->set_contrast(s, 1);                      // 對比 +1
    } else {
        s->set_gainceiling(s, GAINCEILING_2X);      // 還原預設
        s->set_aec2(s, 0);
        s->set_brightness(s, 0);
        s->set_contrast(s, 0);
    }

    _nightMode = on;
    Serial.printf("[Camera] 夜間模式 → %s\n", on ? "ON" : "OFF");
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
                                 float accel,
                                 double lat,
                                 double lon,
                                 bool locValid,
                                 const String& timeStr) {
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
        File logFile = SD_MMC.open("/dashcam/log.csv", FILE_APPEND);
        if (logFile) {
            // 首次寫入時加入 CSV 標頭
            // v3（2026-07-20）新增 time,lat,lon 三欄——伺服器端地圖標記的前提。
            // 注意：與 v2 舊卡的 log.csv 欄位不相容，v3 首次使用建議清空 SD 或換卡。
            if (logFile.size() == 0) {
                logFile.println("index,time,event,roll,pitch,accel,lat,lon,filename");
            }
            // GPS 未定位時 lat/lon 記空欄（伺服器端以空欄判斷「無座標照片」，
            // 不用 0,0 佔位——0,0 是幾內亞灣有效座標，會污染地圖）
            char latStr[16] = "", lonStr[16] = "";
            if (locValid) {
                snprintf(latStr, sizeof(latStr), "%.6f", lat);
                snprintf(lonStr, sizeof(lonStr), "%.6f", lon);
            }
            logFile.printf("%u,%s,%s,%.2f,%.2f,%.2f,%s,%s,%s\n",
                           _fileIndex,
                           timeStr.c_str(),
                           _eventToStr(event),
                           roll, pitch, accel,
                           latStr, lonStr,
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
    File file = SD_MMC.open(filename, FILE_WRITE);
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
