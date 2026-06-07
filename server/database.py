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
