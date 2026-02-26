#!/usr/bin/env python3
"""
瓦片动画打包脚本

将 idx + tileset.bin 打包为 animation_frames.bin

输入:
  - gifs/*.idx: 每帧的瓦片索引文件 (344 bytes each)
  - gifs/tileset.bin: 共享瓦片库 + 调色板 (152KB)

输出:
  - main/images/shared/animation_frames.bin: 打包后的动画资源

使用方法:
    python scripts/pack_tile_assets.py

数据格式说明:
    tileset.bin:
        Header (12 bytes): magic(2B=TT) version(2B) tile_size(1B) colors(1B)
                          tile_count(2B) width(2B) height(2B)
        Palette (32 bytes): 16 x RGB565
        Tiles: tile_count x (tile_size^2 / 2) bytes (4-bit packed)

    idx 文件:
        Header (12 bytes): magic(2B=DI) width(2B) height(2B) reserved(6B)
        Tile IDs: (width/tile_size) x (height/tile_size) x 2 bytes

    animation_frames.bin (新格式):
        Header (20 bytes):
            magic      : 4B  ('TILE')
            version    : 2B  (2)
            frame_count: 2B  (总帧数)
            tile_size  : 1B  (10)
            colors     : 1B  (16)
            src_width  : 2B  (140 - 源尺寸)
            src_height : 2B  (120)
            tile_count : 2B  (瓦片数量)
            tiles_per_frame: 2B (每帧瓦片数)
        Palette (32 bytes): 从 tileset.bin 复制
        Tiles data: 从 tileset.bin 复制所有瓦片
        Frame data: 每帧的 tile_id 数组 (tiles_per_frame x 2B per frame)
"""

import os
import sys
import struct
import re
from pathlib import Path

# 配置
GIFS_DIR = "gifs"
OUTPUT_FILE = "main/images/shared/animation_frames.bin"

# 动画定义 (与 animation_loader.h 中的 AnimLoaderType 对应)
ANIMATIONS = [
    ("dynamic1", 13),  # ANIM_DYNAMIC1: Speaking
    ("dynamic2", 13),  # ANIM_DYNAMIC2: Listening
    ("static1",  13),  # ANIM_STATIC1: Idle/Breathing
    ("dynamic3", 13),  # ANIM_DYNAMIC3: Touch interaction
]


def natural_sort_key(path):
    """自然排序key函数，按文件名中的数字排序"""
    name = path.stem if hasattr(path, 'stem') else str(path)
    match = re.search(r'_(\d+)$', name)
    if match:
        return int(match.group(1))
    return 0


def read_tileset(tileset_path):
    """读取 tileset.bin"""
    print(f"\n[Tileset] 读取: {tileset_path}")

    with open(tileset_path, 'rb') as f:
        data = f.read()

    # 解析头部
    magic = struct.unpack('<H', data[0:2])[0]
    version = struct.unpack('<H', data[2:4])[0]
    tile_size = data[4]
    colors = data[5]
    tile_count = struct.unpack('<H', data[6:8])[0]
    width = struct.unpack('<H', data[8:10])[0]
    height = struct.unpack('<H', data[10:12])[0]

    print(f"  Magic: 0x{magic:04X} (期望: 0x5454='TT')")
    print(f"  Version: {version}")
    print(f"  Tile size: {tile_size}x{tile_size}")
    print(f"  Colors: {colors}")
    print(f"  Tile count: {tile_count}")
    print(f"  Source size: {width}x{height}")

    # 验证 magic
    if magic != 0x5454:  # 'TT'
        print(f"  警告: Magic 不是 'TT' (0x5454), 继续尝试...")

    # 读取调色板 (16 x RGB565 = 32 bytes)
    palette_offset = 12
    palette_data = data[palette_offset:palette_offset + colors * 2]
    print(f"  Palette: {len(palette_data)} bytes")

    # 读取瓦片数据
    tiles_offset = palette_offset + colors * 2
    tile_bytes = tile_size * tile_size // 2  # 4-bit packed
    tiles_data = data[tiles_offset:tiles_offset + tile_count * tile_bytes]
    print(f"  Tiles: {len(tiles_data)} bytes ({tile_count} tiles x {tile_bytes} bytes)")

    return {
        'tile_size': tile_size,
        'colors': colors,
        'tile_count': tile_count,
        'width': width,
        'height': height,
        'palette': palette_data,
        'tiles': tiles_data,
    }


