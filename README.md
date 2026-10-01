# bike-assist

智慧型自行車輔助系統:ESP32-S3 硬體 + AI 路況偵測 + 手機 App 即時顯示與後端。

## 專案結構

| 資料夾 | 內容 |
|--------|------|
| [`flutter_application_1/`](flutter_application_1/) | 手機 APP（SafeWay）：即時儀表板、鏡頭串流、騎乘記錄與回放、匯出、緊急回報、手機定位傳給車機 |
| [`firmware-v3/`](firmware-v3/) | **目前使用的**車機韌體（ESP32-S3-CAM，PlatformIO）。熱點帳密放 `src/secrets.h`（不進 Git，請由 `secrets.example.h` 複製） |
| [`server/`](server/) | 系統端（FastAPI）＋展示用資料庫與影像。Windows 雙擊 `start_server.bat` 即可啟動 |
| [`inference/`](inference/) | YOLO 偵測 worker（需 NVIDIA GPU＋Docker；模型權重 `best.pt` 不在 repo 內） |
| [`docs/展場/`](docs/展場/) | **展場操作 SOP**、Tailscale 遠端連線設定 |
| [`firmware/`](firmware/) | 早期韌體（camtest，保留參考） |

APK 下載：[Releases → SafeWay 最新測試版](https://github.com/Edwatsontw/bike-assist/releases/tag/safeway-latest)

## App 快速開始

```bash
cd flutter_application_1
flutter pub get
flutter run            # 或 flutter run -d chrome / -d <裝置>
```

需先安裝 [Flutter SDK](https://docs.flutter.dev/get-started/install);要跑 Android 需 Android SDK 與裝置/模擬器。

> 注意:騎乘記錄用的 sqflite 不支援純網頁,鏡頭/儀表板可在 web 上以模擬資料執行,完整功能請用 Android 裝置/模擬器。

## 韌體快速開始

`firmware/camtest.cpp` 需放進你的 PlatformIO 專案 `src/`,並在 `platformio.ini` 的 `lib_deps` 加入 `bblanchon/ArduinoJson` 與 `espressif/esp32-camera`。詳見 [firmware/README.md](firmware/README.md)。

## 系統架構

```
[ESP32-S3] ──WiFi/HTTP──> [手機 App]
  ├─ OV2640  → MJPEG /stream          即時鏡頭 / 匯出影片
  └─ GPS     → 經緯度 / 速度           儀表板 / 路線記錄 / 匯出座標 CSV
```
