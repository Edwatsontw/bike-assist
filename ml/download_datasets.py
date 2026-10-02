import os
"""
2026-07-20 資料集下載腳本（在本機執行，不是沙盒——api.roboflow.com 對沙盒網路是擋的）

下載兩個新資料集到 ml/raw_downloads/：
  1. stray-animal-detection v14（流浪狗，含 dog/cat/person，YOLO11 格式）
  2. road-damage-uyvns v3（路況，6 類含 pothole_water 積水坑洞，YOLO11 格式）

執行前：
  py -3.11 -m pip install roboflow --break-system-packages
執行：
  cd ml
  py -3.11 download_datasets.py

下載完不會自動合併，先讓你看過資料再決定第二步（見
docs/每日進度/2026-07-20_資料集候選清單.md 的待決事項：路況 6 類要怎麼跟
pothole/dog 合併——原樣保留 6 類、還是把 pothole_water/pothole_water_m
歸併進 pothole？）。
"""
from roboflow import Roboflow

API_KEY = os.environ["ROBOFLOW_API_KEY"]  # 2026-10-02：金鑰改放環境變數，不進 Git

rf = Roboflow(api_key=API_KEY)

print("=== 下載 1/2：流浪狗 (stray-animal-detection v14) ===")
proj1 = rf.workspace("rep-rxi6f").project("stray-animal-detection")
ds1 = proj1.version(14).download("yolov11", location="raw_downloads/stray-animal-detection-v14")
print("完成 →", ds1.location)

print("\n=== 下載 2/2：路況 (road-damage-uyvns v3) ===")
proj2 = rf.workspace("roaddamage-ak8w6").project("road-damage-uyvns")
ds2 = proj2.version(3).download("yolov11", location="raw_downloads/road-damage-v3")
print("完成 →", ds2.location)

print("\n=== 全部下載完成 ===")
print("流浪狗：", ds1.location)
print("路況　：", ds2.location)