def read_idx_file(idx_path, tile_count):
    """读取单个 idx 文件

    idx 格式:
      Header (8 bytes):
        [0-1] magic   : uint16 LE = 0x4944 ('ID')
        [2-3] version : uint16 LE (bit1=0x02 表示有变换)
        [4-5] width   : uint16 LE = 14 (瓦片列数)
        [6-7] height  : uint16 LE = 12 (瓦片行数)
      Indices:
        tile_count <= 256: uint8 × (width×height)
        tile_count > 256:  uint16 LE × (width×height)
    """
    with open(idx_path, 'rb') as f:
        data = f.read()

    # 解析头部
    magic = struct.unpack('<H', data[0:2])[0]
    version = struct.unpack('<H', data[2:4])[0]
    pixel_w = struct.unpack('<H', data[4:6])[0]  # 像素宽度 (140)
    pixel_h = struct.unpack('<H', data[6:8])[0]  # 像素高度 (120)

    # 计算瓦片数量 (像素尺寸 / 瓦片尺寸)
    tile_size = 10  # 瓦片大小固定为 10x10
    tiles_w = pixel_w // tile_size  # 14
    tiles_h = pixel_h // tile_size  # 12
    tiles_per_frame = tiles_w * tiles_h  # 168

    # Debug: 打印头部信息（只打印第一个文件）
    print(f"    [idx] {idx_path.name}: magic=0x{magic:04X}, ver={version}, pixels={pixel_w}x{pixel_h}, tiles={tiles_w}x{tiles_h}={tiles_per_frame}, 文件={len(data)}字节")

    # 验证 magic
    if magic != 0x4944:  # 'ID'
        print(f"    警告: magic 不是 0x4944='ID'")

    has_transform = (version & 0x02) != 0

    # 根据文件大小判断是8位还是16位索引
    data_size = len(data) - 8  # 减去头部
    expected_16bit = tiles_per_frame * 2
    expected_8bit = tiles_per_frame

    # 读取 tile IDs (从偏移8开始)
    offset = 8
    tile_ids_raw = []

    # 根据实际数据大小判断格式
    if data_size >= expected_16bit:
        # 16位索引
        for i in range(tiles_per_frame):
            packed = struct.unpack('<H', data[offset:offset+2])[0]
            if has_transform:
                tile_id = packed & 0x1FFF  # 低13位是 tile_id
            else:
                tile_id = packed
            tile_ids_raw.append(tile_id)
            offset += 2
    elif data_size >= expected_8bit:
        # 8位索引
        for i in range(tiles_per_frame):
            tile_id = data[offset]
            tile_ids_raw.append(tile_id)
            offset += 1
    else:
        print(f"    错误: {idx_path.name} 数据不足! 文件={len(data)}, 需要={8+expected_8bit}或{8+expected_16bit}")
        return {'tiles_w': tiles_w, 'tiles_h': tiles_h, 'tile_ids': b''}

    # 转换为统一的 uint16 LE 字节串
    tile_ids_bytes = b''.join(struct.pack('<H', tid) for tid in tile_ids_raw)

    return {
        'tiles_w': tiles_w,
        'tiles_h': tiles_h,
        'tile_ids': tile_ids_bytes,
    }


