# bike-assist v3

> 建立日期：2026-07-20，自 [bike-assist-v2](../bike-assist-v2/) 分出。
> **v3 是「架構版本」**：硬體完全沿用 v2（GOOUUU ESP32-S3-CAM），
> 進化的是系統架構——從單機獨立運作 → **車＋站點＋雲三層系統**。
> 完整規劃見 `docs/v3-架構規劃.md`。

## 三層架構

```
車上（v2 硬體）             歸還站點               家用伺服器（24hr, RTX 5070）
拍照存SD（附GPS＋時間）  →  服務機WiFi批次收資料  →  YOLO11n 推論標記（pothole/dog）
煞車/碰撞警示照常（IMU）    轉送雲端                地圖：路線統計＋標記點位
```

## 目錄結構

| 目錄 | 內容 | 來源 |
|---|---|---|
| `src/` | ESP32-S3 韌體（PlatformIO+Arduino） | 複製自 v2，待改：log.csv 補 GPS/時間、NetworkManager 站點上傳模式 |
| `server/` | FastAPI 伺服器 | 複製自 v2，待擴充：批次上傳 API、YOLO11n 推論、地圖 |
| `ml/` | 訓練設定 | 新 2 類 `data.yaml`（pothole/dog）；實體 datasets 暫留 v2 |
| `docs/` | v3 規劃與進度 | `v3-架構規劃.md`、腳位分配（沿用） |

## 與 v2 的關係

- **沿用不動**：硬體、腳位分配、IMU 警示邏輯、TurnSwitch 方向燈、行車紀錄
- **v2 保留封存**：espdet_pico/ESP-DL on-device 推論成果（已擱置）、
  YOLO11n 3 類訓練（v3 重訓為 2 類）、訓練 venv 與 datasets（venv 不可搬移，
  重組資料時再於 v3 重建）
- **v3 新增**：站點批次上傳、伺服器端推論標記、GPS 地圖統計

## 待辦（優先序，詳見 docs/v3-架構規劃.md 第五節）

1. 韌體：log.csv 補 GPS 座標＋時間戳
2. ML：流浪狗公開資料集 → 2 類重訓 YOLO11n
3. 伺服器：批次上傳 API ＋ 推論標記
4. 伺服器：地圖路線＋標記統計（Leaflet+OSM）
5. 伺服器：對外連線（Cloudflare Tunnel vs DDNS）＋ 24hr 維運
6. 韌體：站點 WiFi 批次上傳模式
7. 站點服務機（先用現有電腦扮演）
8. 端到端測試
