import asyncio
import csv
import io
import os
import time
from contextlib import asynccontextmanager
from pathlib import Path
from fastapi import FastAPI, HTTPException, Query, Request, UploadFile, File
from fastapi.responses import FileResponse, Response, HTMLResponse, StreamingResponse
from pydantic import BaseModel
from models import SensorPayload
from database import (init_db, insert_record, get_latest,
                      insert_fallen, get_fallen, resolve_fallen,
                      upsert_frame_meta, mark_frame_image, get_pending_frames,
                      insert_detections, get_detections, detection_summary)

TEMPLATES = Path(__file__).parent / "templates"
UPLOAD_DIR = Path(os.environ.get("UPLOAD_DIR", "uploads"))
UPLOAD_DIR.mkdir(parents=True, exist_ok=True)

@asynccontextmanager
async def lifespan(app: FastAPI):
    init_db()
    print("[Server] 資料庫初始化完成")
    yield

app = FastAPI(title="Bike Sensor Server", lifespan=lifespan)

@app.get("/")
def dashboard():
    return FileResponse(TEMPLATES / "index.html")

@app.get("/api/health")
def health():
    return {"status": "ok"}

@app.post("/api/data")
def receive(payload: SensorPayload):
    try:
        insert_record(payload)
        return {"ok": True}
    except Exception as e:
        raise HTTPException(status_code=500, detail=str(e))

@app.get("/api/data")
def query_data(limit: int = Query(default=100, le=1000)):
    return get_latest(limit)

# ── 倒車事件（2026-07-20 v3；2026-07-22 改兩段式）────────────
# 流程：車體倒地 → ESP32 BLE廣播（10秒=notice通知，若持續到5分鐘再多廣播一次
#       升級為 emergency）→ 附近手機app掃到 → app POST 座標到這裡 → 存DB
#       → /fallen 地圖標記，供維護扶正。level 由 app 依 BLE payload 第0byte
#       （0x01=notice, 0x02=emergency）轉換文字傳入，預設 notice 相容舊版app。
class FallenReport(BaseModel):
    lat: float
    lon: float
    device_ts: int = 0
    level: str = "notice"   # "notice"=一般通知（倒下10秒） / "emergency"=緊急事故（5分鐘未扶正）

@app.post("/api/fallen")
def receive_fallen(r: FallenReport):
    try:
        insert_fallen(r.lat, r.lon, r.device_ts, r.level)
        return {"ok": True}
    except Exception as e:
        raise HTTPException(status_code=500, detail=str(e))

@app.get("/api/fallen")
def query_fallen(only_unresolved: bool = Query(default=False)):
    return get_fallen(only_unresolved)

@app.post("/api/fallen/{event_id}/resolve")
def mark_resolved(event_id: int):
    resolve_fallen(event_id)
    return {"ok": True}

# ── 站點批次上傳 + 伺服器端偵測（2026-07-20 v3）───────────────
# 流程：車機到站連 WiFi → 上傳 log.csv（影像↔GPS 對應）+ 各張影像 →
#       inference worker 輪詢 /api/pending 拿待推論影像 → 跑 YOLO →
#       POST /api/detections 回存 → /detections 地圖依 GPS 標記
_ALLOWED_IMG = {".jpg", ".jpeg", ".png"}

@app.post("/api/upload/log")
async def upload_log(request: Request):
    """接收 SD 的 log.csv（text/csv）：解析 filename↔lat/lon/time 建 frame metadata。
    v3 log.csv 欄位：index,time,event,roll,pitch,accel,lat,lon,filename"""
    raw = (await request.body()).decode("utf-8", errors="replace")
    n = 0
    reader = csv.DictReader(io.StringIO(raw))
    for row in reader:
        fn = (row.get("filename") or "").strip()
        if not fn:
            continue
        fn = os.path.basename(fn)              # 只留檔名，防路徑
        def _f(x):
            try: return float(x)
            except: return 0.0
        upsert_frame_meta(fn, (row.get("time") or "").strip(),
                          _f(row.get("lat")), _f(row.get("lon")))
        n += 1
    return {"ok": True, "rows": n}

