#!/usr/bin/env python3
"""
PNG 图片序列转 RGB565 二进制 - 去重+Asset打包版本
1. 分析所有帧的相似度，只保留唯一帧
2. 生成二进制帧文件
3. 生成C头文件（只包含索引，不包含数据）
4. 生成asset打包文件
5. 自动读取wifi/wifi.txt并更新板级代码中的WiFi配置

使用方法:
    python png_to_assets_dedup.py [相似度阈值] [保留帧数]
    python png_to_assets_dedup.py 0.97
    python png_to_assets_dedup.py 0.998 8
    python png_to_assets_dedup.py 0 8      # 极限去重: 首帧共用, 其他帧不去重

WiFi配置文件格式 (wifi/wifi.txt):
    第一行: SSID
    第二行: 密码
"""

import os
import sys
import struct
import re
from pathlib import Path

try:
    from PIL import Image
    import numpy as np
except ImportError:
    print("错误: 请先安装依赖")
    print("运行: pip install Pillow numpy")
    sys.exit(1)


# 配置 - 4个动画
ANIMATIONS = [
    ("gifs/dynamic1", "dynamic1", True),   # (input_dir, name, is_dynamic) Speaking
    ("gifs/dynamic2", "dynamic2", True),   # Listening
    ("gifs/static1",  "static1",  False),  # Idle/Breathing (静态动画)
    ("gifs/dynamic3", "dynamic3", True),   # Touch interaction
]

# 特殊帧选取规则（帧号从1开始）
# 如果动画名称包含key，则使用指定的帧号列表
SPECIAL_FRAME_RULES = {
    'dynamic3': [1, 3, 5, 7, 9, 10, 11, 13],    # dynamic3：取指定帧
}

OUTPUT_DIR = "main/images"
ASSETS_DIR = "main/images/shared"  # 直接输出到正确位置

# WiFi配置
WIFI_CONFIG_FILE = "wifi/wifi.txt"
WIFI_BIN_FILE = "main/images/shared/wifi_config.bin"  # WiFi配置二进制文件
WIFI_FLASH_ADDR = "0x7F0000"  # WiFi配置烧录地址


def generate_wifi_config_bin():
    """生成WiFi配置二进制文件（用于直接烧录，无需重新编译）"""
    wifi_file = Path(WIFI_CONFIG_FILE)

    if not wifi_file.exists():
        print(f"\n[WiFi] 跳过: {WIFI_CONFIG_FILE} 不存在")
        return None

    # 读取WiFi配置
    try:
        with open(wifi_file, 'r', encoding='utf-8') as f:
            lines = f.read().strip().split('\n')
            if len(lines) < 2:
                print(f"\n[WiFi] 错误: {WIFI_CONFIG_FILE} 格式不正确，需要两行(SSID和密码)")
                return None
            ssid = lines[0].strip()
            password = lines[1].strip()

        if not ssid:
            print(f"\n[WiFi] 跳过: SSID为空")
            return None

    except Exception as e:
        print(f"\n[WiFi] 读取配置文件失败: {e}")
        return None

    # 生成二进制文件
    # 格式: Magic(4) + Version(2) + SSID_len(1) + SSID(32) + PWD_len(1) + PWD(64) = 104 bytes
    try:
        output_dir = Path(WIFI_BIN_FILE).parent
        output_dir.mkdir(parents=True, exist_ok=True)

        with open(WIFI_BIN_FILE, 'wb') as f:
            # Magic: "WIFI"
            f.write(b'WIFI')
            # Version: 1
            f.write(struct.pack('<H', 1))
            # SSID length
            ssid_bytes = ssid.encode('utf-8')[:32]
            f.write(struct.pack('B', len(ssid_bytes)))
            # SSID (32 bytes, null-padded)
            f.write(ssid_bytes.ljust(32, b'\x00'))
            # Password length
            pwd_bytes = password.encode('utf-8')[:64]
            f.write(struct.pack('B', len(pwd_bytes)))
            # Password (64 bytes, null-padded)
            f.write(pwd_bytes.ljust(64, b'\x00'))

        print(f"\n[WiFi] 生成配置文件: {WIFI_BIN_FILE}")
        print(f"[WiFi]   SSID: {ssid}")
        print(f"[WiFi]   Password: {'*' * len(password)}")
        print(f"[WiFi]   烧录地址: {WIFI_FLASH_ADDR}")
        return WIFI_BIN_FILE

    except Exception as e:
        print(f"\n[WiFi] 生成配置文件失败: {e}")
        return None


