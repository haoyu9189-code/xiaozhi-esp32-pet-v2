#!/usr/bin/env python3
"""
生成发布包 - 只包含二进制固件和必要工具，不含源码
运行: python scripts/make_release.py
"""

import os
import sys
import shutil
from pathlib import Path

# 项目根目录
PROJECT_ROOT = Path(__file__).parent.parent

# 发布目录
RELEASE_DIR = PROJECT_ROOT / "release"

# 需要复制的固件文件
FIRMWARE_FILES = [
    ("build/bootloader/bootloader.bin", "firmware/bootloader.bin"),
    ("build/partition_table/partition-table.bin", "firmware/partition-table.bin"),
    ("build/xiaozhi.bin", "firmware/xiaozhi.bin"),
    ("build/ota_data_initial.bin", "firmware/ota_data_initial.bin"),
]

# 需要复制的工具脚本
TOOL_FILES = [
    ("scripts/png_to_assets_dedup.py", "tools/png_to_assets_dedup.py"),
]


def clean_release_dir():
    """清理旧的发布目录"""
    if RELEASE_DIR.exists():
        shutil.rmtree(RELEASE_DIR)
    RELEASE_DIR.mkdir(parents=True)


def copy_firmware():
    """复制固件文件"""
    print("\n[固件] 复制编译产物...")
    firmware_dir = RELEASE_DIR / "firmware"
    firmware_dir.mkdir(exist_ok=True)

    for src, dst in FIRMWARE_FILES:
        src_path = PROJECT_ROOT / src
        dst_path = RELEASE_DIR / dst

        if not src_path.exists():
            print(f"  警告: {src} 不存在，请先运行 idf.py build")
            continue

        shutil.copy2(src_path, dst_path)
        print(f"  复制: {src} -> {dst}")


def copy_tools():
    """复制工具脚本"""
    print("\n[工具] 复制脚本...")
    tools_dir = RELEASE_DIR / "tools"
    tools_dir.mkdir(exist_ok=True)

    for src, dst in TOOL_FILES:
        src_path = PROJECT_ROOT / src
        dst_path = RELEASE_DIR / dst

        if src_path.exists():
            shutil.copy2(src_path, dst_path)
            print(f"  复制: {src}")


def create_directories():
    """创建必要的目录结构"""
    print("\n[目录] 创建目录结构...")

    dirs = [
        "gifs/dynamic1",
        "gifs/dynamic2",
        "gifs/dynamic3",
        "gifs/static1",
        "wifi",
        "output",
    ]

    for d in dirs:
        (RELEASE_DIR / d).mkdir(parents=True, exist_ok=True)
        print(f"  创建: {d}/")


def create_flash_script():
    """创建一键烧录脚本"""
    print("\n[脚本] 生成烧录脚本...")

    # Windows 批处理
    bat_content = '''@echo off
chcp 65001 >nul
echo ========================================
echo   小智 ESP32-C6 固件烧录工具
echo ========================================
echo.

set PORT=COM3
if not "%1"=="" set PORT=%1

echo 使用串口: %PORT%
echo.

echo [1/3] 擦除 Flash...
esptool.py --chip esp32c6 --port %PORT% erase_flash
if errorlevel 1 (
    echo 错误: 擦除失败
    pause
    exit /b 1
)

echo.
echo [2/3] 烧录固件...
esptool.py --chip esp32c6 --port %PORT% --baud 460800 write_flash ^
    0x0 firmware/bootloader.bin ^
    0x8000 firmware/partition-table.bin ^
    0xd000 firmware/ota_data_initial.bin ^
    0x10000 firmware/xiaozhi.bin
if errorlevel 1 (
    echo 错误: 固件烧录失败
    pause
    exit /b 1
)

echo.
echo [3/3] 烧录动画资源...
if exist "output\\animation_frames.bin" (
    esptool.py --chip esp32c6 --port %PORT% write_flash ^
        0x7F0000 output/wifi_config.bin ^
        0x800000 output/animation_frames.bin
    if errorlevel 1 (
        echo 警告: 资源烧录失败，请检查文件是否存在
    )
) else (
    echo 跳过: output/animation_frames.bin 不存在
    echo 请先运行 generate_assets.bat 生成动画资源
)

echo.
echo ========================================
echo   烧录完成！
echo ========================================
pause
'''

    (RELEASE_DIR / "flash.bat").write_text(bat_content, encoding='utf-8')
    print("  生成: flash.bat")

    # 生成资源脚本
    gen_content = '''@echo off
chcp 65001 >nul
echo ========================================
echo   生成动画资源
echo ========================================
echo.

echo 请确保已将 GIF 帧图片放入以下目录:
echo   gifs/dynamic1/  - 说话动画
echo   gifs/dynamic2/  - 聆听动画
echo   gifs/dynamic3/  - 触摸动画
echo   gifs/static1/   - 待机动画
echo.

python tools/png_to_assets_dedup.py 0 8

echo.
echo 移动生成的文件到 output 目录...
if not exist "output" mkdir output

if exist "main\\images\\shared\\animation_frames.bin" (
    move /Y main\\images\\shared\\animation_frames.bin output\\
    echo   移动: animation_frames.bin
)
if exist "main\\images\\shared\\wifi_config.bin" (
    move /Y main\\images\\shared\\wifi_config.bin output\\
    echo   移动: wifi_config.bin
)

echo.
echo 完成！生成的文件在 output/ 目录
echo 运行 flash.bat 进行烧录
pause
'''

    (RELEASE_DIR / "generate_assets.bat").write_text(gen_content, encoding='utf-8')
    print("  生成: generate_assets.bat")


