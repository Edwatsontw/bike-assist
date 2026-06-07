from contextlib import asynccontextmanager
from pathlib import Path
from fastapi import FastAPI, HTTPException, Query
from fastapi.responses import FileResponse
from models import SensorPayload
from database import init_db, insert_record, get_latest

TEMPLATES = Path(__file__).parent / "templates"

@asynccontextmanager
async def lifespan(app: FastAPI):
    init_db()
    print("[Server] 資料庫初始化完成")
    yield

app = FastAPI(title="Bike Sensor Server", lifespan=lifespan)

@app.get("/")
def dashboard():
    return FileResponse(TEMPLATES / "index.html")

@app.post("/api/data")
def receive_data(payload: SensorPayload):
    try:
        insert_record(payload)
        return {"status": "ok"}
    except Exception as e:
        raise HTTPException(status_code=500, detail=str(e))

@app.get("/api/data")
def query_data(limit: int = Query(default=100, le=1000)):
    return get_latest(limit)