def natural_sort_key(path):
    """自然排序key函数，按文件名中的数字排序"""
    # 提取文件名中的数字
    import re
    name = path.stem if hasattr(path, 'stem') else str(path)
    # 找到文件名末尾的数字 (如 Default_static1_10 -> 10)
    match = re.search(r'_(\d+)$', name)
    if match:
        return int(match.group(1))
    return 0


def select_frames_by_rule(png_files, anim_name, max_frames):
    """根据动画名称选择帧

    Args:
        png_files: 已排序的PNG文件列表
        anim_name: 动画名称
        max_frames: 默认最大帧数

    Returns:
        选中的帧文件列表
    """
    # 检查是否有特殊规则
    for rule_name, frame_numbers in SPECIAL_FRAME_RULES.items():
        if rule_name in anim_name.lower():
            # 帧号从1开始，转换为索引（从0开始）
            selected = []
            for frame_num in frame_numbers:
                idx = frame_num - 1
                if idx < len(png_files):
                    selected.append(png_files[idx])
            return selected

    # 默认规则：取前N帧
    return png_files[:max_frames]


def rgb888_to_rgb565(r, g, b):
    """RGB888 转 RGB565 (16位色)"""
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)


def load_image_as_array(path):
    """加载图片并转换为numpy数组"""
    img = Image.open(path).convert('RGB')
    return np.array(img), img.size


def enhance_dark_details(img_array, gamma=0.55, shadow_boost=2.0):
    """
    增强暗部细节
    - gamma < 1: 提亮暗部，保留亮部
    - shadow_boost: 暗部对比度增强系数
    """
    pixels = img_array.astype(np.float32) / 255.0

    # Gamma 校正 - 提亮暗部
    pixels = np.power(pixels, gamma)

    # 暗部对比度增强 (只影响暗部区域)
    # 对亮度低于0.5的区域增强对比度
    luminance = 0.299 * pixels[:,:,0] + 0.587 * pixels[:,:,1] + 0.114 * pixels[:,:,2]
    shadow_mask = np.clip(1.0 - luminance * 2, 0, 1)  # 暗部权重
    shadow_mask = shadow_mask[:,:,np.newaxis]

    # 增强暗部对比度
    mid = 0.25  # 暗部中心点
    enhanced = mid + (pixels - mid) * (1 + (shadow_boost - 1) * shadow_mask)

    # 限制范围
    enhanced = np.clip(enhanced * 255, 0, 255).astype(np.uint8)
    return enhanced


def apply_floyd_steinberg_dither(img_array):
    """
    Floyd-Steinberg 抖动算法
    通过误差扩散使 RGB565 颜色过渡更平滑，减少色带
    """
    height, width, _ = img_array.shape
    # 使用 float32 避免溢出
    pixels = img_array.astype(np.float32).copy()

    for y in range(height):
        for x in range(width):
            old_r, old_g, old_b = pixels[y, x]

            # 量化到 RGB565 色深 (R:5bit, G:6bit, B:5bit)
            new_r = round(old_r / 255 * 31) * 255 / 31
            new_g = round(old_g / 255 * 63) * 255 / 63
            new_b = round(old_b / 255 * 31) * 255 / 31

            pixels[y, x] = [new_r, new_g, new_b]

            # 计算量化误差
            err_r = old_r - new_r
            err_g = old_g - new_g
            err_b = old_b - new_b

            # 扩散误差到相邻像素 (Floyd-Steinberg 系数)
            #       * 7/16
            # 3/16 5/16 1/16
            if x + 1 < width:
                pixels[y, x + 1] += [err_r * 7/16, err_g * 7/16, err_b * 7/16]
            if y + 1 < height:
                if x > 0:
                    pixels[y + 1, x - 1] += [err_r * 3/16, err_g * 3/16, err_b * 3/16]
                pixels[y + 1, x] += [err_r * 5/16, err_g * 5/16, err_b * 5/16]
                if x + 1 < width:
                    pixels[y + 1, x + 1] += [err_r * 1/16, err_g * 1/16, err_b * 1/16]

    # 限制到有效范围并转回 uint8
    return np.clip(pixels, 0, 255).astype(np.uint8)


def calculate_similarity(arr1, arr2):
    """计算两张图片的相似度 (0-1)"""
    if arr1.shape != arr2.shape:
        return 0.0
    diff = arr1.astype(np.float32) - arr2.astype(np.float32)
    mse = np.mean(diff ** 2)
    similarity = np.exp(-mse / 5000)
    return similarity


