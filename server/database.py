import os
import sqlite3
from contextlib import contextmanager
from models import SensorPayload

DB_PATH = os.environ.get("DB_PATH", "bike_data.db")

@contextmanager
def _conn():
    conn = sqlite3.connect(DB_PATH)
    conn.row_factory = sqlite3.Row
    try:
        yield conn
        conn.commit()
    except:
        conn.rollback()
        raise
    finally:
        conn.close()

def init_db():
    with _conn() as conn:
        conn.execute('''
            CREATE TABLE IF NOT EXISTS sensor_logs (
                id          INTEGER PRIMARY KEY AUTOINCREMENT,
                timestamp   TEXT    DEFAULT (datetime('now','localtime')),
                roll        REAL,
                pitch       REAL,
                gx          REAL,
                gy          REAL,
                gz          REAL,
                accel_x     REAL,
                accel_y     REAL,
                accel_z     REAL,
                accel_event TEXT,
                lat         REAL,
                lon         REAL,
                alt         REAL,
                speed       REAL
            )
        ''')
        conn.execute('''
            CREATE INDEX IF NOT EXISTS idx_sensor_logs_id
            ON sensor_logs(id DESC)
        ''')
        # 倒車事件（2026-07-20 v3；2026-07-22 加兩段式 level）：
        # app 掃到 BLE 廣播後 POST 座標進來。level: 'notice'=倒下10秒一般通知，
        # 'emergency'=持續到5分鐘仍未扶正的緊急事故（同一次倒下可能先後各來一筆）。
        conn.execute('''
            CREATE TABLE IF NOT EXISTS fallen_events (
                id         INTEGER PRIMARY KEY AUTOINCREMENT,
                timestamp  TEXT    DEFAULT (datetime('now','localtime')),
                device_ts  INTEGER,
                lat        REAL,
                lon        REAL,
                level      TEXT    DEFAULT 'notice',
                resolved   INTEGER DEFAULT 0
            )
        ''')
        # 舊資料庫沒有 level 欄位時補上（新建的表本來就有，這裡是相容既有 DB）
        try:
            conn.execute("ALTER TABLE fallen_events ADD COLUMN level TEXT DEFAULT 'notice'")
        except sqlite3.OperationalError:
            pass  # 欄位已存在
        # 上傳影格（2026-07-20 v3 站點批次上傳）：一張影像一列，含 GPS。
        # processed: 0=待推論 1=已推論。filename 唯一（車機 SD 的檔名）。
        conn.execute('''
            CREATE TABLE IF NOT EXISTS uploaded_frames (
                id         INTEGER PRIMARY KEY AUTOINCREMENT,
                filename   TEXT    UNIQUE,
                cap_time   TEXT,
                lat        REAL,
                lon        REAL,
                has_image  INTEGER DEFAULT 0,
                processed  INTEGER DEFAULT 0,
                uploaded_at TEXT   DEFAULT (datetime('now','localtime'))
            )
        ''')
        # 偵測結果（inference worker 跑完 POST 回來）：一個框一列
        conn.execute('''
            CREATE TABLE IF NOT EXISTS detections (
                id        INTEGER PRIMARY KEY AUTOINCREMENT,
                frame_id  INTEGER,
                cls       TEXT,
                conf      REAL,
                cx        REAL,
                cy        REAL,
                w         REAL,
                h         REAL,
                lat       REAL,
                lon       REAL,
                cap_time  TEXT,
                filename  TEXT
            )
        ''')
        conn.execute('CREATE INDEX IF NOT EXISTS idx_frames_pending ON uploaded_frames(processed, has_image)')
        conn.execute('CREATE INDEX IF NOT EXISTS idx_det_cls ON detections(cls)')

def insert_fallen(lat: float, lon: float, device_ts: int = 0, level: str = "notice"):
    with _conn() as conn:
        conn.execute(
            "INSERT INTO fallen_events (device_ts, lat, lon, level) VALUES (?, ?, ?, ?)",
            (device_ts, lat, lon, level))

