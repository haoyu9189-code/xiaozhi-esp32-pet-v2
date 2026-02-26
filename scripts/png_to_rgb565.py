#!/usr/bin/env python3
"""
PNG 图片序列转 RGB565 C 头文件工具
用于将 GIF 拆帧后的 PNG 图片转换为 ESP32 LCD 可直接显示的 RGB565 格式

使用方法:
    python png_to_rgb565.py <输入目录> <输出目录> [前缀]
    python png_to_rgb565.py gifs main/images/christmas gImage_christmas
"""

import os
import sys
from pathlib import Path

try:
    from PIL import Image
except ImportError:
    print("错误: 请先安装 Pillow")
    print("运行: pip install Pillow")
    sys.exit(1)


def rgb888_to_rgb565(r, g, b):
    """RGB888 转 RGB565 (16位色)"""
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)


def convert_image(input_path, output_path, var_name):
    """将单张 PNG 图片转换为 RGB565 C 头文件"""
    img = Image.open(input_path).convert('RGB')
    width, height = img.size

    print(f"  转换: {os.path.basename(input_path)}")
    print(f"  尺寸: {width} x {height}")

    with open(output_path, 'w', encoding='utf-8') as f:
        f.write(f'// Auto-generated from {os.path.basename(input_path)}\n')
        f.write(f'// Size: {width} x {height}, RGB565 format (Little Endian for LVGL)\n')
        f.write(f'// Total bytes: {width * height * 2}\n\n')
        f.write(f'#ifndef _{var_name.upper()}_H_\n')
        f.write(f'#define _{var_name.upper()}_H_\n\n')
        f.write(f'#include <stdint.h>\n\n')
        f.write(f'static const uint8_t {var_name}[{width * height * 2}] = {{\n')

        pixels_per_line = 12
        pixel_count = 0

        for y in range(height):
            for x in range(width):
                r, g, b = img.getpixel((x, y))
                pixel = rgb888_to_rgb565(r, g, b)

                # 小端序 (LSB first) - LVGL会在发送到显示屏时交换字节
                low_byte = pixel & 0xFF
                high_byte = (pixel >> 8) & 0xFF

                if pixel_count % pixels_per_line == 0:
                    f.write('    ')

                f.write(f'0x{low_byte:02X},0x{high_byte:02X}, ')
                pixel_count += 1

                if pixel_count % pixels_per_line == 0:
                    f.write('\n')

        if pixel_count % pixels_per_line != 0:
            f.write('\n')

        f.write('};\n\n')
        f.write(f'#define {var_name.upper()}_WIDTH  {width}\n')
        f.write(f'#define {var_name.upper()}_HEIGHT {height}\n\n')
        f.write(f'#endif // _{var_name.upper()}_H_\n')

    file_size = width * height * 2
    print(f"  大小: {file_size:,} 字节 ({file_size/1024:.1f} KB)\n")
    return file_size, width, height