def create_wifi_template():
    """创建 WiFi 配置模板"""
    print("\n[配置] 创建 WiFi 模板...")

    wifi_content = '''你的WiFi名称
你的WiFi密码
'''
    (RELEASE_DIR / "wifi" / "wifi.txt").write_text(wifi_content, encoding='utf-8')
    print("  生成: wifi/wifi.txt (模板)")


def create_readme():
    """创建说明文档"""
    print("\n[文档] 生成 README...")

    readme = '''# 小智 ESP32-C6 固件发布包

## 目录结构

```
release/
├── firmware/           # 预编译固件（不要修改）
│   ├── bootloader.bin
│   ├── partition-table.bin
│   ├── ota_data_initial.bin
│   └── xiaozhi.bin
├── tools/              # 工具脚本
│   └── png_to_assets_dedup.py
├── gifs/               # 放你的 GIF 帧图片
│   ├── dynamic1/       # 说话动画
│   ├── dynamic2/       # 聆听动画
│   ├── dynamic3/       # 触摸动画
│   └── static1/        # 待机动画
├── wifi/
│   └── wifi.txt        # WiFi 配置
├── output/             # 生成的资源文件
├── flash.bat           # 一键烧录
├── generate_assets.bat # 生成动画资源
└── README.txt
```

## 快速开始

### 1. 安装依赖

```
pip install Pillow numpy esptool
```

### 2. 准备动画

将 GIF 帧图片（PNG 格式，280x240 像素）放入对应目录：
- gifs/dynamic1/ - 说话时的动画
- gifs/dynamic2/ - 聆听时的动画
- gifs/dynamic3/ - 触摸时的动画
- gifs/static1/  - 待机时的动画

文件命名格式: xxx_1.png, xxx_2.png, ... 或 frame_001.png, frame_002.png, ...

### 3. 配置 WiFi（可选）

编辑 wifi/wifi.txt：
```
你的WiFi名称
你的WiFi密码
```

### 4. 生成资源

双击运行 `generate_assets.bat`

### 5. 烧录

1. 连接设备到电脑
2. 双击运行 `flash.bat`
3. 如果串口不是 COM3，运行: `flash.bat COM端口号`

## 仅更换动画

已烧录过固件的设备，只需更新动画：

```batch
# 1. 更换 gifs/ 目录中的图片
# 2. 运行生成脚本
generate_assets.bat

# 3. 只烧录资源（不需要重烧固件）
esptool.py --chip esp32c6 --port COM3 write_flash 0x800000 output/animation_frames.bin
```

## 仅更换 WiFi

```batch
# 1. 修改 wifi/wifi.txt
# 2. 运行生成脚本
generate_assets.bat

# 3. 清除 NVS + 烧录 WiFi 配置
esptool.py --chip esp32c6 --port COM3 erase_region 0x7E9000 0x6000
esptool.py --chip esp32c6 --port COM3 write_flash 0x7F0000 output/wifi_config.bin
```

## 注意事项

- 首次烧录需要完整执行 flash.bat
- 更换动画不需要重新烧录固件
- 设备需要 16MB Flash
- 图片尺寸必须是 280x240 像素
'''

    (RELEASE_DIR / "README.txt").write_text(readme, encoding='utf-8')
    print("  生成: README.txt")


def main():
    print("=" * 50)
    print("  生成发布包")
    print("=" * 50)

    # 检查是否已编译
    xiaozhi_bin = PROJECT_ROOT / "build" / "xiaozhi.bin"
    if not xiaozhi_bin.exists():
        print("\n错误: 请先编译项目")
        print("运行: idf.py build")
        sys.exit(1)

    clean_release_dir()
    copy_firmware()
    copy_tools()
    create_directories()
    create_flash_script()
    create_wifi_template()
    create_readme()

    print("\n" + "=" * 50)
    print(f"  发布包已生成: {RELEASE_DIR}")
    print("=" * 50)
    print("\n可以将 release/ 目录打包发给合作者")


if __name__ == "__main__":
    main()
