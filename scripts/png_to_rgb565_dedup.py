#!/usr/bin/env python3
"""
PNG 图片序列转 RGB565 C 头文件工具 - 帧去重版本
跨动画分析相似帧，只保存唯一帧，相似帧复用同一数据

使用方法:
    python png_to_rgb565_dedup.py [相似度阈值]
    python png_to_rgb565_dedup.py 0.90  # 90%相似度阈值
"""

import os
import sys
import shutil
from pathlib import Path
from collections import defaultdict

try:
    from PIL import Image
    import numpy as np
except ImportError:
    print("错误: 请先安装依赖")
    print("运行: pip install Pillow numpy")
    sys.exit(1)


# 配置
ANIMATIONS = [
    ("gifs/dynamic1", "dynamic1", "gImage_dynamic1"),
    ("gifs/dynamic2", "dynamic2", "gImage_dynamic2"),
    ("gifs/static1",  "static1",  "gImage_static1"),
    ("gifs/static2",  "static2",  "gImage_static2"),
    ("gifs/dynamic3", "dynamic3", "gImage_dynamic3"),
    ("gifs/dynamic4", "dynamic4", "gImage_dynamic4"),
]

SHARED_DIR = "main/images/shared"
OUTPUT_BASE = "main/images"


def rgb888_to_rgb565(r, g, b):
    """RGB888 转 RGB565 (16位色)"""
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)


def load_image_as_array(path):
    """加载图片并转换为numpy数组"""
    img = Image.open(path).convert('RGB')
    return np.array(img), img.size


def calculate_similarity(arr1, arr2):
    """计算两张图片的相似度 (0-1)"""
    if arr1.shape != arr2.shape:
        return 0.0

    # 使用均方误差 (MSE)
    diff = arr1.astype(np.float32) - arr2.astype(np.float32)
    mse = np.mean(diff ** 2)

    # 转换为相似度 (MSE越小相似度越高)
    # 对于RGB图像，MSE范围是0-65025 (255^2)
    # 使用指数衰减使相似度更敏感
    similarity = np.exp(-mse / 5000)
    return similarity


def find_unique_frames(all_frames, threshold=0.90):
    """
    找出所有唯一帧
    返回: {frame_path: unique_frame_id} 映射
    """
    print(f"\n分析 {len(all_frames)} 张图片的相似度 (阈值: {threshold*100:.0f}%)...")

    # 加载所有图片
    images = {}
    for path in all_frames:
        arr, size = load_image_as_array(path)
        images[path] = arr

    # 找出唯一帧
    unique_frames = []  # [(path, array), ...]
    frame_mapping = {}  # {original_path: unique_frame_index}

    for path, arr in images.items():
        # 检查是否与已有唯一帧相似
        found_match = False
        for idx, (unique_path, unique_arr) in enumerate(unique_frames):
            similarity = calculate_similarity(arr, unique_arr)
            if similarity >= threshold:
                frame_mapping[path] = idx
                found_match = True
                print(f"  {os.path.basename(path)} -> 复用 {os.path.basename(unique_path)} (相似度: {similarity*100:.1f}%)")
                break

        if not found_match:
            # 新的唯一帧
            frame_mapping[path] = len(unique_frames)
            unique_frames.append((path, arr))
            print(f"  {os.path.basename(path)} -> 新帧 #{len(unique_frames)}")

    print(f"\n去重结果: {len(all_frames)} 帧 -> {len(unique_frames)} 唯一帧")
    print(f"压缩比: {len(unique_frames)/len(all_frames)*100:.1f}%")

    return unique_frames, frame_mapping


def generate_frame_header(img_array, output_path, var_name, width, height):
    """生成单个帧的头文件"""
    with open(output_path, 'w', encoding='utf-8') as f:
        f.write(f'// Auto-generated unique frame\n')
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
                r, g, b = img_array[y, x]
                pixel = rgb888_to_rgb565(int(r), int(g), int(b))

                # 小端序 (LSB first)
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
        f.write(f'#endif // _{var_name.upper()}_H_\n')


