#pragma once
#include <Arduino.h>

// ═══════════════════════════════════════════════════════════════
//  FallDetector — 倒車偵測（2026-07-20 v3 新增；2026-07-22 改兩段式）
//
//  用途：車體傾倒後分兩段示警：
//    - 倒下 10 秒（NOTICE_DELAY_MS）→ 一般倒車通知（可能只是牽車/暫放）
//    - 倒下持續到 5 分鐘（EMERGENCY_DELAY_MS）仍未扶正 → 升級為緊急事故
//      （騎士很可能已離開或發生意外，須優先處理／可能需通報救援）
//
//  邏輯：
//    1. |roll| > FALL_ANGLE（預設 70°）持續 ENTER_DEBOUNCE_MS（預設 3s）
//       → 確認「倒了」，記下倒下時刻。（去彈跳，避免騎乘中大幅傾斜/牽車誤判）
//    2. 確認倒下後達 NOTICE_DELAY_MS（預設 10 秒）
//       → noticeAlertPending() 回 true，main.cpp 取用後 consumeNoticeAlert()
//    3. 確認倒下後持續達 EMERGENCY_DELAY_MS（預設 5 分鐘，仍未扶正）
//       → emergencyAlertPending() 回 true，main.cpp 取用後 consumeEmergencyAlert()
//    4. 任何時候扶正（|roll| 回到門檻內）→ 整個狀態重置，兩段旗標與計時歸零
//
//  純邏輯、無硬體依賴，吃 main 已算好的 roll。
// ═══════════════════════════════════════════════════════════════

class FallDetector {
public:
    void begin(float fallAngleDeg = 70.0f,
               unsigned long noticeDelayMs    = 10000UL,   /* 10 秒：一般通知 */
               unsigned long emergencyDelayMs = 300000UL /* 5 分鐘：緊急事故 */) {
        _fallAngle        = fallAngleDeg;
        _noticeDelayMs    = noticeDelayMs;
        _emergencyDelayMs = emergencyDelayMs;
    }

    // 每個 loop 呼叫一次，傳入目前 roll（度）
    void update(float roll) {
        unsigned long now = millis();
        bool over = fabsf(roll) > _fallAngle;

        if (over) {
            if (!_over) { _over = true; _overSinceMs = now; }
            // 超過門檻且持續夠久 → 確認倒下（只在尚未確認時設定）
            if (!_fallen && (now - _overSinceMs >= ENTER_DEBOUNCE_MS)) {
                _fallen           = true;
                _fellAtMs         = now;
                _noticeSent       = false;
                _emergencySent    = false;
            }
        } else {
            // 回到門檻內：視為扶正，整組狀態重置
            _over = false;
            if (_fallen) {
                _fallen        = false;
                _noticeSent    = false;
                _emergencySent = false;
            }
        }
    }

    bool isFallen() const { return _fallen; }

    // 已確認倒下且未扶正達 10 秒、且一般通知尚未送出 → true
    bool noticeAlertPending() const {
        return _fallen && !_noticeSent &&
               (millis() - _fellAtMs >= _noticeDelayMs);
    }

    // 已確認倒下且未扶正達 5 分鐘、且緊急示警尚未送出 → true
    bool emergencyAlertPending() const {
        return _fallen && !_emergencySent &&
               (millis() - _fellAtMs >= _emergencyDelayMs);
    }

    // main.cpp 送出對應 BLE 廣播後呼叫，避免同一次倒下重複觸發
    void consumeNoticeAlert()    { _noticeSent    = true; }
    void consumeEmergencyAlert() { _emergencySent = true; }

    // 已倒下多久（ms）；未倒下回 0
    unsigned long fallenForMs() const {
        return _fallen ? (millis() - _fellAtMs) : 0;
    }

private:
    float         _fallAngle        = 70.0f;
    unsigned long _noticeDelayMs    = 10000UL;
    unsigned long _emergencyDelayMs = 300000UL;

    bool          _over         = false;   // 目前是否超過門檻
    unsigned long _overSinceMs  = 0;        // 超過門檻起始時刻
    bool          _fallen       = false;    // 已確認倒下（過去彈跳）
    unsigned long _fellAtMs     = 0;        // 確認倒下時刻
    bool          _noticeSent    = false;   // 本次倒下的「一般通知」是否已送出
    bool          _emergencySent = false;   // 本次倒下的「緊急示警」是否已送出

    static constexpr unsigned long ENTER_DEBOUNCE_MS = 3000; // 需持續 3s 才算倒
};
