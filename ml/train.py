#!/usr/bin/env python3
"""
SafeWay / bike-assist-v3 訓練腳本（自 v2 複製，2026-07-20 改為 v3 用途）
用途：訓練 YOLO11n 物件偵測模型（pothole / dog，v3 重新規劃後的 2 類），
偵測改在伺服器端跑（不部署到 ESP32-S3，espdet_pico/ESP-DL 線已擱置）。

用法：
    python train.py                  # 預設 imgsz=224 訓練
    python train.py --imgsz 320      # 第二輪測試，改用 320
    python train.py --epochs 50 --imgsz 320 --model yolo11n.pt

腳本會先做環境檢查（Python 版本、ultralytics 是否安裝、CUDA 是否可用、
data.yaml 是否存在），確認無誤後才啟動訓練。

注意：ROOT 是「這支腳本自己所在的資料夾」，所以只要放在 v3 的 ml/ 底下執行，
就會自動吃 v3 的 ml/data.yaml（pothole/dog），不用改路徑。
"""

import argparse
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent
DATA_YAML = ROOT / "data.yaml"


def check_and_install_ultralytics():
    try:
        import ultralytics  # noqa: F401
        print(f"[OK] ultralytics 已安裝，版本：{ultralytics.__version__}")
    except ImportError:
        print("[INFO] 未偵測到 ultralytics，開始安裝...")
        subprocess.run(
            [sys.executable, "-m", "pip", "install", "-U", "ultralytics"],
            check=True,
        )
        import ultralytics  # noqa: F401
        print(f"[OK] ultralytics 安裝完成，版本：{ultralytics.__version__}")


def check_environment():
    print("=" * 60)
    print("環境檢查")
    print("=" * 60)
    print(f"Python 版本: {sys.version.split()[0]}")

    check_and_install_ultralytics()

    try:
        import torch
        cuda_ok = torch.cuda.is_available()
        print(f"PyTorch 版本: {torch.__version__}")
        print(f"CUDA 可用: {cuda_ok}")
        if cuda_ok:
            print(f"GPU: {torch.cuda.get_device_name(0)}")
        else:
            print("[提醒] 目前將使用 CPU 訓練，速度會較慢。")
    except ImportError:
        print("[警告] 找不到 torch，ultralytics 安裝時應會自動附帶安裝，請確認安裝過程無誤。")

    if not DATA_YAML.exists():
        print(f"[錯誤] 找不到 data.yaml：{DATA_YAML}")
        sys.exit(1)
    print(f"[OK] data.yaml 存在：{DATA_YAML}")

    datasets_dir = ROOT / "datasets"
    for sub in ["images/train", "images/val", "labels/train", "labels/val"]:
        p = datasets_dir / sub
        n = len(list(p.glob("*"))) if p.exists() else 0
        status = "OK" if n > 0 else "空的，尚未放資料"
        print(f"  - datasets/{sub}: {n} 個檔案 [{status}]")

    print("=" * 60)


def run_training(args):
    # 2026-07-20 改用 Ultralytics Python API 直接訓練，不再 subprocess 呼叫
    # `yolo` CLI——bat 檔啟動時 venv 的 Scripts\ 不一定在 PATH 上，
    # 會炸 FileNotFoundError（WinError 2）。Python API 只要 import 得到
    # ultralytics 就能跑，不依賴 PATH。
    from ultralytics import YOLO

    model = YOLO(args.model)
    train_kwargs = dict(
        data=str(DATA_YAML),
        epochs=args.epochs,
        imgsz=args.imgsz,
        batch=args.batch,
        project=args.project,
        name=args.name,
    )
    if args.device:
        train_kwargs["device"] = args.device
    print("即將執行 model.train(", train_kwargs, ")")
    model.train(**train_kwargs)


def main():
    parser = argparse.ArgumentParser(description="SafeWay YOLO 訓練腳本")
    parser.add_argument("--model", default="yolo11n.pt", help="預訓練權重，預設 yolo11n.pt")
    parser.add_argument("--epochs", type=int, default=100, help="訓練 epoch 數，預設 100")
    parser.add_argument("--imgsz", type=int, default=640,
                         help="輸入影像大小，預設 640（2026-07-20 從224提高：狗為小目標，"
                              "224縮圖後漏檢嚴重；伺服器端推論無MCU限制）")
    parser.add_argument("--batch", type=int, default=16, help="batch size，預設 16")
    parser.add_argument("--project", default="runs", help="輸出目錄，預設 runs/")
    parser.add_argument("--name", default="safeway_yolo11n",
                         help="本次訓練輸出的資料夾名稱")
    parser.add_argument("--device", default="", help="指定裝置，例如 0 或 cpu，留空自動偵測")
    parser.add_argument("--skip-env-check", action="store_true",
                         help="跳過環境檢查與 ultralytics 自動安裝")
    args = parser.parse_args()

    if not args.skip_env_check:
        check_environment()

    run_training(args)


if __name__ == "__main__":
    main()
