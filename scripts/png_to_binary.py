#!/usr/bin/env python3
"""
PNG 图片嵌入工具 - 将PNG文件作为二进制数据嵌入C头文件
PNG保持压缩格式，运行时由LVGL解码

使用方法:
    python png_to_binary.py <输入目录> <输出目录> [前缀]
    python png_to_binary.py gifs main/images/christmas gImage_christmas
"""

import os
import sys
from pathlib import Path


def png_to_c_array(png_path, output_path, var_name):
    """将PNG文件转换为C数组（保持PNG压缩格式）"""
    with open(png_path, 'rb') as f:
        data = f.read()

    # 验证PNG签名
    if data[:8] != b'\x89PNG\r\n\x1a\n':
        print(f"  警告: {png_path} 不是有效的PNG文件")

    size = len(data)

    with open(output_path, 'w', encoding='utf-8') as f:
        f.write(f'// Auto-generated from {os.path.basename(png_path)}\n')
        f.write(f'// PNG compressed data - {size} bytes\n\n')
        f.write(f'#ifndef _{var_name.upper()}_H_\n')
        f.write(f'#define _{var_name.upper()}_H_\n\n')
        f.write(f'#include <stdint.h>\n\n')
        f.write(f'static const uint8_t {var_name}[] = {{\n')

        # 每16个字节一行
        bytes_per_line = 16
        for i in range(0, len(data), bytes_per_line):
            chunk = data[i:i+bytes_per_line]
            hex_str = ', '.join(f'0x{b:02X}' for b in chunk)
            f.write(f'    {hex_str},\n')

        f.write(f'}};\n')
        f.write(f'#define {var_name.upper()}_SIZE {size}\n\n')
        f.write(f'#endif // _{var_name.upper()}_H_\n')

    print(f"  {os.path.basename(png_path)}: {size:,} bytes")
    return size


def batch_convert(input_dir, output_dir, prefix="gImage"):
    """批量转换目录下的所有PNG图片"""
    input_path = Path(input_dir)
    output_path = Path(output_dir)

    # 创建输出目录
    output_path.mkdir(parents=True, exist_ok=True)

    # 获取所有PNG文件并排序
    png_files = set(input_path.glob('*.png')) | set(input_path.glob('*.PNG'))
    png_files = sorted(png_files)

    if not png_files:
        print(f"错误: 在 {input_dir} 中未找到PNG文件")
        return False

    print(f"\n找到 {len(png_files)} 个PNG文件")
    print("=" * 50)

    total_size = 0
    generated_files = []

    for i, png_file in enumerate(png_files, 1):
        var_name = f"{prefix}_{i:04d}"
        output_file = output_path / f"{var_name}.h"
        size = png_to_c_array(str(png_file), str(output_file), var_name)
        total_size += size
        generated_files.append((var_name, size))

    print("=" * 50)
    print(f"\n压缩统计:")
    print(f"  PNG总大小: {total_size:,} bytes ({total_size/1024:.1f} KB)")

    # 计算原始RGB565大小 (从第一个PNG获取尺寸)
    from PIL import Image
    first_img = Image.open(png_files[0])
    width, height = first_img.size
    raw_size = width * height * 2 * len(png_files)
    compression_ratio = raw_size / total_size

    print(f"  RGB565原始大小: {raw_size:,} bytes ({raw_size/1024:.1f} KB)")
    print(f"  压缩比: {compression_ratio:.1f}x")
    print(f"  节省空间: {(1 - total_size/raw_size)*100:.1f}%")

    # 生成索引头文件
    index_file = output_path / f"{prefix}_png_index.h"
    with open(index_file, 'w', encoding='utf-8') as f:
        f.write(f'// Auto-generated PNG animation index file\n')
        f.write(f'// Total frames: {len(generated_files)}\n')
        f.write(f'// Image size: {width} x {height}\n')
        f.write(f'// Format: PNG compressed\n')
        f.write(f'// Total size: {total_size} bytes\n\n')
        f.write(f'#ifndef _{prefix.upper()}_PNG_INDEX_H_\n')
        f.write(f'#define _{prefix.upper()}_PNG_INDEX_H_\n\n')
        f.write(f'#include <stdint.h>\n\n')

        # 包含所有帧头文件
        for var_name, _ in generated_files:
            f.write(f'#include "{var_name}.h"\n')

        f.write(f'\n// 图片尺寸\n')
        f.write(f'#define {prefix.upper()}_PNG_WIDTH  {width}\n')
        f.write(f'#define {prefix.upper()}_PNG_HEIGHT {height}\n\n')

        # PNG数据结构
        f.write(f'// PNG数据结构\n')
        f.write(f'typedef struct {{\n')
        f.write(f'    const uint8_t* data;\n')
        f.write(f'    uint32_t size;\n')
        f.write(f'}} PngFrameData;\n\n')

        # 生成正向播放数组
        f.write(f'// 图片数组 - 正向播放\n')
        f.write(f'static const PngFrameData {prefix}_png_frames[] = {{\n')
        for var_name, size in generated_files:
            f.write(f'    {{ {var_name}, {size} }},\n')
        f.write(f'}};\n')
        f.write(f'#define {prefix.upper()}_PNG_FRAME_COUNT {len(generated_files)}\n\n')

        # 生成往返动画数组
        if len(generated_files) > 2:
            f.write(f'// 图片数组 - 往返播放\n')
            f.write(f'static const PngFrameData {prefix}_png_bounce[] = {{\n')
            # 正向
            for var_name, size in generated_files:
                f.write(f'    {{ {var_name}, {size} }},\n')
            # 反向 (排除尾帧)
            for var_name, size in reversed(generated_files[:-1]):
                f.write(f'    {{ {var_name}, {size} }},\n')
            f.write(f'}};\n')
            bounce_count = len(generated_files) * 2 - 1
            f.write(f'#define {prefix.upper()}_PNG_BOUNCE_COUNT {bounce_count}\n\n')

        f.write(f'#endif // _{prefix.upper()}_PNG_INDEX_H_\n')

    print(f"\n已生成索引文件: {index_file}")

    return True


def main():
    if len(sys.argv) < 3:
        print("PNG嵌入工具 - 保持PNG压缩格式")
        print("=" * 40)
        print("\n用法:")
        print("  python png_to_binary.py <输入目录> <输出目录> [前缀]")
        print("\n示例:")
        print("  python png_to_binary.py gifs main/images/christmas gImage_christmas")
        sys.exit(1)

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
