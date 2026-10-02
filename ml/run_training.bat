@echo off
setlocal enabledelayedexpansion
cd /d "%~dp0"

REM ============================================================
REM  bike-assist v3 training launcher (pothole + dog)
REM  Builds its OWN .venv-train in this folder (no v2 sharing).
REM  ASCII-only on purpose: Chinese chars in a .bat break cmd.exe
REM  parsing on Big5 codepage systems. Keep this file English-only.
REM  imgsz=640 (up from 224): dogs are tiny in the CCTV dataset,
REM  224 downscale made them nearly undetectable. Server-side
REM  inference has no MCU limit so 640 is fine.
REM ============================================================
echo ============================================================
echo bike-assist v3 training launcher (pothole+dog, RTX 5070/cu128)
echo Builds its OWN .venv-train here - no v2 sharing
echo ============================================================
echo.

REM --- pick a Python that has cu128 torch wheels (3.11/3.12/3.13) ---
set "PYEXE="
for %%V in (3.11 3.12 3.13) do (
    if not defined PYEXE (
        py -%%V --version >nul 2>&1 && set "PYEXE=py -%%V"
    )
)
if not defined PYEXE (
    echo [ERROR] Need Python 3.11/3.12/3.13 for PyTorch.
    echo         Install Python 3.11 from python.org, then re-run.
    goto :end
)
echo Using: %PYEXE%
%PYEXE% --version
echo.

REM --- create v3's own training venv ---
set "VENVPY=.venv-train\Scripts\python.exe"
if not exist "%VENVPY%" (
    echo [Creating v3 training venv .venv-train ...]
    %PYEXE% -m venv .venv-train
    if errorlevel 1 ( echo [ERROR] venv creation failed. & goto :end )
)

REM --- call venv python by full path; do NOT rely on activate.bat ---
"%VENVPY%" --version
"%VENVPY%" -m pip install --upgrade pip

REM --- install CUDA 12.8 PyTorch for RTX 50-series (sm_120) ---
"%VENVPY%" -c "import torch,sys;sys.exit(0 if torch.cuda.is_available() else 1)" 2>nul
if errorlevel 1 (
    echo [Installing cu128 PyTorch for RTX 5070 ...]
    "%VENVPY%" -m pip install torch torchvision --index-url https://download.pytorch.org/whl/cu128
    if errorlevel 1 ( echo [ERROR] torch install failed. & goto :end )
)

REM --- install ultralytics ---
"%VENVPY%" -c "import ultralytics" 2>nul || "%VENVPY%" -m pip install ultralytics
if errorlevel 1 ( echo [ERROR] ultralytics install failed. & goto :end )

REM --- train (2 classes pothole/dog, see ml\data.yaml) ---
"%VENVPY%" train.py --imgsz 640 --epochs 100 --name safeway_v3_pothole_dog_640 --project "%~dp0runs" --skip-env-check

echo.
echo [DONE] Results are under runs\safeway_v3_pothole_dog_640

:end
echo.
echo ------------------------------------------------------------
echo Press any key to close this window...
pause >nul