def collect_idx_files(gifs_dir):
    """收集所有动画的 idx 文件"""
    frames_by_anim = {}
    total_frames = 0

    print(f"\n[IDX] 扫描目录: {gifs_dir}")

    for anim_name, expected_count in ANIMATIONS:
        # 查找该动画的所有 idx 文件
        pattern = f"Default_{anim_name}_*.idx"
        idx_files = list(Path(gifs_dir).glob(pattern))

        # 也尝试不带 Default_ 前缀
        if not idx_files:
            pattern = f"{anim_name}_*.idx"
            idx_files = list(Path(gifs_dir).glob(pattern))

        # 自然排序
        idx_files = sorted(idx_files, key=natural_sort_key)

        if idx_files:
            frames_by_anim[anim_name] = idx_files
            total_frames += len(idx_files)
            print(f"  {anim_name}: {len(idx_files)} 帧")
            for idx_file in idx_files[:3]:
                print(f"    - {idx_file.name}")
            if len(idx_files) > 3:
                print(f"    ... (共 {len(idx_files)} 个)")
        else:
            print(f"  {anim_name}: 未找到 idx 文件!")

    print(f"  总计: {total_frames} 帧")
    return frames_by_anim, total_frames


def pack_animation_frames(tileset, frames_by_anim, output_path):
    """打包所有动画帧到二进制文件"""
    print(f"\n[Pack] 生成: {output_path}")

    # 确保输出目录存在
    Path(output_path).parent.mkdir(parents=True, exist_ok=True)

    # 收集所有帧数据
    all_frames = []
    anim_frame_counts = []

    for anim_name, _ in ANIMATIONS:
        if anim_name in frames_by_anim:
            idx_files = frames_by_anim[anim_name]
            frame_count = len(idx_files)

            for idx_file in idx_files:
                frame_data = read_idx_file(idx_file, tileset['tile_count'])
                all_frames.append(frame_data['tile_ids'])

            anim_frame_counts.append(frame_count)
            print(f"  {anim_name}: {frame_count} 帧")
        else:
            anim_frame_counts.append(0)
            print(f"  {anim_name}: 0 帧 (缺失)")

    total_frames = len(all_frames)
    tiles_per_frame = tileset['width'] // tileset['tile_size'] * tileset['height'] // tileset['tile_size']

    print(f"\n  总帧数: {total_frames}")
    print(f"  每帧瓦片数: {tiles_per_frame}")

    with open(output_path, 'wb') as f:
        # 写入头部 (20 bytes)
        f.write(b'TILE')                                           # magic (4B)
        f.write(struct.pack('<H', 2))                              # version (2B)
        f.write(struct.pack('<H', total_frames))                   # frame_count (2B)
        f.write(struct.pack('B', tileset['tile_size']))            # tile_size (1B)
        f.write(struct.pack('B', tileset['colors']))               # colors (1B)
        f.write(struct.pack('<H', tileset['width']))               # src_width (2B)
        f.write(struct.pack('<H', tileset['height']))              # src_height (2B)
        f.write(struct.pack('<H', tileset['tile_count']))          # tile_count (2B)
        f.write(struct.pack('<H', tiles_per_frame))                # tiles_per_frame (2B)

        header_size = 18  # 4+2+2+1+1+2+2+2+2 = 18 bytes
        print(f"\n  Header: {header_size} bytes")

        # 写入调色板 (32 bytes)
        f.write(tileset['palette'])
        print(f"  Palette: {len(tileset['palette'])} bytes")

        # 写入瓦片数据
        f.write(tileset['tiles'])
        print(f"  Tiles: {len(tileset['tiles'])} bytes")

        # 写入动画帧数表 (每个动画的帧数, 4 x 2B = 8 bytes)
        for count in anim_frame_counts:
            f.write(struct.pack('<H', count))
        print(f"  Anim frame counts: {len(anim_frame_counts) * 2} bytes")

        # 写入帧数据
        frames_offset = f.tell()
        print(f"  [Debug] all_frames 数量: {len(all_frames)}")
        if all_frames:
            print(f"  [Debug] 第一帧数据长度: {len(all_frames[0])} bytes")
        for i, frame_tile_ids in enumerate(all_frames):
            if len(frame_tile_ids) == 0:
                print(f"  [Debug] 警告: 帧 {i} 数据为空!")
            f.write(frame_tile_ids)
        frames_size = f.tell() - frames_offset
        print(f"  Frame data: {frames_size} bytes ({total_frames} frames x {tiles_per_frame * 2} bytes)")

        total_size = f.tell()

    print(f"\n  总大小: {total_size:,} bytes ({total_size/1024:.1f} KB)")

    return output_path, total_size, anim_frame_counts