def image_to_rgb565_bytes(img_array, use_dither=False, enhance_shadows=True):
    """将图片数组转换为RGB565字节数组"""
    # 增强暗部细节（针对真实照片）
    if enhance_shadows:
        img_array = enhance_dark_details(img_array)

    # 应用抖动算法减少色带
    if use_dither:
        img_array = apply_floyd_steinberg_dither(img_array)

    height, width, _ = img_array.shape
    data = bytearray()
    for y in range(height):
        for x in range(width):
            r, g, b = img_array[y, x]
            pixel = rgb888_to_rgb565(int(r), int(g), int(b))
            data.append(pixel & 0xFF)
            data.append((pixel >> 8) & 0xFF)
    return bytes(data)


def find_unique_frames(all_frames, threshold=0.90):
    """找出所有唯一帧"""
    print(f"\n分析 {len(all_frames)} 张图片的相似度 (阈值: {threshold*100:.0f}%)...")

    images = {}
    for path in all_frames:
        arr, size = load_image_as_array(path)
        images[path] = arr

    unique_frames = []
    frame_mapping = {}

    for path, arr in images.items():
        found_match = False
        for idx, (unique_path, unique_arr) in enumerate(unique_frames):
            similarity = calculate_similarity(arr, unique_arr)
            if similarity >= threshold:
                frame_mapping[path] = idx
                found_match = True
                print(f"  {os.path.basename(path)} -> 复用帧 #{idx+1} (相似度: {similarity*100:.1f}%)")
                break

        if not found_match:
            frame_mapping[path] = len(unique_frames)
            unique_frames.append((path, arr))
            print(f"  {os.path.basename(path)} -> 新帧 #{len(unique_frames)}")

    print(f"\n去重结果: {len(all_frames)} 帧 -> {len(unique_frames)} 唯一帧")
    return unique_frames, frame_mapping


def find_unique_frames_extreme(all_frames, anim_frames):
    """极限去重模式：所有动画共用首帧，其他帧不去重"""
    print(f"\n极限去重模式: {len(all_frames)} 张图片...")
    print("  规则: 所有动画共用首帧，其他帧各自独立")

    images = {}
    for path in all_frames:
        arr, size = load_image_as_array(path)
        images[path] = arr

    unique_frames = []
    frame_mapping = {}

    # 找出所有动画的首帧路径
    first_frame_paths = set()
    for anim_name, anim_data in anim_frames.items():
        if anim_data['files']:
            first_frame_paths.add(anim_data['files'][0])

    # 处理首帧：所有动画的首帧共用索引0
    first_frame_added = False
    for path in all_frames:
        if path in first_frame_paths:
            if not first_frame_added:
                # 第一个首帧，添加到唯一帧列表
                unique_frames.append((path, images[path]))
                frame_mapping[path] = 0
                print(f"  {os.path.basename(path)} -> 共享首帧 #1")
                first_frame_added = True
            else:
                # 其他动画的首帧，复用索引0
                frame_mapping[path] = 0
                print(f"  {os.path.basename(path)} -> 复用首帧 #1")
        else:
            # 非首帧，各自独立
            frame_mapping[path] = len(unique_frames)
            unique_frames.append((path, images[path]))
            print(f"  {os.path.basename(path)} -> 新帧 #{len(unique_frames)}")

    print(f"\n极限去重结果: {len(all_frames)} 帧 -> {len(unique_frames)} 唯一帧")
    print(f"  (节省了 {len(first_frame_paths) - 1} 个首帧)")
    return unique_frames, frame_mapping


def generate_header_file(anim_name, prefix, frame_indices, bounce_indices,
                         unique_count, width, height, output_dir):
    """生成动画索引头文件（只包含索引，不包含数据）"""
    index_file = Path(output_dir) / f"{prefix}_index.h"

    with open(index_file, 'w', encoding='utf-8') as f:
        f.write(f'// Auto-generated animation index file (frames stored in assets partition)\n')
        f.write(f'// Animation: {anim_name}\n')
        f.write(f'// Total frames: {len(frame_indices)} (references to {unique_count} unique frames)\n')
        f.write(f'// Image size: {width} x {height}\n')
        f.write(f'// Format: RGB565 Little Endian (for LVGL)\n\n')
        f.write(f'#ifndef _{prefix.upper()}_INDEX_H_\n')
        f.write(f'#define _{prefix.upper()}_INDEX_H_\n\n')
        f.write(f'#include <stdint.h>\n\n')

        # 图片尺寸定义
        f.write(f'// 图片尺寸\n')
        f.write(f'#define {prefix.upper()}_WIDTH  {width}\n')
        f.write(f'#define {prefix.upper()}_HEIGHT {height}\n\n')

        # 帧索引数组（索引到唯一帧）
        f.write(f'// 帧索引数组 - 正向播放 (索引到唯一帧编号)\n')
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
        f.write(f'#define {prefix.upper()}_FRAME_COUNT {len(frame_indices)}\n\n')

        # 往返播放索引
        if len(bounce_indices) > 0:
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

    return str(index_file)


