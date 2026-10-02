"""
2026-07-20 第三批合併：COCO 三類車併進 datasets/（本機執行，可續跑）

來源：raw_downloads/coco-vehicles（download_coco_vehicles.py 產出）
  匯出 class：0=motorcycle 1=car 2=bicycle → remap 2=motorcycle 3=car 4=bicycle（+2）

不平衡處理（使用者選「優先稀有類 + 限量 car」）：
  - 圖片含 motorcycle 或 bicycle → 全部收（連帶把圖裡的 car 框也收，car 仍有量）
  - 只含 car 的圖 → 限量收 CAR_ONLY_CAP 張（依檔名排序取前 N，deterministic 可續跑）
  這樣三類車較平衡，且車輛不會淹沒既有的 pothole(~8500)/dog(~6500)。

現有 pothole(0)/dog(1) 標籤不動。fiftyone 通常把 train+val 放單一 val 資料夾，
腳本自動偵測來源並自己切 9:1。目的檔已存在就跳過（resume-safe）。

執行（PowerShell）：
  cd "C:\\Users\\Edwatson\\Desktop\\BIKEEEE\\bike-assist-v3\\ml"
  .venv-train\\Scripts\\python.exe merge_datasets_round3.py
"""
import os, shutil, glob

ML = os.path.dirname(os.path.abspath(__file__))
V3 = os.path.join(ML, "datasets")
COCO = os.path.join(ML, "raw_downloads", "coco-vehicles")

REMAP = {0: 2, 1: 3, 2: 4}   # motorcycle→2, car→3, bicycle→4
CAR_ONLY_CAP = 3000          # 只含 car 的圖上限（避免 car 壓過其他兩類）

for split in ("train", "val"):
    os.makedirs(os.path.join(V3, "images", split), exist_ok=True)
    os.makedirs(os.path.join(V3, "labels", split), exist_ok=True)

stats = {"copied": 0, "skip_exist": 0, "no_img": 0,
         "car_only_included": 0, "car_only_skipped_cap": 0,
         "has_rare": 0}


def read_classes(label_path):
    """回傳這張圖 remap 後的 (lines, class_set)。"""
    lines, classes = [], set()
    with open(label_path) as f:
        for line in f:
            p = line.strip().split()
            if len(p) < 5:
                continue
            c = int(p[0])
            if c not in REMAP:
                continue
            nc = REMAP[c]
            lines.append(f"{nc} " + " ".join(p[1:]))
            classes.add(nc)
    return lines, classes


def write_out(stem, lines, img_dir, dst_split):
    new_stem = f"cocoveh_{stem}"
    dst_lbl = os.path.join(V3, "labels", dst_split, f"{new_stem}.txt")
    if os.path.exists(dst_lbl):
        stats["skip_exist"] += 1
        return
    cands = glob.glob(os.path.join(img_dir, stem + ".*"))
    if not cands:
        stats["no_img"] += 1
        return
    ext = os.path.splitext(cands[0])[1]
    shutil.copy(cands[0], os.path.join(V3, "images", dst_split, new_stem + ext))
    with open(dst_lbl, "w") as f:
        f.write("\n".join(lines) + "\n")
    stats["copied"] += 1


# 找 coco-vehicles 底下所有 labels 資料夾
label_dirs = []
for root, dirs, files in os.walk(COCO):
    if os.path.basename(root) == "labels":
        for sub in os.listdir(root):
            p = os.path.join(root, sub)
            if os.path.isdir(p):
                label_dirs.append(p)
print("label 來源：", label_dirs)

idx = 0
for ld in label_dirs:
    imd = ld.replace(os.sep + "labels" + os.sep, os.sep + "images" + os.sep)
    for lp in sorted(glob.glob(os.path.join(ld, "*.txt"))):
        stem = os.path.splitext(os.path.basename(lp))[0]
        lines, classes = read_classes(lp)
        if not lines:
            continue
        has_rare = (2 in classes) or (4 in classes)   # motorcycle 或 bicycle
        if has_rare:
            stats["has_rare"] += 1
        else:
            # 只含 car：套上限（依排序取前 N，deterministic）
            if stats["car_only_included"] >= CAR_ONLY_CAP:
                stats["car_only_skipped_cap"] += 1
                continue
            stats["car_only_included"] += 1
        dst = "val" if idx % 10 == 0 else "train"
        write_out(stem, lines, imd, dst)
        idx += 1

print("\n=== 完成 ===")
print(stats)
for s in ("train", "val"):
    ni = len(os.listdir(os.path.join(V3, "images", s)))
    print(f"{s}: images={ni}")
