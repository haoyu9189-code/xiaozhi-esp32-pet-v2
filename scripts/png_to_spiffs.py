#!/usr/bin/env python3
"""
PNG 图片序列转 RGB565 二进制文件 - SPIFFS存储版本
生成的文件可以直接烧录到SPIFFS分区，运行时按需读取

使用方法:
    python png_to_spiffs.py
"""

import os
import sys
import json
from pathlib import Path

try:
    from PIL import Image
except ImportError:
    print("错误: 请先安装 Pillow")
    print("运行: pip install Pillow")
    sys.exit(1)


# 配置
ANIMATIONS = [
    ("gifs/dynamic1", "dynamic1"),
    ("gifs/dynamic2", "dynamic2"),
    ("gifs/static1",  "static1"),
    ("gifs/static2",  "static2"),
    ("gifs/dynamic3", "dynamic3"),
    ("gifs/dynamic4", "dynamic4"),
]

OUTPUT_DIR = "assets/animations"


def rgb888_to_rgb565(r, g, b):
    """RGB888 转 RGB565 (16位色)"""
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)


def convert_image_to_binary(input_path, output_path):
    """将PNG图片转换为RGB565二进制文件"""
    img = Image.open(input_path).convert('RGB')
    width, height = img.size

    data = bytearray()
    for y in range(height):
        for x in range(width):
            r, g, b = img.getpixel((x, y))
            pixel = rgb888_to_rgb565(r, g, b)
            # 小端序
            data.append(pixel & 0xFF)
            data.append((pixel >> 8) & 0xFF)

    with open(output_path, 'wb') as f:
        f.write(data)

    return len(data), width, height


def main():
    print("=" * 60)
    print("PNG 序列转 RGB565 二进制 - SPIFFS存储版本")
    print("=" * 60)

    # 创建输出目录
    output_path = Path(OUTPUT_DIR)
    output_path.mkdir(parents=True, exist_ok=True)

    # 动画索引信息
    animation_index = {
        "version": 1,
        "animations": {}
    }

    total_size = 0
    total_frames = 0

    for input_dir, anim_name in ANIMATIONS:
        input_path = Path(input_dir)
        if not input_path.exists():
            print(f"跳过: 目录不存在 - {input_dir}")
            continue

        # 创建动画子目录
        anim_output = output_path / anim_name
        anim_output.mkdir(parents=True, exist_ok=True)

        # 获取PNG文件
        png_files = sorted(input_path.glob('*.png')) + sorted(input_path.glob('*.PNG'))
        png_files = sorted(set(png_files))

        if not png_files:
            print(f"跳过: 无PNG文件 - {input_dir}")
            continue

        print(f"\n处理: {anim_name} ({len(png_files)} 帧)")

        anim_info = {
            "frame_count": len(png_files),
            "width": 0,
            "height": 0,
            "frame_size": 0,
            "files": []
        }

        anim_size = 0
        for i, png_file in enumerate(png_files, 1):
            # 输出文件名: frame_01.bin, frame_02.bin, ...
            bin_filename = f"frame_{i:02d}.bin"
            bin_path = anim_output / bin_filename

            size, width, height = convert_image_to_binary(str(png_file), str(bin_path))
            anim_size += size

            anim_info["width"] = width
            anim_info["height"] = height
            anim_info["frame_size"] = size
            anim_info["files"].append(bin_filename)

            print(f"  {png_file.name} -> {bin_filename} ({size:,} bytes)")

        animation_index["animations"][anim_name] = anim_info
        total_size += anim_size
        total_frames += len(png_files)

        print(f"  小计: {anim_size:,} bytes ({anim_size/1024/1024:.2f} MB)")

    # 保存索引文件
    index_path = output_path / "index.json"
    with open(index_path, 'w', encoding='utf-8') as f:
        json.dump(animation_index, f, indent=2)

    print("\n" + "=" * 60)
    print("转换完成!")
    print("=" * 60)
    print(f"  总帧数: {total_frames}")
    print(f"  总大小: {total_size:,} bytes ({total_size/1024/1024:.2f} MB)")
    print(f"  输出目录: {output_path}")
    print(f"  索引文件: {index_path}")
    print("\n下一步: 运行 'idf.py build' 然后 'idf.py flash'")


if __name__ == '__main__':
    main()