def generate_shared_header(unique_count, width, height, output_dir):
    """生成共享帧信息头文件"""
    shared_dir = Path(output_dir) / "shared"
    shared_dir.mkdir(parents=True, exist_ok=True)

    index_file = shared_dir / "gFrame_index.h"
    with open(index_file, 'w', encoding='utf-8') as f:
        f.write(f'// Auto-generated shared frames info (data stored in assets partition)\n')
        f.write(f'// Total unique frames: {unique_count}\n')
        f.write(f'// Image size: {width} x {height}\n\n')
        f.write(f'#ifndef _GFRAME_INDEX_H_\n')
        f.write(f'#define _GFRAME_INDEX_H_\n\n')
        f.write(f'#include <stdint.h>\n\n')
        f.write(f'// Shared frame dimensions\n')
        f.write(f'#define GFRAME_WIDTH  {width}\n')
        f.write(f'#define GFRAME_HEIGHT {height}\n')
        f.write(f'#define GFRAME_SIZE   ({width} * {height} * 2)\n')
        f.write(f'#define GFRAME_UNIQUE_COUNT {unique_count}\n\n')
        f.write(f'// Frame data is stored in assets partition\n')
        f.write(f'// Use AnimationLoader to access frame data\n\n')
        f.write(f'#endif // _GFRAME_INDEX_H_\n')


def pack_assets(unique_frames, width, height, output_dir):
    """打包所有唯一帧到assets二进制文件"""
    assets_dir = Path(output_dir)
    assets_dir.mkdir(parents=True, exist_ok=True)

    # 帧数据文件
    frames_file = assets_dir / "animation_frames.bin"

    frame_size = width * height * 2

    print(f"\n生成帧数据文件...")
    with open(frames_file, 'wb') as f:
        # 写入头部
        # Magic: "ANIM"
        f.write(b'ANIM')
        # Version: 1
        f.write(struct.pack('<H', 1))
        # Frame count
        f.write(struct.pack('<H', len(unique_frames)))
        # Width, Height
        f.write(struct.pack('<HH', width, height))
        # Frame size
        f.write(struct.pack('<I', frame_size))
        # Reserved (padding to 16 bytes)
        f.write(b'\x00' * 2)

        # 写入帧数据
        for i, (path, arr) in enumerate(unique_frames):
            frame_data = image_to_rgb565_bytes(arr)
            f.write(frame_data)
            print(f"  帧 #{i+1}: {len(frame_data):,} bytes")

    total_size = os.path.getsize(frames_file)
    print(f"\n帧数据文件: {frames_file}")
    print(f"  总大小: {total_size:,} bytes ({total_size/1024/1024:.2f} MB)")

    return str(frames_file)