def generate_animation_index(anim_name, prefix, frame_indices, unique_count, width, height, output_dir):
    """生成动画索引文件"""
    index_file = Path(output_dir) / f"{prefix}_index.h"

    with open(index_file, 'w', encoding='utf-8') as f:
        f.write(f'// Auto-generated animation index file (with frame deduplication)\n')
        f.write(f'// Animation: {anim_name}\n')
        f.write(f'// Total frames: {len(frame_indices)} (references to {unique_count} unique frames)\n')
        f.write(f'// Image size: {width} x {height}\n')
        f.write(f'// Format: RGB565 Little Endian (for LVGL)\n\n')
        f.write(f'#ifndef _{prefix.upper()}_INDEX_H_\n')
        f.write(f'#define _{prefix.upper()}_INDEX_H_\n\n')
        f.write(f'#include <stdint.h>\n\n')

        # 包含共享帧头文件
        f.write(f'// Include shared frames\n')
        f.write(f'#include "shared/gFrame_index.h"\n\n')

        # 图片尺寸定义
        f.write(f'// 图片尺寸\n')
        f.write(f'#define {prefix.upper()}_WIDTH  {width}\n')
        f.write(f'#define {prefix.upper()}_HEIGHT {height}\n\n')

        # 生成正向播放数组
        f.write(f'// 图片数组 - 正向播放 (引用共享帧)\n')
        f.write(f'static const uint8_t* {prefix}_frames[] = {{\n')
        for idx in frame_indices:
            f.write(f'    gFrame_{idx+1:04d},\n')
        f.write(f'}};\n')
        f.write(f'#define {prefix.upper()}_FRAME_COUNT {len(frame_indices)}\n\n')

        # 生成往返动画数组
        if len(frame_indices) > 2:
            f.write(f'// 图片数组 - 往返播放\n')
            f.write(f'static const uint8_t* {prefix}_bounce[] = {{\n')
            # 正向
            for idx in frame_indices:
                f.write(f'    gFrame_{idx+1:04d},\n')
            # 反向 (排除尾帧)
            for idx in reversed(frame_indices[:-1]):
                f.write(f'    gFrame_{idx+1:04d},\n')
            f.write(f'}};\n')
            bounce_count = len(frame_indices) * 2 - 1
            f.write(f'#define {prefix.upper()}_BOUNCE_COUNT {bounce_count}\n\n')

        f.write(f'#endif // _{prefix.upper()}_INDEX_H_\n')

    return str(index_file)


def generate_shared_index(unique_frames, width, height, output_dir):
    """生成共享帧索引文件"""
    index_file = Path(output_dir) / "gFrame_index.h"

    with open(index_file, 'w', encoding='utf-8') as f:
        f.write(f'// Auto-generated shared frames index\n')
        f.write(f'// Total unique frames: {len(unique_frames)}\n')
        f.write(f'// Image size: {width} x {height}\n')
        f.write(f'// Format: RGB565 Little Endian (for LVGL)\n\n')
        f.write(f'#ifndef _GFRAME_INDEX_H_\n')
        f.write(f'#define _GFRAME_INDEX_H_\n\n')
        f.write(f'#include <stdint.h>\n\n')

        # 包含所有唯一帧
        for i in range(len(unique_frames)):
            f.write(f'#include "gFrame_{i+1:04d}.h"\n')

        f.write(f'\n// Shared frame dimensions\n')
        f.write(f'#define GFRAME_WIDTH  {width}\n')
        f.write(f'#define GFRAME_HEIGHT {height}\n')
        f.write(f'#define GFRAME_UNIQUE_COUNT {len(unique_frames)}\n\n')

        f.write(f'#endif // _GFRAME_INDEX_H_\n')


