#!/usr/bin/env python3
"""
自动更新 GIF 配置脚本
从 gifs/index.json 读取配置，更新代码中的硬编码值：
- 背景色范围 (BG_R_MIN/MAX, BG_G_MIN/MAX, BG_B_MIN/MAX)
- 动画帧数 (ANIM_FRAME_COUNT)
- 背景数量 (BG_COUNT)

用法：python update_bg_color.py
"""

import json
import re
import os

# 文件路径
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
INDEX_JSON = os.path.join(SCRIPT_DIR, "gifs", "index.json")
BOARD_FILE = os.path.join(SCRIPT_DIR, "main", "boards", "waveshare-c6-lcd-1.69", "esp32-c6-lcd-1.69.cc")
ANIM_LOADER_H = os.path.join(SCRIPT_DIR, "main", "images", "animation_loader.h")
BG_MANAGER_H = os.path.join(SCRIPT_DIR, "main", "images", "background_manager.h")

def rgb888_to_rgb565_components(r, g, b):
    """将 RGB888 转换为 RGB565 的各分量"""
    r5 = r >> 3   # 8-bit -> 5-bit
    g6 = g >> 2   # 8-bit -> 6-bit
    b5 = b >> 3   # 8-bit -> 5-bit
    return r5, g6, b5

def update_file(filepath, replacements):
    """更新文件中的定义"""
    with open(filepath, 'r', encoding='utf-8') as f:
        content = f.read()

    updated = False
    for pattern, replacement in replacements:
        new_content, count = re.subn(pattern, replacement, content)
        if count > 0:
            content = new_content
            updated = True

    if updated:
        with open(filepath, 'w', encoding='utf-8') as f:
            f.write(content)

    return updated


def main():
    # 读取 index.json
    print(f"读取 {INDEX_JSON}...")
    with open(INDEX_JSON, 'r', encoding='utf-8') as f:
        data = json.load(f)

    # ========== 1. 更新动画帧数 ==========
    total_frames = data.get('total_frames', 0)
    if total_frames > 0:
        print(f"\n[1/3] 动画帧数: {total_frames}")
        if update_file(ANIM_LOADER_H, [
            (r'#define ANIM_FRAME_COUNT\s+\d+', f'#define ANIM_FRAME_COUNT        {total_frames}'),
            (r'#define ANIM_TOTAL_FRAMES\s+\d+', f'#define ANIM_TOTAL_FRAMES       {total_frames}'),
        ]):
            print(f"  已更新 animation_loader.h")
        else:
            print(f"  animation_loader.h 无需更新")
    else:
        print("\n[1/3] 跳过：未找到 total_frames")

    # ========== 2. 更新背景数量 ==========
    backgrounds = data.get('backgrounds', [])
    bg_count = len(backgrounds)
    if bg_count > 0:
        print(f"\n[2/3] 背景数量: {bg_count}")
        # 注意：BG_COUNT 在 enum 中，格式不同
        if update_file(BG_MANAGER_H, [
            (r'BG_COUNT\s*=\s*\d+', f'BG_COUNT = {bg_count}'),
        ]):
            print(f"  已更新 background_manager.h")
        else:
            print(f"  background_manager.h 无需更新")
    else:
        print("\n[2/3] 跳过：未找到 backgrounds")

    # ========== 3. 更新背景色范围 ==========
    frames = data.get('frames', [])
    if not frames:
        print("\n[3/3] 跳过：未找到 frames 数据")
        return 0

    r_values = []
    g_values = []
    b_values = []

    for frame in frames:
        bg_color = frame.get('bg_color_rgb', [])
        if len(bg_color) >= 3:
            r_values.append(bg_color[0])
            g_values.append(bg_color[1])
            b_values.append(bg_color[2])

    if not r_values:
        print("\n[3/3] 跳过：没有有效的 bg_color_rgb")
        return 0

    # 计算 RGB888 范围（添加容差 ±5）
    tolerance = 5
    r_min_888 = max(0, min(r_values) - tolerance)
    r_max_888 = min(255, max(r_values) + tolerance)
    g_min_888 = max(0, min(g_values) - tolerance)
    g_max_888 = min(255, max(g_values) + tolerance)
    b_min_888 = max(0, min(b_values) - tolerance)
    b_max_888 = min(255, max(b_values) + tolerance)

    # 转换为 RGB565 分量
    r_min_5, _, _ = rgb888_to_rgb565_components(r_min_888, 0, 0)
    r_max_5, _, _ = rgb888_to_rgb565_components(r_max_888, 0, 0)
    _, g_min_6, _ = rgb888_to_rgb565_components(0, g_min_888, 0)
    _, g_max_6, _ = rgb888_to_rgb565_components(0, g_max_888, 0)
    _, _, b_min_5 = rgb888_to_rgb565_components(0, 0, b_min_888)
    _, _, b_max_5 = rgb888_to_rgb565_components(0, 0, b_max_888)

    print(f"\n[3/3] 背景色范围（{len(r_values)} 帧）:")
    print(f"  原始 RGB: R[{min(r_values)}-{max(r_values)}] G[{min(g_values)}-{max(g_values)}] B[{min(b_values)}-{max(b_values)}]")
    print(f"  RGB565:   R[{r_min_5}-{r_max_5}] G[{g_min_6}-{g_max_6}] B[{b_min_5}-{b_max_5}]")

    if update_file(BOARD_FILE, [
        (r'#define BG_R_MIN\s+\d+', f'#define BG_R_MIN  {r_min_5}'),
        (r'#define BG_R_MAX\s+\d+', f'#define BG_R_MAX  {r_max_5}'),
        (r'#define BG_G_MIN\s+\d+', f'#define BG_G_MIN  {g_min_6}'),
        (r'#define BG_G_MAX\s+\d+', f'#define BG_G_MAX  {g_max_6}'),
        (r'#define BG_B_MIN\s+\d+', f'#define BG_B_MIN  {b_min_5}'),
        (r'#define BG_B_MAX\s+\d+', f'#define BG_B_MAX  {b_max_5}'),
    ]):
        print(f"  已更新 esp32-c6-lcd-1.69.cc")
    else:
        print(f"  esp32-c6-lcd-1.69.cc 无需更新")

    print("\n========================================")
    print("配置更新完成！请重新编译固件:")
    print("  idf.py build")
    print("========================================")

    return 0

if __name__ == "__main__":
    exit(main())