def organize_gifs_directory():
    """整理gifs目录，将散落的PNG文件按动画名称分类到子目录"""
    gifs_dir = Path("gifs")
    if not gifs_dir.exists():
        return False

    # 匹配文件名格式: Default_dynamic1_1.png 或 dynamic1_1.png
    pattern = re.compile(r'^(?:Default_)?(dynamic\d+|static\d+)_(\d+)\.png$', re.IGNORECASE)

    # 扫描gifs目录下的所有PNG文件 (去重，Windows不区分大小写)
    png_files = list(gifs_dir.glob('*.png'))
    png_files = list({f.resolve(): f for f in png_files if f.is_file()}.values())

    if not png_files:
        return False

    # 检查是否有需要整理的文件
    files_to_organize = []
    for png_file in png_files:
        match = pattern.match(png_file.name)
        if match:
            files_to_organize.append((png_file, match.group(1), int(match.group(2))))

    if not files_to_organize:
        print("gifs 根目录无需整理的文件，跳过目录整理步骤")
        return False

    print("=" * 60)
    print("整理 gifs 目录结构")
    print("=" * 60)
    print(f"发现 {len(files_to_organize)} 个需要整理的文件")

    # 按动画名称分组
    anim_files = {}
    for png_file, anim_name, frame_num in files_to_organize:
        anim_name_lower = anim_name.lower()
        if anim_name_lower not in anim_files:
            anim_files[anim_name_lower] = []
        anim_files[anim_name_lower].append((png_file, frame_num))

    # 创建子目录并移动文件（保留所有帧，不限制数量）
    MAX_FRAMES_PER_ANIM = 999  # 不限制帧数
    import shutil
    for anim_name, files in anim_files.items():
        # 创建子目录
        sub_dir = gifs_dir / anim_name
        sub_dir.mkdir(exist_ok=True)

        # 按帧序号排序
        files.sort(key=lambda x: x[1])

        # 只保留前9张
        files_to_keep = files[:MAX_FRAMES_PER_ANIM]
        files_to_delete = files[MAX_FRAMES_PER_ANIM:]

        print(f"\n  {anim_name}/: 保留 {len(files_to_keep)} 帧 (共 {len(files)} 帧)")

        # 移动保留的文件（保留原始文件名）
        for i, (png_file, frame_num) in enumerate(files_to_keep, start=1):
            new_path = sub_dir / png_file.name

            # 移动文件
            shutil.move(str(png_file), str(new_path))
            print(f"    {png_file.name} -> {anim_name}/{png_file.name}")

        # 删除多余的文件
        for png_file, frame_num in files_to_delete:
            png_file.unlink()
            print(f"    {png_file.name} -> 已删除")

    print(f"\n目录整理完成!")
    return True


