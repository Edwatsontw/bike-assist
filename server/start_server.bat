@echo off
cd /d "%~dp0"
title SafeWay 伺服器

rem -- 1. 檢查 Python（Microsoft Store 的假 python 也會被擋下來）--
python -c "import sys" >nul 2>nul
if errorlevel 1 (
  echo [安裝] 這台電腦還沒有 Python，正在用 winget 安裝 Python 3.12 ...
  winget install -e --id Python.Python.3.12 --accept-package-agreements --accept-source-agreements
  echo.
  echo 安裝完成。請「關掉這個視窗」，再雙擊一次 start_server.bat。
  pause
  exit /b
)

rem -- 2. 第一次執行：建立虛擬環境並安裝套件（之後會直接略過）--
if not exist ".venv\Scripts\python.exe" (
  echo [準備] 第一次啟動，建立 Python 環境中 ...
  python -m venv .venv
)
".venv\Scripts\python.exe" -m pip install -q --disable-pip-version-check -r requirements.txt
if errorlevel 1 (
  echo [錯誤] 套件安裝失敗，請確認這台電腦有連上網路後再試一次。
  pause
  exit /b
)

rem -- 3. 防火牆：允許車機與手機連進 8000 埠（需要系統管理員，只要做一次）--
netsh advfirewall firewall show rule name="SafeWay 8000" >nul 2>nul
if errorlevel 1 (
  netsh advfirewall firewall add rule name="SafeWay 8000" dir=in action=allow protocol=TCP localport=8000 profile=any >nul 2>nul
  if errorlevel 1 (
    echo [注意] 還沒開防火牆。請關掉視窗，對 start_server.bat 按右鍵
    echo        「以系統管理員身分執行」一次，之後就可以正常雙擊。
    echo.
  ) else (
    echo [完成] 已開啟防火牆 8000 埠。
  )
)

rem -- 4. 啟動伺服器（資料庫與影像都放在這個資料夾裡）--
if not exist data mkdir data
if not exist uploads mkdir uploads
set "DB_PATH=%~dp0data\bike_data.db"
set "UPLOAD_DIR=%~dp0uploads"

echo.
echo ================================================
echo   SafeWay 伺服器啟動中
echo   系統端畫面： http://localhost:8000
echo   車機/手機請連這台筆電的熱點（EdwatsonPC）
echo   APP 緊急回報網址： http://192.168.137.1:8000/api/fallen
echo   要關閉伺服器：直接關掉這個視窗
echo ================================================
echo.
start "" cmd /c "timeout /t 4 >nul & start http://localhost:8000"
".venv\Scripts\python.exe" -m uvicorn main:app --host 0.0.0.0 --port 8000
pause