@app.post("/api/upload/frame/{filename}")
async def upload_frame_file(filename: str, request: Request):
    """接收單張影像（raw jpeg body），存 uploads/ 並標記 has_image。"""
    filename = os.path.basename(filename)
    ext = os.path.splitext(filename)[1].lower()
    if ext not in _ALLOWED_IMG:
        raise HTTPException(status_code=400, detail="only jpg/png")
    data = await request.body()
    (UPLOAD_DIR / filename).write_bytes(data)
    mark_frame_image(filename)
    return {"ok": True, "size": len(data)}

@app.get("/api/pending")
def api_pending(limit: int = Query(default=50, le=200)):
    """inference worker 用：拿待推論影像清單。"""
    return get_pending_frames(limit)

@app.get("/api/uploads/{filename}")
def api_get_upload(filename: str):
    """inference worker / UI 取上傳的影像檔。"""
    filename = os.path.basename(filename)
    p = UPLOAD_DIR / filename
    if not p.exists():
        raise HTTPException(status_code=404, detail="not found")
    return FileResponse(p)

class DetectionPost(BaseModel):
    frame_id: int
    detections: list  # [{cls,conf,cx,cy,w,h}]（歸一化中心座標）

@app.post("/api/detections")
def api_post_detections(d: DetectionPost):
    """inference worker 推論完 POST 回結果，存 DB 並標記該 frame processed。"""
    insert_detections(d.frame_id, d.detections)
    return {"ok": True, "n": len(d.detections)}

@app.get("/api/detections")
def api_get_detections(cls: str = Query(default=None), limit: int = Query(default=2000, le=10000)):
    return get_detections(cls, limit)

@app.get("/api/detections/summary")
def api_det_summary():
    return detection_summary()

# 2026-10-01：/fallen、/camera 獨立頁已移除，功能統一在首頁（儀表板/偵測地圖/倒車事件）。

# ── 相機即時影像 ─────────────────────────────────
_latest_frame: bytes = b""
_latest_frame_ts: float = 0.0

@app.post("/api/frame")
async def receive_frame(request: Request):
    """接收 ESP32 POST 的 JPEG 影格（raw bytes, Content-Type: image/jpeg）"""
    global _latest_frame, _latest_frame_ts
    _latest_frame = await request.body()
    _latest_frame_ts = time.time()
    return {"ok": True, "size": len(_latest_frame)}

@app.get("/api/frame")
def get_frame():
    """回傳最新一張影格（單張，供除錯/相容用）"""
    if not _latest_frame:
        raise HTTPException(status_code=404, detail="尚未收到任何影格")
    return Response(_latest_frame, media_type="image/jpeg",
                    headers={"Cache-Control": "no-store"})

# ── 原生 MJPEG 串流（2026-07-20 新增）─────────────────────────────
# 跟 ESP32 自己 /stream 用同一種手法（multipart/x-mixed-replace）：
# 瀏覽器只發一次請求、伺服器維持連線持續推新影格，<img> 原生渲染，
# 不需要 JS 輪詢／blob URL，比舊版 /camera（每 200ms 重新 fetch 單張）順很多。
STREAM_BOUNDARY = b"frame"
POLL_INTERVAL   = 0.05  # 伺服器內部檢查有無新影格的頻率（非對外請求頻率）

async def _mjpeg_generator():
    last_ts = 0.0
    while True:
        if _latest_frame and _latest_frame_ts != last_ts:
            last_ts = _latest_frame_ts
            yield (b"--" + STREAM_BOUNDARY + b"\r\n"
                   b"Content-Type: image/jpeg\r\n"
                   b"Content-Length: " + str(len(_latest_frame)).encode() + b"\r\n\r\n"
                   + _latest_frame + b"\r\n")
        await asyncio.sleep(POLL_INTERVAL)

@app.get("/api/stream")
async def video_stream():
    return StreamingResponse(
        _mjpeg_generator(),
        media_type=f"multipart/x-mixed-replace; boundary={STREAM_BOUNDARY.decode()}",
        headers={"Cache-Control": "no-store"},
    )
