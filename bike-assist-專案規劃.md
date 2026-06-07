# 🚲 bike-assist 專案規劃 — 2026 年暑期

> **目標**：打造一套智慧自行車輔助系統，整合 ESP32-S3 硬體、AI 路況偵測、手機 APP 即時顯示與雲端後端。  
> **期程**：2026/06/03 — 2026/08/29（12 週）  
> **硬體**：ESP32-S3-DevKitC-1（OPI PSRAM、16MB Flash）  

---

## 🏗️ 系統架構

```
[ESP32-S3]
  ├── OV2640 攝影機  ──→ MJPEG 串流
  ├── MPU6050 IMU    ──→ 加速度 / 陀螺儀
  └── GPS (UART)     ──→ 經緯度 / 速度
        │
        │ WiFi (HTTP POST / WebSocket)
        ▼
[Python FastAPI 後端]
  ├── /api/data      ← 接收感測器 JSON
  ├── /api/image     ← 接收 JPEG 圖片
  ├── /api/infer     ← AI 推論接口
  └── SQLite DB      ← 儲存所有資料
        │
        ├──→ [MobileNetV2 模型] ──→ 路況異常偵測
        └──→ [手機 APP]
               ├── WebSocket 即時資料（GPS/IMU/速度）
               ├── MJPEG 鏡頭串流
               ├── AI 警報通知（FCM）
               └── 歷史路線地圖回放
```

---

## 📅 完整 12 週時程

### Phase 1：硬體韌體（W1–W4）→ Milestone 1

| 週次 | 期間 | 主題 | 核心任務 |
|------|------|------|---------|
| **W1** | 6/03–6/09 | ⚙️ 韌體基礎：ESP32 + 感測器 | main.cpp 架構、WiFi、MPU6050、GPS 讀取 |
| **W2** | 6/10–6/16 | 📷 韌體進階：鏡頭 + 串流 | esp32-camera、WebSocket、MJPEG endpoint |
| **W3** | 6/17–6/23 | 🗄️ 後端 + 資料庫 | FastAPI、SQLite schema、/api/data |
| **W4** | 6/24–6/30 | 🔗 全系統整合 | 端對端測試、實車騎乘、開始蒐集 AI 資料 |

**🏁 6/30 Milestone 1 達標條件**
- ✅ 模型機可實際騎乘
- ✅ 資料自動存入 DB
- ✅ 開始收集 AI 訓練資料

---

### Phase 2：AI + APP（W5–W8）→ Milestone 2

| 週次 | 期間 | 主題 | 核心任務 |
|------|------|------|---------|
| **W5** | 7/01–7/07 | 🤖 AI 資料前處理 & 訓練 | 224×224 縮放 + 50% 壓縮 pipeline、資料清洗標注、MobileNetV2 訓練 |
| **W6** | 7/07–7/14 | 🧠 AI 模型導出 & 部署 | TFLite/ONNX 轉換、後端推論 API `/api/infer`、FCM 推播通知整合 |
| **W7** | 7/14–7/21 | 📱 APP 核心功能 | 框架架設、WebSocket client、即時資料顯示（GPS/IMU/速度）、AI 警報 UI |
| **W8** | 7/21–7/28 | 🎥 APP 鏡頭 & 歷史資料 | MJPEG 串流顯示、歷史路線回放地圖、全系統串接測試 |

**🏁 7/31 Milestone 2 達標條件**
- ✅ AI 可推論路況異常
- ✅ APP 可用版本完成
- ✅ 四大系統全部串接

---

### Phase 3：測試 + 發布（W9–W12）→ Milestone 3

| 週次 | 期間 | 主題 | 核心任務 |
|------|------|------|---------|
| **W9–10** | 8/03–8/17 | 🔍 測試 & Bug 修復 | 實車全程測試、Bug 修復、ESP32 記憶體/電池效能優化 |
| **W11–12** | 8/17–8/29 | 🚀 部署 & 上架準備 | 後端 Docker 雲端部署、APP 打包上架、安全性檢查、最終 Demo |

**🎉 8/29 Milestone 3：bike-assist 正式發布！**
- ✅ 硬體、軟體、APP、網頁全部可用
- ✅ 雲端部署完成
- 🚀 bike-assist 正式上線！

---

## 📋 W1 詳細任務（6/03–6/09，本週起執行）

### 目標：ESP32-S3 能正確讀取所有感測器並透過 Serial 輸出

**Day 1–2（週三四）**
- [ ] 建立 `src/main.cpp` 基礎架構（FreeRTOS Task 分離）
- [ ] WiFi 連線模組（帶斷線自動重連）
- [ ] Serial 輸出 WiFi 連線狀態

**Day 3–4（週五六）**
- [ ] MPU6050 初始化，讀取 Accel + Gyro
- [ ] JSON 格式輸出：`{"ax":0.1,"ay":0.2,"az":9.8,"gx":0,"gy":0,"gz":0}`
- [ ] GPS（TinyGPS++）UART 接線，讀取 lat/lng/speed

**Day 5（週日，6/7 有吃飯行程）**
- [ ] 輕量任務：整理 `struct BikeData` 統一資料結構
- [ ] 撰寫 README 硬體接線說明

**週末驗收**
- Serial Monitor 能看到 IMU + GPS 資料每秒更新

---

## ⚠️ 重要提醒

| 日期 | 注意事項 |
|------|---------|
| 6/07（日） | 吃飯行程，排輕量工作 |
| 6/18（四） | 北流演唱會，當天輕量或休息 |
| 6/19（五） | 端午節國定假日，休息 |
| 8/27（四） | 中元節，留意是否影響進度 |

---

## 🔧 技術選型備忘

| 層次 | 技術 | 備注 |
|------|------|------|
| 硬體韌體 | PlatformIO + Arduino framework | ESP32-S3-DevKitC-1 |
| 影像 | esp32-camera（OV2640）| PSRAM OPI 模式 |
| IMU | MPU6050 + I2C | DMP 模式讀 quaternion |
| GPS | TinyGPS++ + UART | NMEA 解析 |
| 網路 | ESPAsyncWebServer + AsyncTCP | WebSocket + MJPEG |
| 後端 | Python FastAPI | `.venv` 已建好 |
| 資料庫 | SQLite → 部署時換 PostgreSQL | |
| AI 模型 | MobileNetV2 → TFLite | 224×224 輸入 |
| APP | 待定（Flutter 建議） | WebSocket + FCM |
| 部署 | Docker + 雲端 | W11-12 處理 |

---

*最後更新：2026-06-03*
