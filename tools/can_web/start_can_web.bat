@echo off
rem ===========================================================================
rem  start_can_web.bat -- CAN debug console, SIMULATED frames (no PCAN hardware)
rem  Double-click this. Close the "can_bridge" window to stop.
rem ===========================================================================
setlocal
cd /d "%~dp0"

where python >nul 2>nul
if errorlevel 1 (
  echo [ERROR] "python" not found in PATH. Install Python 3.7+ first.
  pause
  exit /b 1
)

echo ============================================================
echo   RK3506B car CAN console  --  SIM mode (no PCAN hardware)
echo   Web UI : http://127.0.0.1:8765/
echo   Close the bridge window to stop.
echo ============================================================
echo.

start "can_bridge (sim)" cmd /k "chcp 65001 >nul & python can_bridge.py --sim --port 8765"
timeout /t 2 /nobreak >nul
start "" http://127.0.0.1:8765/

echo Bridge started in a separate window. Browser opening...
echo If the page does not open, visit: http://127.0.0.1:8765/
echo.
pause
endlocal