def main():
    # 解析参数
    threshold = 0.90  # 默认90%相似度
    if len(sys.argv) > 1:
        try:
            threshold = float(sys.argv[1])
            if threshold < 0.5 or threshold > 1.0:
                print("错误: 阈值应在 0.5-1.0 之间")
                sys.exit(1)
        except ValueError:
            print(f"错误: 无效的阈值 '{sys.argv[1]}'")
            sys.exit(1)

    print("=" * 60)
    print("PNG 序列转 RGB565 - 帧去重版本")
    print("=" * 60)
    print(f"相似度阈值: {threshold*100:.0f}%")

    # 收集所有PNG文件
    all_frames = []
    anim_frames = {}  # {anim_name: [paths]}

    for input_dir, anim_name, prefix in ANIMATIONS:
        input_path = Path(input_dir)
        if not input_path.exists():
            print(f"跳过: 目录不存在 - {input_dir}")
            continue

        png_files = sorted(input_path.glob('*.png')) + sorted(input_path.glob('*.PNG'))
        png_files = sorted(set(png_files))

        if png_files:
            anim_frames[anim_name] = [str(p) for p in png_files]
            all_frames.extend(anim_frames[anim_name])
            print(f"  {anim_name}: {len(png_files)} 帧")

    if not all_frames:
        print("错误: 未找到任何PNG文件")
        sys.exit(1)

    print(f"\n总计: {len(all_frames)} 帧")

    # 分析相似度并找出唯一帧
    unique_frames, frame_mapping = find_unique_frames(all_frames, threshold)

    # 获取图片尺寸
    _, (width, height) = load_image_as_array(all_frames[0])

    # 创建共享帧目录
    shared_path = Path(SHARED_DIR)
    if shared_path.exists():
        shutil.rmtree(shared_path)
    shared_path.mkdir(parents=True, exist_ok=True)

    # 生成唯一帧的头文件
    print(f"\n生成 {len(unique_frames)} 个唯一帧头文件...")
    total_size = 0
    frame_size = width * height * 2

    for i, (path, arr) in enumerate(unique_frames):
        var_name = f"gFrame_{i+1:04d}"
        output_file = shared_path / f"{var_name}.h"
        generate_frame_header(arr, str(output_file), var_name, width, height)
        total_size += frame_size
        print(f"  生成: {var_name}.h")

    # 生成共享帧索引
    generate_shared_index(unique_frames, width, height, str(shared_path))
    print(f"  生成: gFrame_index.h")

    # 为每个动画生成索引文件
    print(f"\n生成动画索引文件...")
    for input_dir, anim_name, prefix in ANIMATIONS:
        if anim_name not in anim_frames:
            continue

        output_dir = Path(OUTPUT_BASE) / anim_name
        output_dir.mkdir(parents=True, exist_ok=True)

        # 清理旧的帧文件（但保留索引）
        for old_file in output_dir.glob("*.h"):
            if "_index.h" not in old_file.name:
                old_file.unlink()

        # 构建帧索引
        frame_indices = [frame_mapping[p] for p in anim_frames[anim_name]]

        # 生成索引文件
        index_file = generate_animation_index(
            anim_name, prefix, frame_indices,
            len(unique_frames), width, height, str(output_dir)
        )
        print(f"  生成: {anim_name}/{prefix}_index.h")

    # 打印统计
    print("\n" + "=" * 60)
    print("转换完成!")
    print("=" * 60)
    print(f"  原始帧数: {len(all_frames)}")
    print(f"  唯一帧数: {len(unique_frames)}")
    print(f"  去重比例: {(1 - len(unique_frames)/len(all_frames))*100:.1f}%")
    print(f"  图片尺寸: {width} x {height}")
    print(f"  每帧大小: {frame_size:,} 字节 ({frame_size/1024:.1f} KB)")
    print(f"  总数据量: {total_size:,} 字节 ({total_size/1024/1024:.2f} MB)")

    original_size = len(all_frames) * frame_size
    print(f"\n  原始大小: {original_size:,} 字节 ({original_size/1024/1024:.2f} MB)")
    print(f"  压缩后:   {total_size:,} 字节 ({total_size/1024/1024:.2f} MB)")
    print(f"  节省:     {(1 - total_size/original_size)*100:.1f}%")


if __name__ == '__main__':
    main()
