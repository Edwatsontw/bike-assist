#pragma once
#include <Arduino.h>

// ═══════════════════════════════════════════════════════════════
//  IndicatorModule — 方向燈模組
//  職責：根據 roll 角度控制左右方向燈閃爍、傾斜警告偵測
//  取代 main.cpp 中的 handleBlinker() 與相關全域變數
// ═══════════════════════════════════════════════════════════════

enum class TurnDirection {
    NONE,   // 直行
    LEFT,   // 左轉
    RIGHT,  // 右轉
    HAZARD  // 雙閃警示燈（左右同步閃爍）
};

class IndicatorModule {
public:
    // ── 建構子 ──────────────────────────────────────────────────
    IndicatorModule();

    // ── 初始化：設定 GPIO 接腳並輸出 LOW ────────────────────────
    void begin(int pinLeft, int pinRight);

    // ── 主要更新介面：每個 loop 呼叫一次 ───────────────────────
    // 根據 roll 角度自動判斷方向並控制閃爍
    void update(float roll);

    // ── 手動控制介面 ────────────────────────────────────────────
    void activateLeft();   // 強制啟動左方向燈閃爍
    void activateRight();  // 強制啟動右方向燈閃爍
    void activateHazard(); // 啟動雙閃警示燈（兩側同步）
    void off();            // 關閉所有燈並重設模式

    // ── 狀態讀取介面 ────────────────────────────────────────────
    TurnDirection getDirection() const { return _currentDirection; }
    const char* getDirectionStr() const;
    bool isHazard() const { return _currentDirection == TurnDirection::HAZARD; }
    bool isTiltWarning(float roll) const;  // 傾斜角是否超過警告閾值

    // ── 音效同步介面 ────────────────────────────────────────────
    // 每次 LED 剛切換為「亮」時回傳 true，並自動清除旗標（一次性）
    // 在 loop 中讀取後立即呼叫音效，確保聲光同步
    bool justBlinkedOn();

private:
    // ── 內部閃爍處理 ────────────────────────────────────────────
    void _handleBlink(TurnDirection dir);

    // ── 接腳 ────────────────────────────────────────────────────
    int _pinLeft;
    int _pinRight;

    // ── 閃爍狀態 ────────────────────────────────────────────────
    bool          _blinkState;
    unsigned long _lastBlink;
    bool          _blinkJustOn;  // LED 剛切換為亮的單次旗標

    // ── 當前方向 ────────────────────────────────────────────────
    TurnDirection _currentDirection;

    // ── 常數 ────────────────────────────────────────────────────
    static constexpr float         TURN_DEG       = 15.0f;  // 方向燈觸發角度
    static constexpr float         TILT_WARN_DEG  = 30.0f;  // 傾斜警告閾值
    static constexpr unsigned long BLINK_INTERVAL = 500;    // 閃爍間隔 ms
};