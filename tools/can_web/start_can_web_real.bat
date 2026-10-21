@echo off
rem ===========================================================================
rem  start_can_web_real.bat -- CAN debug console on a real PEAK PCAN-USB
rem    needs:  pip install "python-can[pcan]"  +  PEAK driver installed
rem    edit CHANNEL / BITRATE / PORT below if needed
rem ===========================================================================
setlocal
cd /d "%~dp0"

set CHANNEL=PCAN_USBBUS1
set BITRATE=500000
set PORT=8765

where python >nul 2>nul
if errorlevel 1 (
  echo [ERROR] "python" not found in PATH. Install Python 3.7+ first.
  pause
  exit /b 1
)

echo ============================================================
echo   RK3506B car CAN console  --  REAL PCAN-USB
echo   channel=%CHANNEL%  bitrate=%BITRATE%  port=%PORT%
echo   ^(bitrate MUST match the board: 500000^)
echo   Close the bridge window to stop.
echo ============================================================
echo.

start "can_bridge (PCAN)" cmd /k "chcp 65001 >nul & python can_bridge.py --channel %CHANNEL% --bitrate %BITRATE% --port %PORT%"
timeout /t 2 /nobreak >nul
start "" http://127.0.0.1:%PORT%/

echo Bridge started in a separate window. Browser opening...
echo If the bridge window shows an error, check: PEAK driver, USB cable,
echo channel name, and that no other program (PCAN-View) holds the device.
echo.
pause
endlocal
