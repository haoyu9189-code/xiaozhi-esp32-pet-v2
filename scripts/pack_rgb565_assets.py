#!/usr/bin/env python3
"""
PNG 图片序列打包为 RGB565 二进制文件工具
用于将多组 PNG 动画打包为单个 animation_frames.bin 文件

文件格式 (RGB565AnimationHeader):
  magic[4]: "RGB5"
  version: uint16 (1)
  frame_count: uint16 (总帧数)
  width: uint16 (140)
  height: uint16 (120)
  anim_counts[4]: 4 x uint16 (每个动画的帧数)

  帧数据: frame_count × (width × height × 2) bytes RGB565

使用方法:
    python pack_rgb565_assets.py

输入文件格式 (gifs目录下):
    Default_dynamic1_1.png, Default_dynamic1_2.png, ...  - Speaking 动画帧
    Default_dynamic2_1.png, Default_dynamic2_2.png, ...  - Listening 动画帧
    Default_static1_1.png, Default_static1_2.png, ...    - Idle/Breathing 动画帧
    Default_dynamic3_1.png, Default_dynamic3_2.png, ...  - Center touch 动画帧

输出:
    main/images/shared/animation_frames.bin
"""

import os
import sys
import struct
import re
from pathlib import Path

try:
    from PIL import Image
except ImportError:
    print("错误: 请先安装 Pillow")
    print("运行: pip install Pillow")
    sys.exit(1)


# 目标尺寸
TARGET_WIDTH = 140
TARGET_HEIGHT = 120

# 4种动画配置 (对应 AnimLoaderType)
# 格式: (pattern_prefix, name)
ANIMATIONS = [
    ("dynamic1", "dynamic1"),  # ANIM_DYNAMIC1 - Speaking
    ("dynamic2", "dynamic2"),  # ANIM_DYNAMIC2 - Listening
    ("static1",  "static1"),   # ANIM_STATIC1  - Idle/Breathing
    ("dynamic3", "dynamic3"),  # ANIM_DYNAMIC3 - Center touch
]

GIFS_DIR = "gifs"
OUTPUT_PATH = "main/images/shared/animation_frames.bin"


def rgb888_to_rgb565(r, g, b):
    """RGB888 转 RGB565 (16位色)"""
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)


def load_png_frames_by_pattern(gifs_dir, pattern_prefix):
    """根据命名模式加载PNG帧 (如 Default_dynamic1_1.png)"""
    gifs_path = Path(gifs_dir)

    if not gifs_path.exists():
        print(f"  警告: 目录不存在 - {gifs_dir}")
        return []

    # 匹配 Default_{pattern}_N.png 或 {pattern}_N.png 格式
    pattern = re.compile(rf'(?:Default_)?{pattern_prefix}_(\d+)\.png$', re.IGNORECASE)

    # 找到所有匹配的文件
    matched_files = []
    for f in gifs_path.iterdir():
        match = pattern.match(f.name)
        if match:
            frame_num = int(match.group(1))
            matched_files.append((frame_num, f))

    if not matched_files:
        print(f"  警告: 未找到匹配 {pattern_prefix} 的PNG文件")
        return []

    # 按帧号排序
    matched_files.sort(key=lambda x: x[0])

    frames = []
    for frame_num, png_file in matched_files:
        img = Image.open(png_file).convert('RGB')

        # 如果尺寸不对，进行缩放
        if img.size != (TARGET_WIDTH, TARGET_HEIGHT):
            img = img.resize((TARGET_WIDTH, TARGET_HEIGHT), Image.Resampling.LANCZOS)

        frames.append(img)

    print(f"  加载 {len(frames)} 帧 ({pattern_prefix})")
    return frames


def convert_frame_to_rgb565(img):
    """将PIL图像转换为RGB565字节数组"""
    width, height = img.size
    data = bytearray()

    for y in range(height):
        for x in range(width):
            r, g, b = img.getpixel((x, y))
            pixel = rgb888_to_rgb565(r, g, b)

            # 小端序 (LSB first)
            data.append(pixel & 0xFF)
            data.append((pixel >> 8) & 0xFF)

    return bytes(data)


def pack_animations():
    """打包所有动画为单个bin文件"""
    print("PNG 序列打包为 RGB565 二进制文件")
    print("=" * 50)

    # 加载所有动画
    all_frames = []
    anim_counts = []

    for pattern_prefix, name in ANIMATIONS:
        print(f"\n处理 {name}:")
        frames = load_png_frames_by_pattern(GIFS_DIR, pattern_prefix)
        anim_counts.append(len(frames))
        all_frames.extend(frames)

    total_frames = len(all_frames)

    if total_frames == 0:
        print("\n错误: 没有找到任何帧!")
        return False

    print(f"\n{'='*50}")
    print(f"总帧数: {total_frames}")
    print(f"各动画帧数: {anim_counts}")

    # 构建文件头 (RGB565AnimationHeader)
    # magic[4] + version(2) + frame_count(2) + width(2) + height(2) + anim_counts[4](8) = 20 bytes
    header = struct.pack('<4sHHHH4H',
        b'RGB5',           # magic
        1,                 # version
        total_frames,      # frame_count
        TARGET_WIDTH,      # width
        TARGET_HEIGHT,     # height
        *anim_counts       # anim_counts[4]
    )

    print(f"Header size: {len(header)} bytes")

    # 转换所有帧
    print("\n转换帧数据...")
    frame_size = TARGET_WIDTH * TARGET_HEIGHT * 2
    all_data = bytearray(header)

    for i, frame in enumerate(all_frames):
        if (i + 1) % 10 == 0 or i == total_frames - 1:
            print(f"  进度: {i+1}/{total_frames}")

        frame_data = convert_frame_to_rgb565(frame)
        all_data.extend(frame_data)

    # 确保输出目录存在
    output_path = Path(OUTPUT_PATH)
    output_path.parent.mkdir(parents=True, exist_ok=True)

    # 写入文件
    with open(output_path, 'wb') as f:
        f.write(all_data)

    file_size = len(all_data)
    print(f"\n{'='*50}")
    print(f"打包完成!")
    print(f"  输出文件: {output_path}")
    print(f"  文件大小: {file_size:,} bytes ({file_size/1024:.1f} KB)")
    print(f"  Header: {len(header)} bytes")
    print(f"  每帧大小: {frame_size:,} bytes ({frame_size/1024:.1f} KB)")
    print(f"  帧数据: {total_frames * frame_size:,} bytes ({total_frames * frame_size/1024/1024:.2f} MB)")

    # 烧录命令
    print(f"\n烧录命令:")
    print(f"  esptool.py write_flash 0x800000 {output_path}")

    return True


def main():
    success = pack_animations()
    sys.exit(0 if success else 1)


if __name__ == '__main__':
    main()
