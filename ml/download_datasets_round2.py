import os
"""
2026-07-20 資料集下載腳本 第二批（在本機執行，沙盒連不到 api.roboflow.com）

目標：把 pothole/dog 的漏檢率再往下壓（首輪 640 後 pothole R=0.77、dog R=0.68）。
新增兩個補強來源：
  3. indian-road-potholes v5（4306 張，單一 pothole 類，行車視角，CC BY 4.0）
     —— 補強坑洞，量大、視角接近車拍
  4. COCO dog 子集 —— 補強狗，自然/街景實拍，視角比現有 CCTV 俯視好很多
     （這裡示範用 Roboflow 上現成的 COCO dog-only 專案；若失效見下方備註）

執行前（若還沒裝）：
  py -3.11 -m pip install roboflow --break-system-packages
執行：
  cd ml
  py -3.11 download_datasets_round2.py

下載完先看過再合併（比照 round1 的做法，選項 C：全部 dog→class 1、
所有 pothole 類→class 0）。合併腳本沿用/擴充 round1 的邏輯。

備註（COCO dog）：
  COCO 完整資料集有 12 萬張太大，不要整包下載。下面用 Roboflow 上抽好的
  「dog only」公開專案。若該專案連結失效或內容不符，替代方案：
    - 從 COCO 官方用 pycocotools 只抽 category_id=18（dog）的圖與框，轉 YOLO 格式
    - 或改用清單裡其他街景狗資料集（Dog Detection oidv4 499張 等）
"""
from roboflow import Roboflow

API_KEY = os.environ["ROBOFLOW_API_KEY"]  # 2026-10-02：金鑰改放環境變數，不進 Git

rf = Roboflow(api_key=API_KEY)

print("=== 下載 3：坑洞補強 (indian-road-potholes v5) ===")
proj3 = rf.workspace("project-o3ot9").project("indian-road-potholes")
ds3 = proj3.version(5).download("yolov11", location="raw_downloads/indian-road-potholes-v5")
print("完成 →", ds3.location)

# COCO dog 子集：Roboflow 上有數個抽好的 dog-only / COCO-animals 專案。
# 下面這個先當範例；若下載失敗或內容不符，改用備註方案，不要卡在這裡。
print("\n=== 下載 4：狗補強 (COCO dog 子集，街景視角) ===")
try:
    proj4 = rf.workspace("yolo-lggkk").project("dogs-4i7ne")
    ds4 = proj4.version(1).download("yolov11", location="raw_downloads/dogs-street")
    print("完成 →", ds4.location)
except Exception as e:
    print("[狗補強下載失敗]", e)
    print("請改用備註方案（COCO 官方抽 dog 類，或清單其他街景狗資料集）。")

print("\n=== 下載結束 ===")
