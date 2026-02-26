@echo off
echo ============================================
echo   XiaoZhi ESP32 Full Flash Script
echo ============================================
echo.

:: Check port parameter
if "%1"=="" (
    echo Usage: flash_all.bat COM_PORT
    echo Example: flash_all.bat COM3
    echo.
    echo Available ports:
    mode
    pause
    exit /b 1
)
set PORT=%1
echo Using port: %PORT%
echo.

:: Activate ESP-IDF environment
if exist "C:\Espressif\frameworks\esp-idf-v5.4\export.bat" (
    call "C:\Espressif\frameworks\esp-idf-v5.4\export.bat"
) else if exist "C:\Espressif\frameworks\esp-idf-v5.3.2\export.bat" (
    call "C:\Espressif\frameworks\esp-idf-v5.3.2\export.bat"
) else if exist "%IDF_PATH%\export.bat" (
    call "%IDF_PATH%\export.bat"
) else (
    echo Warning: ESP-IDF not found, make sure you run this in ESP-IDF terminal
)
echo.

:: Step 1: Build firmware
echo [1/3] Building firmware...
call idf.py build
if errorlevel 1 (
    echo Error: Build failed
    pause
    exit /b 1
)
echo.

:: Step 2: Erase and flash firmware
echo [2/3] Erasing and flashing firmware...
call idf.py -p %PORT% erase-flash
if errorlevel 1 (
    echo Error: Erase failed
    pause
    exit /b 1
)
call idf.py -p %PORT% flash
if errorlevel 1 (
    echo Error: Flash failed
    pause
    exit /b 1
)
echo.

:: Step 3: Flash WiFi config and animation resources
echo [3/3] Flashing resources...

:: Check WiFi config
if exist "main\images\shared\wifi_config.bin" (
    echo   WiFi config: main\images\shared\wifi_config.bin
    set WIFI_FLASH=0x7F0000 main/images/shared/wifi_config.bin
) else (
    echo   WiFi config: not found, skipping
    set WIFI_FLASH=
)

:: Check animation resources
if exist "gifs\assets.bin" (
    echo   Animation: gifs\assets.bin
    set ANIM_FLASH=0x800000 gifs/assets.bin
) else if exist "gifs\frames.bin" (
    echo   Animation: gifs\frames.bin
    set ANIM_FLASH=0x800000 gifs/frames.bin
) else (
    echo Error: No animation resource found in gifs/
    pause
    exit /b 1
)

python -m esptool -p %PORT% write_flash %WIFI_FLASH% %ANIM_FLASH%
if errorlevel 1 (
    echo Error: Resource flash failed
    pause
    exit /b 1
)

echo.
echo ============================================
echo   Flash Complete!
echo ============================================
echo.
pause