def generate_header_files(tileset, anim_frame_counts, frames_by_anim):
    """生成动画索引头文件"""
    print(f"\n[Headers] 生成索引头文件...")

    output_dir = Path("main/images")

    # 为每个动画生成头文件
    frame_offset = 0
    for (anim_name, _), frame_count in zip(ANIMATIONS, anim_frame_counts):
        if frame_count == 0:
            continue

        anim_dir = output_dir / anim_name
        anim_dir.mkdir(parents=True, exist_ok=True)

        # 清理旧的 .h 文件
        for old_file in anim_dir.glob("*.h"):
            old_file.unlink()

        prefix = f"gImage_{anim_name}"
        header_file = anim_dir / f"{prefix}_index.h"

        # 生成帧索引 (相对索引: 0, 1, 2, ..., frame_count-1)
        # 注意: GetFrame() 会根据 anim 类型自动计算绝对偏移
        frame_indices = list(range(frame_count))

        # 生成往返索引 (0-7-0)
        bounce_indices = frame_indices.copy()
        if len(frame_indices) > 1:
            bounce_indices.extend(reversed(frame_indices[:-1]))

        with open(header_file, 'w', encoding='utf-8') as f:
            f.write(f'// Auto-generated animation index file (tile-based system)\n')
            f.write(f'// Animation: {anim_name}\n')
            f.write(f'// Total frames: {frame_count}\n')
            f.write(f'// Frame size: {tileset["width"]} x {tileset["height"]} (display layer handles 2x upscale)\n')
            f.write(f'// Format: Tile-based with 16-color palette\n\n')
            f.write(f'#ifndef _{prefix.upper()}_INDEX_H_\n')
            f.write(f'#define _{prefix.upper()}_INDEX_H_\n\n')
            f.write(f'#include <stdint.h>\n\n')

            # 图片尺寸定义 (输出尺寸是 2x)
            f.write(f'// 图片尺寸 (display layer handles 2x upscale to 280x240)\n')
            f.write(f'#define {prefix.upper()}_WIDTH  {tileset["width"]}\n')
            f.write(f'#define {prefix.upper()}_HEIGHT {tileset["height"]}\n\n')

            # 帧索引数组
            f.write(f'// 帧索引数组 - 正向播放\n')
            f.write(f'static const uint8_t {prefix}_frame_indices[] = {{\n')
            for i, idx in enumerate(frame_indices):
                if i % 16 == 0:
                    f.write('    ')
                f.write(f'{idx}, ')
                if (i + 1) % 16 == 0:
                    f.write('\n')
            if len(frame_indices) % 16 != 0:
                f.write('\n')
            f.write(f'}};\n')
            f.write(f'#define {prefix.upper()}_FRAME_COUNT {frame_count}\n\n')

            # 往返播放索引
            f.write(f'// 帧索引数组 - 往返播放\n')
            f.write(f'static const uint8_t {prefix}_bounce_indices[] = {{\n')
            for i, idx in enumerate(bounce_indices):
                if i % 16 == 0:
                    f.write('    ')
                f.write(f'{idx}, ')
                if (i + 1) % 16 == 0:
                    f.write('\n')
            if len(bounce_indices) % 16 != 0:
                f.write('\n')
            f.write(f'}};\n')
            f.write(f'#define {prefix.upper()}_BOUNCE_COUNT {len(bounce_indices)}\n\n')

            f.write(f'#endif // _{prefix.upper()}_INDEX_H_\n')

        print(f"  生成: {header_file}")
        frame_offset += frame_count

    # 生成共享帧信息头文件
    shared_dir = output_dir / "shared"
    shared_dir.mkdir(parents=True, exist_ok=True)

    shared_header = shared_dir / "gFrame_index.h"
    total_frames = sum(anim_frame_counts)

    with open(shared_header, 'w', encoding='utf-8') as f:
        f.write(f'// Auto-generated shared frames info (tile-based system)\n')
        f.write(f'// Total frames: {total_frames}\n')
        f.write(f'// Source size: {tileset["width"]} x {tileset["height"]}\n')
        f.write(f'// Frame size: {tileset["width"]} x {tileset["height"]} (display layer handles 2x upscale)\n\n')
        f.write(f'#ifndef _GFRAME_INDEX_H_\n')
        f.write(f'#define _GFRAME_INDEX_H_\n\n')
        f.write(f'#include <stdint.h>\n\n')
        f.write(f'// Frame dimensions (display layer handles 2x upscale to 280x240)\n')
        f.write(f'#define GFRAME_WIDTH  {tileset["width"]}\n')
        f.write(f'#define GFRAME_HEIGHT {tileset["height"]}\n')
        f.write(f'#define GFRAME_SIZE   ({tileset["width"]} * {tileset["height"]} * 2)  // RGB565\n')
        f.write(f'#define GFRAME_UNIQUE_COUNT {total_frames}\n\n')
        f.write(f'// Source dimensions (before upscale)\n')
        f.write(f'#define GFRAME_SRC_WIDTH  {tileset["width"]}\n')
        f.write(f'#define GFRAME_SRC_HEIGHT {tileset["height"]}\n\n')
        f.write(f'// Tile info\n')
        f.write(f'#define GFRAME_TILE_SIZE {tileset["tile_size"]}\n')
        f.write(f'#define GFRAME_TILE_COUNT {tileset["tile_count"]}\n\n')
        f.write(f'// Frame data is stored in assets partition\n')
        f.write(f'// Use AnimationLoader to access frame data\n\n')
        f.write(f'#endif // _GFRAME_INDEX_H_\n')

    print(f"  生成: {shared_header}")


