"""
2026-07-20 下載 COCO 的 dog 類（街景/自然實拍，補強狗偵測，在本機執行）

背景：Roboflow 上的狗資料集不是視角不對（CCTV 俯視），就是量太小（幾百張、
甚至只有 87 張有標註）。COCO 的 dog 類有約 5000 張專業標註的自然/街景實拍狗，
視角接近腳踏車車後平視，是最好的補強來源。

用 fiftyone 只抓「含狗的圖」，不會下載整個 12 萬張 COCO。輸出成 YOLO 格式，
之後合併時把 class 統一設為 1（dog）。

執行前（在 v3 訓練 venv 裡裝，避免污染）：
  cd ml
  .venv-train\Scripts\python.exe -m pip install fiftyone
執行：
  .venv-train\Scripts\python.exe download_coco_dogs.py

注意：
  - 首次會下載 COCO 標註檔（約數百 MB）+ 含狗的圖片（約 1-2GB），要留 SSD 空間
  - COCO 一張圖可能同時有狗和其他物件，fiftyone 匯出時我們只保留 dog 標註框
  - 匯出到 raw_downloads/coco-dogs/，結構為 YOLO（images/ + labels/）
"""
import fiftyone as fo
import fiftyone.zoo as foz

OUT = "raw_downloads/coco-dogs"

# 只載含 dog 的圖，train + validation 都要（越多越好）
print("=== 載入 COCO dog 子集（train+val，只含狗的圖）===")
splits = ["train", "validation"]
dataset = foz.load_zoo_dataset(
    "coco-2017",
    splits=splits,
    label_types=["detections"],
    classes=["dog"],
    only_matching=True,   # 只保留 dog 這個類的標註框，其他物件框丟棄
    dataset_name="coco-dogs-v3",
)

print(f"總共 {len(dataset)} 張含狗的圖")

# 匯出成 YOLOv5 格式（images/ + labels/ + dataset.yaml）
print("=== 匯出 YOLO 格式 →", OUT, "===")
dataset.export(
    export_dir=OUT,
    dataset_type=fo.types.YOLOv5Dataset,
    classes=["dog"],
    label_field="ground_truth",
)
print("完成。合併時把所有 label 的 class index 統一設為 1（dog）。")
