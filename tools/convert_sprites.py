#!/usr/bin/env python3
"""
Convert Loki theme PNGs to RGB565 BMP files for the CYD SD card.

Usage:
    python3 convert_sprites.py <loki_theme_dir> <output_dir> [--size 80]

Creates SD card directory structure:
    /loki/
        idle1.bmp, idle2.bmp, idle3.bmp, idle4.bmp
        scan1.bmp, scan2.bmp, scan3.bmp, scan4.bmp
        attack1.bmp, attack2.bmp, attack3.bmp, attack4.bmp
        steal1.bmp, steal2.bmp, steal3.bmp, steal4.bmp
        icons/
            target.bmp, port.bmp, cred.bmp, data.bmp, vuln.bmp, zombie.bmp
"""

import os
import sys
import struct
from PIL import Image

def rgb888_to_rgb565(r, g, b):
    """Convert 24-bit RGB to 16-bit RGB565."""
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)

def png_to_rgb565_bmp(input_path, output_path, size, bg_color=(255, 0, 255)):
    """Convert a PNG with transparency to RGB565 BMP.

    Semi-transparent edge pixels are blended into a dark background (10,18,10)
    so they look clean against dark themes. Only fully transparent pixels
    become the magenta transparency key.
    """
    img = Image.open(input_path).convert("RGBA")

    # Resize maintaining aspect ratio
    img.thumbnail((size, size), Image.LANCZOS)

    # Dark background color matching the Loki theme bg
    dark_bg = (10, 18, 10)

    # Create the output image at final size
    result = Image.new("RGB", (size, size), bg_color)

    # Composite character onto dark background first (blends edges properly)
    dark_layer = Image.new("RGBA", (size, size), (*dark_bg, 255))
    offset_x = (size - img.width) // 2
    offset_y = (size - img.height) // 2
    dark_layer.paste(img, (offset_x, offset_y), img)

    # Now go pixel by pixel: fully transparent -> magenta key, otherwise -> blended color
    dark_pixels = dark_layer.load()
    img_padded = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    img_padded.paste(img, (offset_x, offset_y), img)
    alpha_pixels = img_padded.load()
    result_pixels = result.load()

    for y in range(size):
        for x in range(size):
            a = alpha_pixels[x, y][3]
            if a == 0:
                # Fully transparent -> magenta key
                result_pixels[x, y] = bg_color
            else:
                # Has some opacity -> use the dark-blended color (no fringe)
                result_pixels[x, y] = (dark_pixels[x, y][0], dark_pixels[x, y][1], dark_pixels[x, y][2])

    rgb = result

    # Write BMP with RGB565 (BI_BITFIELDS)
    width, height = rgb.size
    row_size = ((width * 2 + 3) // 4) * 4  # Pad rows to 4-byte boundary
    pixel_data_size = row_size * height
    header_size = 14 + 40 + 12  # BMP header + DIB header + 3 color masks
    file_size = header_size + pixel_data_size

    with open(output_path, 'wb') as f:
        # BMP File Header (14 bytes)
        f.write(struct.pack('<2sIHHI', b'BM', file_size, 0, 0, header_size))

        # DIB Header (BITMAPINFOHEADER, 40 bytes)
        f.write(struct.pack('<IiiHHIIiiII',
            40,             # Header size
            width,          # Width
            -height,        # Height (negative = top-down)
            1,              # Color planes
            16,             # Bits per pixel
            3,              # Compression (BI_BITFIELDS)
            pixel_data_size,
            0, 0,           # Resolution
            0, 0            # Colors
        ))

        # Color masks for RGB565
        f.write(struct.pack('<III', 0xF800, 0x07E0, 0x001F))

        # Pixel data (top-down, RGB565)
        pixels = rgb.load()
        for y in range(height):
            row = bytearray()
            for x in range(width):
                r, g, b = pixels[x, y]
                rgb565 = rgb888_to_rgb565(r, g, b)
                row += struct.pack('<H', rgb565)
            # Pad row to 4-byte boundary
            while len(row) < row_size:
                row += b'\x00'
            f.write(row)

    print(f"  {os.path.basename(output_path)} ({width}x{height})")

def select_frames(frame_dir, prefix, count=4):
    """Select evenly-spaced frames from an animation directory."""
    files = sorted([
        f for f in os.listdir(frame_dir)
        if f.endswith('.png') and f.startswith(prefix)
    ], key=lambda f: int(''.join(filter(str.isdigit, f)) or '0'))

    if not files:
        return []

    if len(files) <= count:
        return [os.path.join(frame_dir, f) for f in files]

    # Evenly space the selection
    step = len(files) / count
    selected = [files[int(i * step)] for i in range(count)]
    return [os.path.join(frame_dir, f) for f in selected]

def main():
    if len(sys.argv) < 3:
        print(f"Usage: {sys.argv[0]} <loki_theme_dir> <output_dir> [--size 80]")
        sys.exit(1)

    theme_dir = sys.argv[1]
    output_dir = sys.argv[2]
    sprite_size = 80

    if '--size' in sys.argv:
        idx = sys.argv.index('--size')
        sprite_size = int(sys.argv[idx + 1])

    status_dir = os.path.join(theme_dir, 'images', 'status')
    icons_dir = os.path.join(theme_dir, 'images')

    os.makedirs(os.path.join(output_dir, 'loki'), exist_ok=True)
    os.makedirs(os.path.join(output_dir, 'loki', 'icons'), exist_ok=True)

    # State mappings: CYD name -> (source folder, source prefix, frame count)
    states = {
        'idle':   ('IDLE', 'IDLE', 6),
        'scan':   ('NetworkScanner', 'NetworkScanner', 4),
        'attack': ('SSHBruteforce', 'SSHBruteforce', 4),
        'ftp':    ('FTPBruteforce', 'FTPBruteforce', 4),
        'telnet': ('TelnetBruteforce', 'TelnetBruteforce', 4),
        'steal':  ('StealFilesSSH', 'StealFilesSSH', 4),
        'vuln':   ('NmapVulnScanner', 'NmapVulnScanner', 4),
    }

    print(f"Converting sprites to {sprite_size}x{sprite_size} RGB565 BMP...")
    print(f"Source: {theme_dir}")
    print(f"Output: {output_dir}")
    print()

    for state_name, (folder, prefix, count) in states.items():
        src = os.path.join(status_dir, folder)
        if not os.path.exists(src):
            print(f"  SKIP {state_name} (not found: {src})")
            continue

        print(f"  {state_name}:")
        frames = select_frames(src, prefix, count)
        for i, frame_path in enumerate(frames):
            out_name = f"{state_name}{i + 1}.bmp"
            out_path = os.path.join(output_dir, 'loki', out_name)
            png_to_rgb565_bmp(frame_path, out_path, sprite_size)

    # Convert stat icons (30x30 for CYD)
    icon_size = 30
    icon_files = {
        'target.png': 'target.bmp',
        'port.png': 'port.bmp',
        'cred.png': 'cred.bmp',
        'data.png': 'data.bmp',
        'vuln.png': 'vuln.bmp',
        'zombie.png': 'zombie.bmp',
        'gold.png': 'gold.bmp',
        'attacks.png': 'attacks.bmp',
    }

    print(f"\n  Icons ({icon_size}x{icon_size}):")
    for src_name, dst_name in icon_files.items():
        src_path = os.path.join(icons_dir, src_name)
        if os.path.exists(src_path):
            dst_path = os.path.join(output_dir, 'loki', 'icons', dst_name)
            png_to_rgb565_bmp(src_path, dst_path, icon_size)

    print(f"\nDone! Copy the '{output_dir}/loki' folder to your CYD's SD card root.")

if __name__ == '__main__':
    main()
