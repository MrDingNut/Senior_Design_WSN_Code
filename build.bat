@echo off
echo ============================================
echo  Soil Monitor - Build .exe
echo ============================================
echo.

echo [1/3] Checking / installing required Python libraries...
pip install --quiet --upgrade pyinstaller pyserial matplotlib
if errorlevel 1 (
    echo ERROR: pip failed. Make sure Python is installed and on your PATH.
    pause
    exit /b 1
)

echo [2/3] Libraries ready.
echo.
echo [3/3] Building SoilMonitor.exe...
pyinstaller --onefile ^
            --name "SoilMonitor" ^
            --hidden-import serial.tools.list_ports ^
            --hidden-import tkinter.messagebox ^
            --hidden-import matplotlib.backends.backend_tkagg ^
            --collect-all matplotlib ^
            base_station.py

if errorlevel 1 (
    echo.
    echo ERROR: Build failed. See output above for details.
    pause
    exit /b 1
)

echo.
echo ============================================
echo  Done! SoilMonitor.exe is in the dist\ folder.
echo ============================================
pause
