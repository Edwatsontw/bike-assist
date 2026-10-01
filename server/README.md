# bike-assist v3 伺服器

FastAPI 後端：接收車機上傳的感測資料/影像、提供即時儀表板與倒車事件地圖。
**埠固定 8000**（韌體 `SERVER_PORT=8000` 會推資料到閘道:8000，兩邊必須一致）。

## 用 Docker 跑（推薦，家用 24hr 伺服器）

```bash
cd server
docker compose up -d          # 背景啟動
docker compose logs -f        # 看 log
docker compose down           # 關閉
```

- 資料庫存在 `server/data/bike_data.db`（掛 volume，容器重建不遺失）
- 車機上傳影像存在 `server/uploads/`（未來伺服器端偵測用）
- 有 healthcheck（打 `/api/health`），`docker ps` 會顯示 healthy
- 非 root 執行

## 不用 Docker 直接跑（開發用）

```bash
cd server
pip install -r requirements.txt
uvicorn main:app --host 0.0.0.0 --port 8000
```

## 頁面

| 路徑 | 說明 |
|---|---|
| `/` | 主控台（側邊欄：儀表板 / 鏡頭串流 / 倒車事件） |
| `/camera` | 獨立鏡頭串流頁（原生 MJPEG） |
| `/fallen` | 獨立倒車事件地圖（輕量版，同資料整合在 `/` 主控台內） |

## API

| 方法 路徑 | 說明 |
|---|---|
| GET `/api/health` | 健康檢查（Docker healthcheck 用） |
| POST `/api/data` | 車機上傳感測資料（roll/加速度/GPS…） |
| GET `/api/data?limit=N` | 查最近 N 筆感測資料 |
| POST `/api/frame` | 車機上傳 JPEG 影格（image/jpeg raw） |
| GET `/api/frame` | 取最新單張影格 |
| GET `/api/stream` | 原生 MJPEG 串流 |
| POST `/api/fallen` | 倒車事件回報（app 掃到 BLE 後傳座標：`{lat,lon,device_ts}`） |
| GET `/api/fallen?only_unresolved=true` | 查倒車事件 |
| POST `/api/fallen/{id}/resolve` | 標記某倒車事件已扶正 |

## 資料流（v3 架構）

```
車機 ──WiFi──> POST /api/data、/api/frame ──> SQLite / 記憶體 ──> 主控台即時顯示
車機倒車 ──BLE廣播──> 附近手機app ──> POST /api/fallen ──> SQLite ──> 倒車事件地圖
```

## 待接（下一步）

- 伺服器端 YOLO11n 偵測：收到影格後跑 pothole/dog/機車/轎車/腳踏車偵測，
  標記存 `uploads/` 並依 GPS 上地圖（需另裝 ultralytics/torch，會讓映像變大，
  屆時建議獨立成一個 inference 容器，不要塞進這個輕量 web 容器）。
