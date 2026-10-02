@echo off
cd /d "%~dp0"
title SafeWay 模擬車機（展場備案）

rem -- 備案：車機無法使用時，用這台筆電模擬車機。--
rem    會開兩個視窗：伺服器（start_server.bat）與模擬車機（fake_bike.py）。

rem 防火牆：讓手機連得到模擬車機的 80 / 81 埠（需系統管理員，只要做一次）
netsh advfirewall firewall show rule name="SafeWay demo 80-81" >nul 2>nul
if errorlevel 1 (
  netsh advfirewall firewall add rule name="SafeWay demo 80-81" dir=in action=allow protocol=TCP localport=80,81,8080 profile=any >nul 2>nul
  if errorlevel 1 (
    echo [注意] 還沒開防火牆。請對 demo_mode.bat 按右鍵「以系統管理員身分執行」一次。
    echo.
  )
)

start "SafeWay 伺服器" cmd /c start_server.bat
echo 等待伺服器啟動 ...
timeout /t 8 >nul
if exist ".venv\Scripts\python.exe" (
  ".venv\Scripts\python.exe" fake_bike.py
) else (
  python fake_bike.py
)
pause
