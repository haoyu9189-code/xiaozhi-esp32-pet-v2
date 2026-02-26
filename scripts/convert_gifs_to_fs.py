#!/usr/bin/env python3
"""
Convert GIF animations to FS format (4-bit indexed color)
Generates frames.bin with proper palette indexing

File format:
  Header (12 bytes):
    magic[2]: "FS" (0x4653)
    version: uint16 (1)
    width: uint16 (160)
    height: uint16 (128)
    colors: uint8 (up to 32)
    bg_color_idx: uint8 (most common color index)
    frame_count: uint16

  Palette: colors * uint16 (RGB565, little-endian)

  Frame data: frame_count * (width * height / 2) bytes (4-bit indexed)

Usage:
    python convert_gifs_to_fs.py [input_dir] [output_file]
    python convert_gifs_to_fs.py gifs gifs/frames.bin
"""

import os
import sys
import struct
import json
from pathlib import Path
from collections import Counter

try:
    from PIL import Image
    import numpy as np
except ImportError:
    print("Error: Please install dependencies")
    print("Run: pip install Pillow numpy")
    sys.exit(1)


# Configuration
DEFAULT_INPUT_DIR = "gifs"
DEFAULT_OUTPUT_FILE = "gifs/frames.bin"
TARGET_WIDTH = 160
TARGET_HEIGHT = 128
MAX_COLORS = 32


def rgb888_to_rgb565(r, g, b):
    """Convert RGB888 to RGB565"""
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)


def rgb565_to_rgb888(rgb565):
    """Convert RGB565 back to RGB888 for comparison"""
    r = ((rgb565 >> 11) & 0x1F) << 3
    g = ((rgb565 >> 5) & 0x3F) << 2
    b = (rgb565 & 0x1F) << 3
    return (r, g, b)


def quantize_color(r, g, b):
    """Quantize RGB888 to RGB565 precision"""
    rgb565 = rgb888_to_rgb565(r, g, b)
    return rgb565_to_rgb888(rgb565)


def load_gif_frames(gif_path):
    """Load all frames from a GIF file"""
    frames = []
    try:
        img = Image.open(gif_path)
        while True:
            frame = img.convert('RGB')
            if frame.size != (TARGET_WIDTH, TARGET_HEIGHT):
                frame = frame.resize((TARGET_WIDTH, TARGET_HEIGHT), Image.Resampling.LANCZOS)
            frames.append(np.array(frame))
            try:
                img.seek(img.tell() + 1)
            except EOFError:
                break
    except Exception as e:
        print(f"Error loading {gif_path}: {e}")
    return frames


def load_png_sequence(dir_path, prefix):
    """Load PNG sequence from directory"""
    frames = []
    import re

    # Match patterns like Default_dynamic1_1.png or dynamic1_1.png
    pattern = re.compile(rf'(?:Default_)?{prefix}_(\d+)\.png$', re.IGNORECASE)

    dir_p = Path(dir_path)
    matched = []

    for f in dir_p.iterdir():
        match = pattern.match(f.name)
        if match:
            matched.append((int(match.group(1)), f))

    matched.sort(key=lambda x: x[0])

    for _, png_file in matched:
        img = Image.open(png_file).convert('RGB')
        if img.size != (TARGET_WIDTH, TARGET_HEIGHT):
            img = img.resize((TARGET_WIDTH, TARGET_HEIGHT), Image.Resampling.LANCZOS)
        frames.append(np.array(img))

    return frames


def build_palette(all_frames, max_colors=MAX_COLORS):
    """Build optimal palette from all frames"""
    # Count all colors (quantized to RGB565)
    color_counts = Counter()

    for frame in all_frames:
        for y in range(frame.shape[0]):
            for x in range(frame.shape[1]):
                r, g, b = frame[y, x]
                # Quantize to RGB565 precision
                qr, qg, qb = quantize_color(r, g, b)
                rgb565 = rgb888_to_rgb565(qr, qg, qb)
                color_counts[rgb565] += 1

    # Get most common colors
    most_common = color_counts.most_common(max_colors)

    # Build palette - colors ordered by frequency
    palette = [color for color, count in most_common]

    # Pad palette to max_colors if needed
    while len(palette) < max_colors:
        palette.append(0x0000)  # Black padding

    # Find background color (most common)
    bg_color = palette[0]
    bg_idx = 0

    print(f"Palette built: {len(most_common)} unique colors (using {max_colors})")
    print(f"Background color: 0x{bg_color:04X} (index {bg_idx})")

    return palette, bg_idx


def convert_frame_to_indexed(frame, palette):
    """Convert RGB frame to 4-bit indexed using palette"""
    height, width = frame.shape[:2]

    # Build reverse lookup: RGB565 -> palette index
    color_to_idx = {color: idx for idx, color in enumerate(palette)}

    indexed = np.zeros((height, width), dtype=np.uint8)

    for y in range(height):
        for x in range(width):
            r, g, b = frame[y, x]
            qr, qg, qb = quantize_color(r, g, b)
            rgb565 = rgb888_to_rgb565(qr, qg, qb)

            if rgb565 in color_to_idx:
                indexed[y, x] = color_to_idx[rgb565]
            else:
                # Find nearest color (shouldn't happen with proper palette)
                best_idx = 0
                best_dist = float('inf')
                for idx, pcolor in enumerate(palette):
                    pr, pg, pb = rgb565_to_rgb888(pcolor)
                    dist = (qr - pr)**2 + (qg - pg)**2 + (qb - pb)**2
                    if dist < best_dist:
                        best_dist = dist
                        best_idx = idx
                indexed[y, x] = best_idx

    return indexed


