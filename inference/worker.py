"""
bike-assist v3 伺服器端偵測 worker（2026-07-20）

角色：獨立服務（可 GPU），輪詢 web 伺服器拿「待推論影像」，跑 YOLO11n（5 類：
pothole/dog/motorcycle/car/bicycle），把偵測結果 POST 回 web 伺服器存檔＋上地圖。

與 web 伺服器解耦：只透過 HTTP 溝通（SERVER_URL），影像從 /api/uploads 取，
結果 POST /api/detections。因此 worker 可跑在同機 GPU、也可另一台機器。

環境變數：
  SERVER_URL   web 伺服器位址（預設 http://bike-server:8000，compose 內服務名）
  MODEL_PATH   YOLO 權重（預設 /model/best.pt，compose 掛進來）
  DEVICE       'cuda:0' 或 'cpu'（預設 cuda:0，抓不到會自動退 cpu）
  CONF         信心門檻（預設 0.25）
  POLL_SEC     沒待辦時的輪詢間隔秒（預設 5）
  BATCH        每輪最多處理幾張（預設 16）
"""
import io
import os
import time
import requests
from PIL import Image
from ultralytics import YOLO

SERVER   = os.environ.get("SERVER_URL", "http://bike-server:8000").rstrip("/")
MODEL    = os.environ.get("MODEL_PATH", "/model/best.pt")
DEVICE   = os.environ.get("DEVICE", "cuda:0")
CONF     = float(os.environ.get("CONF", "0.25"))
POLL_SEC = float(os.environ.get("POLL_SEC", "5"))
BATCH    = int(os.environ.get("BATCH", "16"))
# 只回報這些類別（逗號分隔）；空字串=全部。2026-07-21：需求改為只標記
# 流浪狗與路面坑洞（未來加 cat），車輛類偵測到也不存。
KEEP     = {c.strip() for c in os.environ.get("KEEP_CLASSES", "").split(",") if c.strip()}


def pick_device():
    try:
        import torch
        if DEVICE.startswith("cuda") and torch.cuda.is_available():
            print(f"[worker] GPU: {torch.cuda.get_device_name(0)}")
            return DEVICE
    except Exception as e:
        print("[worker] torch/cuda 檢查失敗:", e)
    print("[worker] 使用 CPU（沒抓到 GPU，會較慢）")
    return "cpu"


def main():
    dev = pick_device()
    print(f"[worker] 載入模型 {MODEL} ...")
    model = YOLO(MODEL)
    names = model.names  # {0:'pothole',...}
    print(f"[worker] 類別: {names}")
    print(f"[worker] 連線 {SERVER}，開始輪詢待推論影像")

    while True:
        try:
            pend = requests.get(f"{SERVER}/api/pending", params={"limit": BATCH}, timeout=10)
            frames = pend.json() if pend.ok else []
        except Exception as e:
            print("[worker] 拿待辦失敗:", e); time.sleep(POLL_SEC); continue

        if not frames:
            time.sleep(POLL_SEC); continue

        for fr in frames:
            fid, fn = fr["id"], fr["filename"]
            try:
                img = requests.get(f"{SERVER}/api/uploads/{fn}", timeout=15)
                if not img.ok:
                    # 影像取不到，仍回報空偵測讓它標記 processed，避免卡住佇列
                    requests.post(f"{SERVER}/api/detections",
                                  json={"frame_id": fid, "detections": []}, timeout=10)
                    continue
                pil = Image.open(io.BytesIO(img.content)).convert("RGB")
                res = model.predict(source=pil, conf=CONF,
                                    device=dev, verbose=False)
                dets = []
                for r in res:
                    for b in r.boxes:
                        cls_id = int(b.cls[0])
                        cls_name = names.get(cls_id, str(cls_id))
                        if KEEP and cls_name not in KEEP:
                            continue   # 不在保留清單的類別（車輛等）不回報
                        cx, cy, w, h = b.xywhn[0].tolist()  # 歸一化中心座標
                        dets.append({
                            "cls": cls_name,
                            "conf": round(float(b.conf[0]), 4),
                            "cx": round(cx, 5), "cy": round(cy, 5),
                            "w": round(w, 5), "h": round(h, 5),
                        })
                requests.post(f"{SERVER}/api/detections",
                              json={"frame_id": fid, "detections": dets}, timeout=15)
                print(f"[worker] frame {fid} {fn} → {len(dets)} 個偵測")
            except Exception as e:
                print(f"[worker] 處理 frame {fid} 失敗:", e)
                time.sleep(1)


if __name__ == "__main__":
    main()
