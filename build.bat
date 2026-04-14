@echo off
echo ============================================
echo  Soil Monitor - Build .exe
echo ============================================
echo.

echo [1/2] Installing / upgrading PyInstaller...
pip install --quiet --upgrade pyinstaller
if errorlevel 1 (
    echo ERROR: pip failed. Make sure Python is installed and on your PATH.
    pause
    exit /b 1
)

echo [2/2] Building SoilMonitor.exe...
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
