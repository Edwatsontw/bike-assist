#pragma once
#include <Arduino.h>
#include "IndicatorModule.h"   // 重用 TurnDirection 列舉（NONE/LEFT/RIGHT/HAZARD）

// ═══════════════════════════════════════════════════════════════
//  TurnSwitch — 手動方向燈搖桿開關（3 接腳，維持式／自鎖，非彈簧回中）
//  職責：讀取搖桿目前位置，回傳 TurnDirection 給 main.cpp 驅動 IndicatorModule
//
//  接線（二代機，2026-07-19 新增，2026-07-19 右觸點腳位調整）：
//    共接（Common）  → GND
//    左觸點          → GPIO 41（INPUT_PULLUP，原音效腳位空出，非 strapping）
//    右觸點          → GPIO 48（INPUT_PULLUP；原規劃 GPIO42，因板子上該孔位不好接改用 48；
//                       48 與板載 WS2812 共用但本專案未驅動該燈，且非 strapping，比 42 更安全；
//                       注意：GPIO45 雖也空出，但是 VDD_SPI 電壓選擇的 strapping pin，
//                       INPUT_PULLUP 預設 HIGH 會誤選 1.8V 供電、可能導致開機失敗，故不可用）
//
//  原理（2026-07-19 實測真值表，非一般 SPDT 各側獨立判斷）：
//    置中：左(41)=H 右(48)=L　　撥左：左(41)=L 右(48)=L　　撥右：左(41)=L 右(48)=H
//    左觸點只代表「有沒有離開置中」（撥左撥右皆為 LOW，僅置中為 HIGH），
//    實際方向由右觸點決定（HIGH=右、LOW=左）。內部機構原因未查證，依實測判讀。
//        本開關為維持式（撥到側會停在那，需手動推回中間才停），
//        故不需要「單擊觸發、再次觸發取消」的狀態機，直接讀目前位置即可。
// ═══════════════════════════════════════════════════════════════

class TurnSwitch {
public:
    // 初始化兩個輸入腳（INPUT_PULLUP，不需外部電阻）
    void begin(int pinLeft, int pinRight);

    // 每個 loop 呼叫一次：讀取腳位＋簡易去彈跳
    void update();

    // 目前判定方向（僅會是 NONE / LEFT / RIGHT，不會是 HAZARD）
    TurnDirection getDirection() const { return _direction; }

private:
    int _pinLeft  = -1;
    int _pinRight = -1;

    TurnDirection _direction = TurnDirection::NONE; // 去彈跳後的穩定值
    TurnDirection _lastRaw   = TurnDirection::NONE; // 上次讀到的原始值
    unsigned long _lastChangeMs = 0;

    // 維持式開關機構穩定，只需短去彈跳（不像按鈕需要長去彈跳）
    static constexpr unsigned long DEBOUNCE_MS = 30;
};
