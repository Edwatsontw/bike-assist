"""
2026-07-20 下載 COCO 的 motorcycle / car / bicycle 三類（街景實拍，在本機執行）

新增車輛類別（路邊機車/轎車/腳踏車）。COCO 這三類都有大量街景標註：
  bicycle、car、motorcycle。用 fiftyone 只抓含這三類的圖，輸出 YOLO 格式。

匯出時 classes 順序固定為 ["motorcycle","car","bicycle"]，所以匯出的 label
class index 是 0=motorcycle、1=car、2=bicycle。合併腳本會再 remap 成本專案的
2=motorcycle、3=car、4=bicycle（0/1 留給既有的 pothole/dog）。

執行（PowerShell，用訓練 venv）：
  cd "C:\\Users\\Edwatson\\Desktop\\BIKEEEE\\bike-assist-v3\\ml"
  .venv-train\\Scripts\\python.exe -m pip install fiftyone   # 若還沒裝
  .venv-train\\Scripts\\python.exe download_coco_vehicles.py

注意：
  - car 在 COCO 數量很多（數萬張），這裡 train+val 全含這三類的圖都會抓，
    可能好幾 GB，留意 SSD 空間。若太大想限量，可在 load_zoo_dataset 加
    max_samples=8000 之類（三類合計上限）。
  - 一張圖常同時有多種車，只保留這三類的框（only_matching）。
"""
import fiftyone as fo
import fiftyone.zoo as foz

OUT = "raw_downloads/coco-vehicles"
CLASSES = ["motorcycle", "car", "bicycle"]  # 匯出順序 → 0/1/2，合併時 remap 成 2/3/4

print("=== 載入 COCO 三類車（train+val，只含這三類的圖）===")
dataset = foz.load_zoo_dataset(
    "coco-2017",
    splits=["train", "validation"],
    label_types=["detections"],
    classes=CLASSES,
    only_matching=True,   # 只留這三類的框
    # max_samples=8000,   # 想限量再打開（car 太多時）
    dataset_name="coco-vehicles-v3",
)
print(f"總共 {len(dataset)} 張含車的圖")

print("=== 匯出 YOLO 格式 →", OUT, "===")
dataset.export(
    export_dir=OUT,
    dataset_type=fo.types.YOLOv5Dataset,
    classes=CLASSES,
    label_field="ground_truth",
)
print("完成。匯出 class：0=motorcycle 1=car 2=bicycle（合併時 +2 remap 成 2/3/4）。")
