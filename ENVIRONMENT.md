# SafeWay 開發與執行環境

整個專案在這個 repo（`safeway-final` 分支）裡。下面列出每個部分需要的工具與版本，以及**不在 repo 裡、要自己準備的東西**。

## 一覽

| 部分 | 資料夾 | 需要的工具 | 版本（目前使用） |
|------|--------|------------|------------------|
| 手機 APP | `flutter_application_1/` | Flutter SDK、Java（JDK）、Android SDK | Flutter **3.44.6**（stable）／Dart 3.12、JDK **17** |
| 車機韌體 | `firmware-v3/` | VS Code ＋ PlatformIO | platform `espressif32`、framework `arduino`（函式庫見 `platformio.ini`） |
| 系統端伺服器 | `server/` | Python | **3.12**（`start_server.bat` 會用 winget 自動安裝）；套件見 `server/requirements.txt` |
| YOLO 偵測 | `inference/` | Docker Desktop（WSL2）＋ NVIDIA GPU 驅動 | 模型 `inference/model/best.pt` |
| 模型訓練 | `ml/` | Python ＋ ultralytics ＋ CUDA 版 PyTorch | 資料集不在 repo，需用下載腳本重新取得 |
| 電路板 | `PCB/` | KiCad | `.kicad_pro` / `.kicad_sch` / `.kicad_pcb` |

## 一、手機 APP（不用自己裝 Flutter）

推到 `safeway-final` 分支、且 `flutter_application_1/` 有改動時，GitHub Actions 會自動：
`flutter analyze` → `flutter test` → `flutter build apk --release`，產物放在
[Releases → SafeWay 最新測試版](https://github.com/Edwatsontw/bike-assist/releases/tag/safeway-latest)。

本機要自己建置的話：
```bash
cd flutter_application_1
flutter pub get
flutter test
flutter build apk --release
```

> APK 目前用 GitHub 每次重新產生的 debug 簽章，**每一版簽章都不同**：安裝新版前要先移除舊版。

## 二、車機韌體

1. VS Code 安裝 PlatformIO 擴充套件，用它**開啟 `firmware-v3` 資料夾**。
2. **複製 `src/secrets.example.h` → `src/secrets.h`，填入熱點名稱與密碼**（`secrets.h` 不會上傳）。
3. 環境選 `esp32-s3-devkitc-1` → Upload。燒錄速度已設 115200。
   - 連不上時：按住 BOOT → 按一下 RST → 放開 BOOT，再 Upload；或換板子上另一個 USB-C 孔。
4. 腳位見 `docs/腳位分配-GOOUUU-S3-CAM.md`。

## 三、系統端伺服器（Windows）

| 檔案 | 用途 |
|------|------|
| `server/start_server.bat` | 一鍵啟動（第一次請「以系統管理員身分執行」：裝 Python、套件、開防火牆 8000） |
| `server/demo_mode.bat` | **備案**：伺服器＋模擬車機（`fake_bike.py`），系統端會顯示「模擬資料」 |
| `server/data/bike_data.db` | 展示用資料庫（偵測結果） |
| `server/uploads/` | 偵測地圖的照片 |

Docker 方式（有 Docker Desktop 時）：`cd server && docker compose up -d --build`（含 GPU 偵測 worker）。

## 四、模型訓練（`ml/`）

- 只放了腳本與 `data.yaml`；**資料集（約 5 萬個檔案）、訓練輸出 `runs/`、預訓練權重沒有放進 repo**。
- Roboflow 下載腳本改成從環境變數讀金鑰：
  ```powershell
  $env:ROBOFLOW_API_KEY = "你的金鑰"
  python download_datasets.py
  ```

## 五、不在 repo 裡、要另外保管的東西

| 東西 | 放在哪 |
|------|--------|
| 熱點帳密 | `firmware-v3/src/secrets.h`（只在自己電腦） |
| Roboflow API 金鑰 | 環境變數 `ROBOFLOW_API_KEY` |
| 訓練資料集、`runs/` | 原開發電腦 `bike-assist-v3/ml/` |
| Tailscale 帳號 | 見 `docs/展場/Tailscale設定.md` |

## 六、展場

見 [`docs/展場/展場操作SOP.md`](docs/展場/展場操作SOP.md)（含備案：模擬車機）。
