@echo off
rem ===========================================================================
rem  start_can_web_pcanbasic.bat -- CAN console using PEAK's PCANBasic.py only
rem
rem  Use this when can_bridge.py (python-can) fails with:
rem      module 'can.interfaces' has no attribute 'pcan'
rem  i.e. python-can's pcan submodule could not load PCANBasic.dll at import time.
rem  This bridge talks to the PEAK driver directly through its own PCANBasic.py.
rem
rem  Needs: PEAK PCAN-Basic driver installed  ->  C:\Windows\System32\PCANBasic.dll
rem         check with:  python -c "import PCANBasic"
rem  Edit CHANNEL / BITRATE / PORT below if needed.
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
echo   RK3506B car CAN console  --  PEAK PCANBasic (no python-can)
echo   channel=%CHANNEL%  bitrate=%BITRATE%  port=%PORT%
echo   ^(bitrate MUST match the board: 500000^)
echo   Close the bridge window to stop.
echo ============================================================
echo.
echo Pre-check ^(should print PCANBasic OK ...^):
python -c "import PCANBasic; print('PCANBasic OK', PCANBasic.PCAN_USBBUS1)" 2>nul
if errorlevel 1 (
  echo   [WARN] "import PCANBasic" failed. The PEAK PCAN-Basic driver/py-wrapper
  echo          may not be fully installed. Check:  dir C:\Windows\System32\PCANBasic.dll
  echo          This bridge will also auto-search the usual PEAK install folders.
  echo.
)

start "can_bridge (PCANBasic)" cmd /k "chcp 65001 >nul & python pcanbasic_bridge.py --channel %CHANNEL% --bitrate %BITRATE% --port %PORT%"
timeout /t 2 /nobreak >nul
start "" http://127.0.0.1:%PORT%/

echo Bridge started in a separate window. Browser opening...
echo If the page does not open, visit: http://127.0.0.1:%PORT%/
echo Page top bar "backend" should read:  PCANBasic (PEAK driver)
echo.
pause
endlocal