def pack_indexed_frame(indexed):
    """Pack indexed frame to 4-bit bytes"""
    height, width = indexed.shape
    data = bytearray()

    for y in range(height):
        for x in range(0, width, 2):
            # Pack two pixels into one byte (high nibble, low nibble)
            hi = indexed[y, x] & 0x0F
            lo = indexed[y, x + 1] & 0x0F if x + 1 < width else 0
            data.append((hi << 4) | lo)

    return bytes(data)


def main():
    input_dir = sys.argv[1] if len(sys.argv) > 1 else DEFAULT_INPUT_DIR
    output_file = sys.argv[2] if len(sys.argv) > 2 else DEFAULT_OUTPUT_FILE

    print("=" * 60)
    print("GIF/PNG to FS Format Converter (4-bit indexed)")
    print("=" * 60)
    print(f"Input: {input_dir}")
    print(f"Output: {output_file}")
    print(f"Target size: {TARGET_WIDTH}x{TARGET_HEIGHT}")
    print(f"Max colors: {MAX_COLORS}")

    input_path = Path(input_dir)
    if not input_path.exists():
        print(f"Error: Input directory not found: {input_dir}")
        sys.exit(1)

    # Load animation definitions from index.json if exists
    index_file = input_path / "index.json"
    animations = []

    if index_file.exists():
        with open(index_file, 'r') as f:
            index_data = json.load(f)
            for anim in index_data.get('animations', []):
                animations.append({
                    'name': anim['name'],
                    'frames': []
                })

    # Collect all frames
    all_frames = []
    anim_info = []

    # Look for GIF files first
    gif_files = sorted(input_path.glob('*.gif'))

    if gif_files:
        print(f"\nLoading from GIF files...")
        for gif_file in gif_files:
            anim_name = gif_file.stem
            frames = load_gif_frames(gif_file)
            if frames:
                start_idx = len(all_frames)
                all_frames.extend(frames)
                anim_info.append({
                    'name': anim_name,
                    'start': start_idx,
                    'count': len(frames)
                })
                print(f"  {anim_name}: {len(frames)} frames")
    else:
        # Look for PNG sequences in subdirectories
        print(f"\nLoading from PNG sequences...")
        subdirs = [d for d in input_path.iterdir() if d.is_dir()]

        for subdir in sorted(subdirs):
            prefix = subdir.name
            frames = load_png_sequence(subdir, prefix)
            if not frames:
                # Try loading directly with directory name as pattern
                frames = load_png_sequence(input_path, prefix)

            if frames:
                start_idx = len(all_frames)
                all_frames.extend(frames)
                anim_info.append({
                    'name': f"Default_{prefix}",
                    'start': start_idx,
                    'count': len(frames)
                })
                print(f"  {prefix}: {len(frames)} frames")

    if not all_frames:
        print("\nError: No frames found!")
        print("Please place GIF files or PNG sequences in the input directory.")
        sys.exit(1)

    print(f"\nTotal frames: {len(all_frames)}")

    # Build palette from all frames
    print("\nBuilding palette...")
    palette, bg_idx = build_palette(all_frames)

    # Convert all frames to indexed format
    print("\nConverting frames to indexed format...")
    indexed_frames = []

    for i, frame in enumerate(all_frames):
        indexed = convert_frame_to_indexed(frame, palette)
        indexed_frames.append(indexed)
        if (i + 1) % 50 == 0 or i == len(all_frames) - 1:
            print(f"  Progress: {i + 1}/{len(all_frames)}")

    # Write output file
    print(f"\nWriting {output_file}...")

    with open(output_file, 'wb') as f:
        # Write header
        f.write(b'FS')  # Magic
        f.write(struct.pack('<H', 1))  # Version
        f.write(struct.pack('<H', TARGET_WIDTH))  # Width
        f.write(struct.pack('<H', TARGET_HEIGHT))  # Height
        f.write(struct.pack('B', len(palette)))  # Colors
        f.write(struct.pack('B', bg_idx))  # BG color index
        f.write(struct.pack('<H', len(indexed_frames)))  # Frame count

        # Write palette
        for color in palette:
            f.write(struct.pack('<H', color))  # Little-endian RGB565

        # Write frame data
        for indexed in indexed_frames:
            packed = pack_indexed_frame(indexed)
            f.write(packed)

    file_size = os.path.getsize(output_file)
    frame_size = TARGET_WIDTH * TARGET_HEIGHT // 2  # 4-bit per pixel

    print(f"\n{'=' * 60}")
    print("Conversion complete!")
    print(f"{'=' * 60}")
    print(f"  Output file: {output_file}")
    print(f"  File size: {file_size:,} bytes ({file_size/1024:.1f} KB)")
    print(f"  Dimensions: {TARGET_WIDTH}x{TARGET_HEIGHT}")
    print(f"  Colors: {len(palette)}")
    print(f"  Frames: {len(indexed_frames)}")
    print(f"  Frame size: {frame_size} bytes")

    # Update index.json
    index_output = {
        'width': TARGET_WIDTH,
        'height': TARGET_HEIGHT,
        'colors': len(palette),
        'bg_color_idx': bg_idx,
        'total_frames': len(indexed_frames),
        'frame_size': frame_size,
        'animations': anim_info
    }

    index_json_path = input_path / "index.json"
    with open(index_json_path, 'w') as f:
        json.dump(index_output, f, indent=2)
    print(f"\n  Updated: {index_json_path}")

    print(f"\nFlash command:")
    print(f"  esptool.py write_flash 0x800000 {output_file}")


if __name__ == '__main__':
    main()
