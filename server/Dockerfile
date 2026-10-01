# bike-assist v3 伺服器（FastAPI）
# 2026-07-20 重寫：修正埠號 5000→8000（與韌體 SERVER_PORT 一致，否則收不到車機資料）、
# 非 root 執行、加 healthcheck、資料與上傳影像分開掛載。
FROM python:3.12-slim

# curl 供 healthcheck 用
RUN apt-get update && apt-get install -y --no-install-recommends curl \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

# 先裝相依（利用 layer cache，改程式碼不會重裝套件）
COPY requirements.txt .
RUN pip install --no-cache-dir -r requirements.txt

# 再複製程式碼
COPY . .

# 資料與上傳影像目錄（compose 會掛 volume 進來，確保存在＆可寫）
RUN mkdir -p /app/data /app/uploads

# 2026-07-20：以 root 執行。原本切非 root(appuser)，但 Windows/掛載目錄
# ./data 的擁有者非 appuser，容器內建資料庫時 sqlite 報 "readonly database"
# 而啟動失敗。家用區網伺服器用 root 跑最省事、避開掛載權限問題（安全影響有限）。

EXPOSE 8000

ENV DB_PATH=/app/data/bike_data.db \
    UPLOAD_DIR=/app/uploads

# 健康檢查：打 /api/health
HEALTHCHECK --interval=30s --timeout=5s --start-period=10s --retries=3 \
    CMD curl -fsS http://localhost:8000/api/health || exit 1

CMD ["uvicorn", "main:app", "--host", "0.0.0.0", "--port", "8000"]