def main():
    print("=" * 60)
    print("瓦片动画打包脚本")
    print("=" * 60)

    # 读取 tileset.bin
    tileset_path = Path(GIFS_DIR) / "tileset.bin"
    if not tileset_path.exists():
        print(f"\n错误: 找不到 {tileset_path}")
        sys.exit(1)

    tileset = read_tileset(tileset_path)

    # 收集 idx 文件
    frames_by_anim, total_frames = collect_idx_files(GIFS_DIR)

    if total_frames == 0:
        print("\n错误: 未找到任何 idx 文件")
        sys.exit(1)

    # 打包
    output_path, total_size, anim_frame_counts = pack_animation_frames(
        tileset, frames_by_anim, OUTPUT_FILE
    )

    # 生成头文件
    generate_header_files(tileset, anim_frame_counts, frames_by_anim)

    # 打印统计
    print("\n" + "=" * 60)
    print("打包完成!")
    print("=" * 60)
    print(f"  输出文件: {output_path}")
    print(f"  文件大小: {total_size:,} bytes ({total_size/1024:.1f} KB)")
    print(f"  总帧数:   {total_frames}")
    print(f"  瓦片数:   {tileset['tile_count']}")

    # 对比原始 RGB565 大小
    original_size = total_frames * tileset['width'] * tileset['height'] * 2
    print(f"\n  原始大小 (RGB565): {original_size:,} bytes ({original_size/1024/1024:.2f} MB)")
    print(f"  压缩后大小:        {total_size:,} bytes ({total_size/1024:.1f} KB)")
    print(f"  节省:              {(1 - total_size/original_size)*100:.1f}%")

    print(f"\n" + "=" * 60)
    print("下一步:")
    print("=" * 60)
    print("  1. 运行 'idf.py build'")
    print("  2. 运行 'idf.py flash'")
    print(f"  3. 烧录动画资源:")
    print(f"     esptool.py write_flash 0x800000 {output_path}")


if __name__ == '__main__':
    main()
