#pragma once
#include <Arduino.h>
#include "esp_camera.h"
#include <SD_MMC.h>
#include <FS.h>

// ── GPIO 腳位分配（二代機 GOOUUU ESP32-S3-CAM）─────────────────────
//
//  Camera DVP（GPIO 4-18，排線焊死、固定不動）：
//    pin_sccb_sda = 4   pin_sccb_scl = 5   pin_vsync = 6
//    pin_href     = 7   pin_xclk     = 15  pin_pclk  = 13
//    pin_d0=11  d1=9  d2=8  d3=10  d4=12  d5=18  d6=17  d7=16
//
//  其他模組分配：
//    IMU  SDA/SCL/INT      → GPIO 1 / 2 / 14
//    GPS  RX               → GPIO 21（TX 不接）
//    方向燈 左/右（MOSFET） → GPIO 46 / 47
//    方向燈搖桿開關 左/右   → GPIO 41 / 48（2026-07-19 新增；右觸點原規劃42，
//                             板子上42不好接改48。注意45雖空出但是VDD_SPI
//                             strapping pin，接搖桿當輸入可能導致開機失敗，不可用）
//    SD   板載卡槽 SDMMC 1-bit → CLK=39  CMD=38  D0=40（硬體固定）
//
//  保留備用：GPIO 0、43、44、19、20、42、45（原音效／VDD_SPI strapping，避免用作輸入）
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
    // ── 板載 microSD 卡槽（SDMMC 1-bit 模式，硬體固定）──────────
    static constexpr int PIN_SD_CLK = 39;
    static constexpr int PIN_SD_CMD = 38;
    static constexpr int PIN_SD_D0  = 40;

    bool begin();

    // ── 連續錄影模式 ──────────────────────────
    camera_fb_t* captureFrame();
    void release(camera_fb_t* fb);

    // ── 行車紀錄器功能 ────────────────────────
    // v3（2026-07-20）：log.csv 新增 GPS 座標＋時間戳，供伺服器端
    // 「照片對應地圖座標」標記使用（v3 站點批次上傳架構的前提）。
    //   timeStr：currentTimeStr() 之輸出（未校時為 "----"）
    //   lat/lon：GPS 座標；locValid=false 時記錄空欄（未定位）
    void recordFrame(camera_fb_t* fb, DashcamEvent event,
                     float roll, float pitch, float accel,
                     double lat, double lon, bool locValid,
                     const String& timeStr);
    void onEvent(DashcamEvent event, float value);

    // ── 夜間模式（軟體低光增強：拉高增益天花板＋亮度＋夜間 AEC）──
    // on=true 進夜視、false 回日間預設。純調 sensor 參數，不需硬體。
    void setNightMode(bool on);

    // ── 狀態查詢 ──────────────────────────────
    bool     isReady()      const { return _ready; }
    bool     isRecording()  const { return _recording; }
    bool     isSDReady()    const { return _sdReady; }
    bool     isNightMode()  const { return _nightMode; }
    uint32_t getFrameCount()const { return _frameCount; }

private:
    bool     _ready          = false;
    bool     _recording      = false;
    bool     _sdReady        = false;
    bool     _nightMode      = false;
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
