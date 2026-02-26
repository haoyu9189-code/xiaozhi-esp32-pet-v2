@echo off
chcp 65001 >nul
setlocal enabledelayedexpansion

echo ============================================
echo   XiaoZhi ESP32 Assets Flash Script
echo   动画 + 背景 + 物品资源烧录
echo ============================================
echo.
echo ============================================
echo   手动烧录命令 (推荐):
echo ============================================
echo.
echo   步骤1: 合并资源文件 (CMD中运行)
echo      copy /b gifs\frames.bin + gifs\backgrounds.bin + gifs\items\frames.bin gifs\assets.bin
echo.
echo   步骤2: 烧录到设备 (CMD或PowerShell)
echo      python -m esptool --chip esp32c6 --port COMx --baud 921600 write_flash 0x800000 gifs\assets.bin
echo.
echo   注意: copy /b 必须在CMD中运行，不能在PowerShell中直接运行
echo.
echo ============================================
echo.

cd /d "%~dp0"

:: Check port parameter
if "%1"=="" (
    echo 用法: flash_assets.bat COM端口
    echo 示例: flash_assets.bat COM3
    echo.
    pause
    exit /b 1
)
set PORT=%1
echo 使用串口: %PORT%
echo.

:: Activate ESP-IDF environment (try multiple versions)
if exist "%IDF_PATH%\export.bat" (
    call "%IDF_PATH%\export.bat" >nul 2>&1
) else if exist "C:\Espressif\frameworks\esp-idf-v5.4\export.bat" (
    call "C:\Espressif\frameworks\esp-idf-v5.4\export.bat" >nul 2>&1
) else if exist "C:\Espressif\frameworks\esp-idf-v5.3.2\export.bat" (
    call "C:\Espressif\frameworks\esp-idf-v5.3.2\export.bat" >nul 2>&1
)

:: Step 0: Update background color range from index.json
echo [0/4] 更新背景色范围 (从 index.json 读取)...
echo.
python "%~dp0update_bg_color.py"
if errorlevel 1 (
    echo [警告] 背景色更新失败，使用默认值
) else (
    echo.
    echo   [重要] 背景色范围已更新，需要重新编译固件:
    echo          idf.py build
    echo          idf.py -p %PORT% flash
    echo.
)
echo.

:: Step 1: Check source files
echo [1/4] 检查资源文件...
echo.

:: Debug: show current directory
echo   当前目录: %CD%

set ANIM_FILE=gifs\frames.bin
set BG_FILE=gifs\backgrounds.bin
set ITEMS_FILE=gifs\items\frames.bin
set COMBINED_FILE=gifs\assets_combined.bin

:: Animation frames (required)
if not exist "%ANIM_FILE%" (
    echo.
    echo [错误] 找不到动画资源文件: %ANIM_FILE%
    echo   完整路径: %CD%\%ANIM_FILE%
    echo.
    echo 动画文件格式:
    echo   - 无头 RGB888 索引格式 每帧 26368 字节
    echo   - 104 帧动画 8组 x 13帧
    echo   - 尺寸 160x160 像素
    echo.
    pause
    exit /b 1
)

:: Display animation file info
echo   动画: %ANIM_FILE%
for %%A in ("%ANIM_FILE%") do (
    set ANIM_SIZE=%%~zA
    echo   大小: !ANIM_SIZE! 字节
    set /a FRAME_SIZE=26368
    set /a FRAMES=!ANIM_SIZE! / !FRAME_SIZE!
    echo   帧数: !FRAMES! 帧 (每帧 !FRAME_SIZE! 字节)
)
echo.

:: Background (optional but recommended)
if not exist "%BG_FILE%" (
    echo [提示] 未找到背景文件: %BG_FILE%
    echo        将只烧录动画，背景显示为黑色
    echo.
    echo 背景文件格式:
    echo   - 无头 RGB888 索引格式 (每帧 67968 字节)
    echo   - 16 张背景 (280x240 全屏)
    echo.
    set HAS_BG=0
) else (
    echo   背景: %BG_FILE%
    for %%A in ("%BG_FILE%") do (
        set BG_SIZE=%%~zA
        echo   大小: !BG_SIZE! 字节
    )
    echo.
    set HAS_BG=1
)

:: Items (optional - coin and poop sprites)
if not exist "%ITEMS_FILE%" (
    echo [提示] 未找到物品文件: %ITEMS_FILE%
    echo        将不显示场景金币和便便
    echo.
    echo 物品文件格式:
    echo   - 无头 RGB888 索引格式 (每帧 2365 字节, 255色调色板)
    echo   - 2 个物品 (40x40: 金币, 便便)
    echo.
    set HAS_ITEMS=0
) else (
    echo   物品: %ITEMS_FILE%
    for %%A in ("%ITEMS_FILE%") do (
        set ITEMS_SIZE=%%~zA
        echo   大小: !ITEMS_SIZE! 字节
    )
    echo.
    set HAS_ITEMS=1
)

:: Step 2: Combine files
echo [2/4] 合并资源文件...
echo.

:: Build combined file based on what's available
if "!HAS_BG!"=="1" (
    if "!HAS_ITEMS!"=="1" (
        echo   合并: %ANIM_FILE% + %BG_FILE% + %ITEMS_FILE%
        echo   输出: %COMBINED_FILE%
        copy /b "%ANIM_FILE%" + "%BG_FILE%" + "%ITEMS_FILE%" "%COMBINED_FILE%" >nul
    ) else (
        echo   合并: %ANIM_FILE% + %BG_FILE%
        echo   输出: %COMBINED_FILE%
        copy /b "%ANIM_FILE%" + "%BG_FILE%" "%COMBINED_FILE%" >nul
    )
    if errorlevel 1 (
        echo [错误] 文件合并失败
        pause
        exit /b 1
    )

    for %%A in ("%COMBINED_FILE%") do (
        set COMBINED_SIZE=%%~zA
        echo   合并大小: !COMBINED_SIZE! 字节
    )
    echo.
    set FLASH_FILE=%COMBINED_FILE%
) else (
    echo   无背景文件，直接使用动画文件
    set FLASH_FILE=%ANIM_FILE%
)

:: Step 3: Flash to device
echo [3/4] 烧录到设备...
echo   地址: 0x800000 (assets 分区)
echo   文件: !FLASH_FILE!
echo.

python -m esptool --chip esp32c6 --port %PORT% --baud 921600 write_flash 0x800000 "!FLASH_FILE!"
if errorlevel 1 (
    echo.
    echo [错误] 烧录失败，请检查:
    echo   1. 设备是否连接
    echo   2. 串口是否正确
    echo   3. 是否安装 esptool.py
    pause
    exit /b 1
)

echo.
echo ============================================
echo   烧录完成!
echo ============================================
echo.
echo 数据布局 (地址 0x800000):
echo   - 动画数据: 偏移 0, 104帧 x 26368 = 2,742,272 字节
if "!HAS_BG!"=="1" (
    echo   - 背景数据: 偏移 2,742,272 (280x240, 16张 x 67968 = 1,087,488 字节)
)
if "!HAS_ITEMS!"=="1" (
    echo   - 物品数据: 偏移 = 动画+背景 (40x40, 2个 x 2365 = 4,730 字节)
)
echo.
pause