def main():
    threshold = 0.90
    max_frames = 8  # 默认保留前8帧

    if len(sys.argv) > 1:
        try:
            threshold = float(sys.argv[1])
            if threshold != 0 and (threshold < 0.5 or threshold > 1.0):
                print("错误: 阈值应为0(极限去重)或在0.5-1.0之间")
                sys.exit(1)
        except ValueError:
            print(f"错误: 无效的阈值 '{sys.argv[1]}'")
            sys.exit(1)

    if len(sys.argv) > 2:
        try:
            max_frames = int(sys.argv[2])
            if max_frames < 1:
                print("错误: 保留帧数应大于0")
                sys.exit(1)
        except ValueError:
            print(f"错误: 无效的保留帧数 '{sys.argv[2]}'")
            sys.exit(1)

    # 首先整理gifs目录
    organize_gifs_directory()

    print("\n" + "=" * 60)
    print("PNG 序列转 RGB565 - 去重+Asset打包版本")
    print("=" * 60)
    if threshold == 0:
        print("去重模式: 极限去重 (所有动画共用首帧)")
    else:
        print(f"相似度阈值: {threshold*100:.0f}%")
    print(f"每动画保留帧数: {max_frames}")

    # 收集所有PNG文件
    all_frames = []
    anim_frames = {}

    for input_dir, anim_name, is_dynamic in ANIMATIONS:
        input_path = Path(input_dir)
        if not input_path.exists():
            print(f"跳过: 目录不存在 - {input_dir}")
            continue

        png_files = list(input_path.glob('*.png')) + list(input_path.glob('*.PNG'))
        png_files = list(set(png_files))
        png_files = sorted(png_files, key=natural_sort_key)  # 按数字自然排序
        png_files = select_frames_by_rule(png_files, anim_name, max_frames)  # 根据规则选帧

        if png_files:
            anim_frames[anim_name] = {
                'files': [str(p) for p in png_files],
                'is_dynamic': is_dynamic
            }
            all_frames.extend(anim_frames[anim_name]['files'])
            # 显示使用的规则
            rule_used = None
            for rule_name in SPECIAL_FRAME_RULES:
                if rule_name in anim_name.lower():
                    rule_used = SPECIAL_FRAME_RULES[rule_name]
                    break
            if rule_used:
                print(f"  {anim_name}: {len(png_files)} 帧 (特殊规则: 取帧 {rule_used})")
            else:
                print(f"  {anim_name}: {len(png_files)} 帧")

    if not all_frames:
        print("错误: 未找到任何PNG文件")
        sys.exit(1)

    print(f"\n总计: {len(all_frames)} 帧")

    # 分析相似度并找出唯一帧
    if threshold == 0:
        # 极限去重模式：首帧共用
        unique_frames, frame_mapping = find_unique_frames_extreme(all_frames, anim_frames)
    else:
        unique_frames, frame_mapping = find_unique_frames(all_frames, threshold)

    # 获取图片尺寸
    _, (width, height) = load_image_as_array(all_frames[0])

    # 生成共享帧信息头文件
    generate_shared_header(len(unique_frames), width, height, OUTPUT_DIR)

    # 为每个动画生成索引头文件
    print(f"\n生成动画索引头文件...")
    for input_dir, anim_name, is_dynamic in ANIMATIONS:
        if anim_name not in anim_frames:
            continue

        output_dir = Path(OUTPUT_DIR) / anim_name
        output_dir.mkdir(parents=True, exist_ok=True)

        # 清理旧文件
        for old_file in output_dir.glob("*.h"):
            old_file.unlink()

        # 构建帧索引
        frame_indices = [frame_mapping[p] for p in anim_frames[anim_name]['files']]

        # 构建往返播放索引 (0-7-0, 完整往返循环)
        bounce_indices = frame_indices.copy()
        if len(frame_indices) > 1:
            bounce_indices.extend(reversed(frame_indices[:-1]))

        prefix = f"gImage_{anim_name}"
        generate_header_file(
            anim_name, prefix, frame_indices, bounce_indices,
            len(unique_frames), width, height, str(output_dir)
        )
        print(f"  生成: {anim_name}/{prefix}_index.h")

    # 打包到assets
    frames_file = pack_assets(unique_frames, width, height, ASSETS_DIR)

    # 打印统计
    frame_size = width * height * 2
    total_size = len(unique_frames) * frame_size
    original_size = len(all_frames) * frame_size

    print("\n" + "=" * 60)
    print("转换完成!")
    print("=" * 60)
    print(f"  动画帧数: {len(all_frames)}")
    print(f"  唯一帧数: {len(unique_frames)}")
    if len(all_frames) > 0:
        print(f"  去重比例: {(1 - len(unique_frames)/len(all_frames))*100:.1f}%")
    print(f"  图片尺寸: {width} x {height}")
    print(f"  每帧大小: {frame_size:,} bytes ({frame_size/1024:.1f} KB)")
    print(f"\n  原始大小: {original_size:,} bytes ({original_size/1024/1024:.2f} MB)")
    print(f"  压缩后:   {total_size:,} bytes ({total_size/1024/1024:.2f} MB)")
    print(f"  节省:     {(1 - total_size/original_size)*100:.1f}%")
    print(f"\n  帧数据文件: {frames_file}")

    # 生成WiFi配置二进制文件
    wifi_file = generate_wifi_config_bin()

    print(f"\n" + "=" * 60)
    print("下一步:")
    print("=" * 60)
    print(f"  1. 运行 'idf.py build'")
    print(f"  2. 运行 'idf.py flash'")
    if wifi_file:
        print(f"  3. 烧录动画+WiFi资源:")
        print(f"     esptool.py write_flash {WIFI_FLASH_ADDR} {wifi_file} 0x800000 {frames_file}")
    else:
        print(f"  3. 烧录动画资源:")
        print(f"     esptool.py write_flash 0x800000 {frames_file}")

    print(f"\n" + "=" * 60)
    print("参数说明:")
    print("=" * 60)
    print("  python png_to_assets_dedup.py [阈值] [帧数]")
    print("  ")
    print("  阈值:")
    print("    0        极限去重模式 (首帧共用, 其他帧不去重)")
    print("    0.5-1.0  相似度去重 (按阈值合并相似帧)")
    print("  ")
    print("  帧数: 每个动画保留的最大帧数 (默认8)")

    print(f"\n" + "=" * 60)
    print("极限去重模式 (用于后续远程换GIF/WiFi):")
    print("=" * 60)
    print("  首次完整烧录:")
    print("  步骤一: python scripts/png_to_assets_dedup.py 0 8")
    print("  步骤二: idf.py build && idf.py flash")
    if wifi_file:
        print(f"  步骤三: esptool.py write_flash {WIFI_FLASH_ADDR} {wifi_file} 0x800000 {frames_file}")
    else:
        print(f"  步骤三: esptool.py write_flash 0x800000 {frames_file}")
    print("  ")
    print("  后续换图/换WiFi (无需重新编译):")
    print("  步骤一: python scripts/png_to_assets_dedup.py 0 8")
    if wifi_file:
        print(f"  步骤二: esptool.py write_flash {WIFI_FLASH_ADDR} {wifi_file} 0x800000 {frames_file}")
    else:
        print(f"  步骤二: esptool.py write_flash 0x800000 {frames_file}")


if __name__ == '__main__':
    main()