def get_fallen(only_unresolved: bool = False):
    q = "SELECT * FROM fallen_events"
    if only_unresolved:
        q += " WHERE resolved = 0"
    q += " ORDER BY id DESC"
    with _conn() as conn:
        rows = conn.execute(q).fetchall()
    return [dict(r) for r in rows]

def resolve_fallen(event_id: int):
    with _conn() as conn:
        conn.execute("UPDATE fallen_events SET resolved = 1 WHERE id = ?", (event_id,))

def insert_record(p: SensorPayload):
    with _conn() as conn:
        conn.execute('''
            INSERT INTO sensor_logs
                (roll, pitch, gx, gy, gz,
                 accel_x, accel_y, accel_z, accel_event,
                 lat, lon, alt, speed)
            VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
        ''', (p.roll, p.pitch, p.gx, p.gy, p.gz,
              p.accelX, p.accelY, p.accelZ, p.accelEvent,
              p.lat, p.lon, p.alt, p.speed))

def get_latest(limit: int = 100):
    with _conn() as conn:
        rows = conn.execute(
            "SELECT * FROM sensor_logs ORDER BY id DESC LIMIT ?", (limit,)
        ).fetchall()
    return [dict(r) for r in rows]

# ── 站點批次上傳 / 偵測（v3 2026-07-20）──────────────────────────

def upsert_frame_meta(filename: str, cap_time: str, lat: float, lon: float):
    """來自 log.csv 的一列：建立/更新該影格的 metadata（GPS/時間）。"""
    with _conn() as conn:
        conn.execute('''
            INSERT INTO uploaded_frames (filename, cap_time, lat, lon)
            VALUES (?, ?, ?, ?)
            ON CONFLICT(filename) DO UPDATE SET
                cap_time=excluded.cap_time, lat=excluded.lat, lon=excluded.lon
        ''', (filename, cap_time, lat, lon))

def mark_frame_image(filename: str):
    """影像檔實際上傳到 uploads/ 後標記 has_image=1（沒 meta 也先建一列）。"""
    with _conn() as conn:
        conn.execute('''
            INSERT INTO uploaded_frames (filename, has_image)
            VALUES (?, 1)
            ON CONFLICT(filename) DO UPDATE SET has_image=1
        ''', (filename,))

def get_pending_frames(limit: int = 50):
    """待推論：有影像、還沒處理過。"""
    with _conn() as conn:
        rows = conn.execute('''
            SELECT id, filename, lat, lon, cap_time FROM uploaded_frames
            WHERE has_image = 1 AND processed = 0
            ORDER BY id ASC LIMIT ?
        ''', (limit,)).fetchall()
    return [dict(r) for r in rows]

def insert_detections(frame_id: int, dets: list):
    """dets: [{cls,conf,cx,cy,w,h}]；lat/lon/cap_time/filename 由 frame 帶入。"""
    with _conn() as conn:
        fr = conn.execute("SELECT filename, lat, lon, cap_time FROM uploaded_frames WHERE id=?",
                          (frame_id,)).fetchone()
        if fr is None:
            return
        for d in dets:
            conn.execute('''
                INSERT INTO detections
                    (frame_id, cls, conf, cx, cy, w, h, lat, lon, cap_time, filename)
                VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
            ''', (frame_id, d["cls"], d["conf"], d["cx"], d["cy"], d["w"], d["h"],
                  fr["lat"], fr["lon"], fr["cap_time"], fr["filename"]))
        conn.execute("UPDATE uploaded_frames SET processed = 1 WHERE id = ?", (frame_id,))

def get_detections(cls: str = None, limit: int = 2000):
    q = "SELECT * FROM detections"
    args = []
    if cls:
        q += " WHERE cls = ?"; args.append(cls)
    q += " ORDER BY id DESC LIMIT ?"; args.append(limit)
    with _conn() as conn:
        rows = conn.execute(q, args).fetchall()
    return [dict(r) for r in rows]

def detection_summary():
    """各類別偵測數量統計（給地圖圖例/篩選用）。"""
    with _conn() as conn:
        rows = conn.execute(
            "SELECT cls, COUNT(*) n FROM detections GROUP BY cls").fetchall()
    return {r["cls"]: r["n"] for r in rows}
