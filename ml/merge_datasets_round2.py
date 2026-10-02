"""
2026-07-20 第二批資料集合併（在本機執行，比沙盒掛載硬碟快很多；可續跑）

把兩個補強資料集併進現有 ml/datasets/（2類 0:pothole 1:dog）：
  - indian-road-potholes-v5：單一 pothole 類 → class 0（train→train, valid→val, test→train）
  - coco-dogs：class 0(dog) → 重新編為 class 1；全部在 val 資料夾，這裡自己切 9:1

特性：目的檔已存在就跳過（resume-safe）。若中途中斷，直接再跑一次即可補完。

執行（PowerShell）：
  cd "C:\\Users\\Edwatson\\Desktop\\BIKEEEE\\bike-assist-v3\\ml"
  .venv-train\\Scripts\\python.exe merge_datasets_round2.py
"""
import os, shutil, glob

ML = os.path.dirname(os.path.abspath(__file__))
V3 = os.path.join(ML, "datasets")
IND = os.path.join(ML, "raw_downloads", "indian-road-potholes-v5")
COCO = os.path.join(ML, "raw_downloads", "coco-dogs")

for split in ("train", "val"):
    os.makedirs(os.path.join(V3, "images", split), exist_ok=True)
    os.makedirs(os.path.join(V3, "labels", split), exist_ok=True)

stats = {"ind_pothole": 0, "coco_dog": 0, "skip_exist": 0, "skip_nobox": 0, "no_img": 0}


def process(label_path, img_dir, dst_split, prefix, force_class):
    stem = os.path.splitext(os.path.basename(label_path))[0]
    new_stem = f"{prefix}{stem}"
    dst_lbl = os.path.join(V3, "labels", dst_split, f"{new_stem}.txt")
    if os.path.exists(dst_lbl):
        stats["skip_exist"] += 1
        return False
    lines = []
    with open(label_path) as f:
        for line in f:
            p = line.strip().split()
            if len(p) >= 5:
                lines.append(f"{force_class} " + " ".join(p[1:]))
    if not lines:
        stats["skip_nobox"] += 1
        return False
    cands = glob.glob(os.path.join(img_dir, stem + ".*"))
    if not cands:
        stats["no_img"] += 1
        return False
    ext = os.path.splitext(cands[0])[1]
    shutil.copy(cands[0], os.path.join(V3, "images", dst_split, new_stem + ext))
    with open(dst_lbl, "w") as f:
        f.write("\n".join(lines) + "\n")
    return True


# ── indian-road-potholes：pothole → class 0 ──
print("=== 合併 indian-road-potholes (pothole → class 0) ===")
for src, dst in {"train": "train", "valid": "val", "test": "train"}.items():
    ld = os.path.join(IND, src, "labels")
    imd = os.path.join(IND, src, "images")
    if not os.path.isdir(ld):
        continue
    for lp in glob.glob(os.path.join(ld, "*.txt")):
        if process(lp, imd, dst, "indpot_", 0):
            stats["ind_pothole"] += 1

# ── coco-dogs：dog(0) → class 1；全在 val 資料夾，自己切 9:1 ──
print("=== 合併 coco-dogs (dog → class 1) ===")
coco_ld = os.path.join(COCO, "labels", "val")
coco_imd = os.path.join(COCO, "images", "val")
files = sorted(glob.glob(os.path.join(coco_ld, "*.txt")))
for i, lp in enumerate(files):
    dst = "val" if i % 10 == 0 else "train"
    if process(lp, coco_imd, dst, "cocodog_", 1):
        stats["coco_dog"] += 1

print("\n=== 完成 ===")
print(stats)
for s in ("train", "val"):
    ni = len(os.listdir(os.path.join(V3, "images", s)))
    nl = len(os.listdir(os.path.join(V3, "labels", s)))
    print(f"{s}: images={ni} labels={nl}")
