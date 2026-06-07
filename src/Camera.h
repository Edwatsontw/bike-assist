#pragma once
#include <Arduino.h>
#include "esp_camera.h"
#include <SD.h>
#include <FS.h>

// ── GPIO 腳位分配（衝突已解決）────────────────────────────────────
//
//  Camera DVP（GPIO 4-18，固定不動）：
//    pin_sccb_sda = 4   pin_sccb_scl = 5   pin_vsync = 6
//    pin_href     = 7   pin_xclk     = 15  pin_pclk  = 13
//    pin_d0=11  d1=9  d2=8  d3=10  d4=12  d5=18  d6=17  d7=16
//
//  其他模組已移出 Camera 範圍：
//    IMU  SDA/SCL/INT → GPIO 1 / 2 / 3
//    Audio BCLK/LRCK/DATA → GPIO 38 / 39 / 40
//    GPS  RX/TX → GPIO 41 / 42
//    SD   CS/MOSI/CLK/MISO → GPIO 21 / 47 / 48 / 14
//
//  保留備用：GPIO 43、44（可供方向燈或其他模組使用）
// ──────────────────────────────────────────────────────────────────

// ── 行車紀錄器事件類型 ────────────────────────
enum class DashcamEvent {
    NORMAL,           // 普通錄製
    EMERGENCY_BRAKE,  // 緊急制動（大加速度）
    SHARP_TURN,       // 急轉彎（Roll > 25°）
    COLLISION,        // 碰撞檢測（加速度突增）
};

class CameraManager {
public:
    // ── SD 卡 SPI 腳位 ────────────────────────
    static constexpr int PIN_SD_CS   = 21;
    static constexpr int PIN_SD_MOSI = 47;
    static constexpr int PIN_SD_CLK  = 48;
    static constexpr int PIN_SD_MISO = 14;

    bool begin();

    // ── 連續錄影模式 ──────────────────────────
    camera_fb_t* captureFrame();
    void release(camera_fb_t* fb);

    // ── 行車紀錄器功能 ────────────────────────
    void recordFrame(camera_fb_t* fb, DashcamEvent event,
                     float roll, float pitch, float accel);
    void onEvent(DashcamEvent event, float value);

    // ── 狀態查詢 ──────────────────────────────
    bool     isReady()      const { return _ready; }
    bool     isRecording()  const { return _recording; }
    bool     isSDReady()    const { return _sdReady; }
    uint32_t getFrameCount()const { return _frameCount; }

private:
    bool     _ready          = false;
    bool     _recording      = false;
    bool     _sdReady        = false;
    uint32_t _frameCount     = 0;
    uint32_t _fileIndex      = 0;   // 流水號，避免重複檔名
    uint32_t _recordStartTime= 0;

    DashcamEvent _lastEvent  = DashcamEvent::NORMAL;
    unsigned long _eventEndTime = 0;

    // ── 私有方法 ──────────────────────────────
    bool   _initSD();
    bool   _saveFrame(camera_fb_t* fb, const char* filename);
    void   _buildFilename(char* buf, size_t bufSize,
                          DashcamEvent event);
    const char* _eventToStr(DashcamEvent event);
};