def batch_convert(input_dir, output_dir, prefix="gImage"):
    """批量转换目录下的所有 PNG 图片"""
    input_path = Path(input_dir)
    output_path = Path(output_dir)

    # 创建输出目录
    output_path.mkdir(parents=True, exist_ok=True)

    # 获取所有 PNG 文件并排序 (使用 set 去重，避免大小写重复)
    png_files = set(input_path.glob('*.png')) | set(input_path.glob('*.PNG'))
    png_files = sorted(png_files)

    if not png_files:
        print(f"错误: 在 {input_dir} 中未找到 PNG 文件")
        return False

    print(f"\n找到 {len(png_files)} 个 PNG 文件")
    print("=" * 50 + "\n")

    total_size = 0
    generated_files = []
    img_width = 0
    img_height = 0

    for i, png_file in enumerate(png_files, 1):
        # 生成变量名: gImage_christmas_0001, gImage_christmas_0002, ...
        var_name = f"{prefix}_{i:04d}"
        output_file = output_path / f"{var_name}.h"

        size, w, h = convert_image(str(png_file), str(output_file), var_name)
        total_size += size
        generated_files.append(var_name)
        img_width = w
        img_height = h

    print("=" * 50)
    print(f"\n转换完成!")
    print(f"  图片尺寸: {img_width} x {img_height}")
    print(f"  总帧数: {len(png_files)}")
    print(f"  总大小: {total_size:,} 字节 ({total_size/1024/1024:.2f} MB)")
    print(f"  输出目录: {output_path}")

    # 生成索引头文件
    index_file = output_path / f"{prefix}_index.h"
    with open(index_file, 'w', encoding='utf-8') as f:
        f.write(f'// Auto-generated animation index file\n')
        f.write(f'// Total frames: {len(generated_files)}\n')
        f.write(f'// Image size: {img_width} x {img_height}\n')
        f.write(f'// Format: RGB565 Little Endian (for LVGL)\n\n')
        f.write(f'#ifndef _{prefix.upper()}_INDEX_H_\n')
        f.write(f'#define _{prefix.upper()}_INDEX_H_\n\n')
        f.write(f'#include <stdint.h>\n\n')

        # 包含所有帧头文件
        for var_name in generated_files:
            f.write(f'#include "{var_name}.h"\n')

        # 图片尺寸定义
        f.write(f'\n// 图片尺寸\n')
        f.write(f'#define {prefix.upper()}_WIDTH  {img_width}\n')
        f.write(f'#define {prefix.upper()}_HEIGHT {img_height}\n\n')

        # 生成正向播放数组
        f.write(f'// 图片数组 - 正向播放\n')
        f.write(f'static const uint8_t* {prefix}_frames[] = {{\n')
        for var_name in generated_files:
            f.write(f'    {var_name},\n')
        f.write(f'}};\n')
        f.write(f'#define {prefix.upper()}_FRAME_COUNT {len(generated_files)}\n\n')

        # 生成往返动画数组 (如果帧数 > 2)
        if len(generated_files) > 2:
            f.write(f'// 图片数组 - 往返播放 (只排除尾帧避免重复)\n')
            f.write(f'static const uint8_t* {prefix}_bounce[] = {{\n')
            # 正向: 1, 2, 3, ..., n
            for var_name in generated_files:
                f.write(f'    {var_name},\n')
            # 反向: n-1, n-2, ..., 1 (只排除尾帧n)
            for var_name in reversed(generated_files[:-1]):
                f.write(f'    {var_name},\n')
            f.write(f'}};\n')
            bounce_count = len(generated_files) * 2 - 1
            f.write(f'#define {prefix.upper()}_BOUNCE_COUNT {bounce_count}\n\n')

        f.write(f'#endif // _{prefix.upper()}_INDEX_H_\n')

    print(f"\n已生成索引文件: {index_file}")
    if len(generated_files) > 2:
        bounce_count = len(generated_files) * 2 - 1
        print(f"  正向播放帧数: {len(generated_files)}")
        print(f"  往返播放帧数: {bounce_count}")

    return True


def main():
    # 6种动画配置
    ANIMATIONS = [
        ("gifs/dynamic1", "main/images/dynamic1", "gImage_dynamic1"),
        ("gifs/dynamic2", "main/images/dynamic2", "gImage_dynamic2"),
        ("gifs/static1",  "main/images/static1",  "gImage_static1"),
        ("gifs/static2",  "main/images/static2",  "gImage_static2"),
        ("gifs/dynamic3", "main/images/dynamic3", "gImage_dynamic3"),
        ("gifs/dynamic4", "main/images/dynamic4", "gImage_dynamic4"),
    ]

    if len(sys.argv) < 3:
        # 无参数时，批量转换所有6种动画
        print("PNG 序列转 RGB565 C 头文件工具")
        print("=" * 50)
        print("\n批量转换6种动画...\n")

        total_success = 0
        for input_dir, output_dir, prefix in ANIMATIONS:
            print(f"\n{'='*50}")
            print(f"处理: {prefix}")
            print(f"{'='*50}")
            if os.path.isdir(input_dir):
                if batch_convert(input_dir, output_dir, prefix):
                    total_success += 1
            else:
                print(f"跳过: 目录不存在 - {input_dir}")

        print(f"\n{'='*50}")
        print(f"完成! 成功转换 {total_success}/{len(ANIMATIONS)} 组动画")
        sys.exit(0 if total_success > 0 else 1)

    # 有参数时，按原方式处理
    input_dir = sys.argv[1]
    output_dir = sys.argv[2]
    prefix = sys.argv[3] if len(sys.argv) > 3 else "gImage"

    if not os.path.isdir(input_dir):
        print(f"错误: 输入目录不存在 - {input_dir}")
        sys.exit(1)

    success = batch_convert(input_dir, output_dir, prefix)
    sys.exit(0 if success else 1)


if __name__ == '__main__':
    main()
