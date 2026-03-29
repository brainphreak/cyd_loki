#!/usr/bin/env python3
"""
Convert RGB565 BMP files to PROGMEM C header arrays.
Reads BMP files from the sdcard/loki directory and generates .h files
that can be included directly in the firmware.

Usage:
    python3 bmp_to_progmem.py <sdcard_dir> <output_dir>
"""

import os
import sys
import struct


def bmp_to_array(bmp_path):
    """Read an RGB565 BMP and return (width, height, pixel_data_as_uint16_list)."""
    with open(bmp_path, 'rb') as f:
        header = f.read(66)

    if header[0:2] != b'BM':
        raise ValueError(f"Not a BMP: {bmp_path}")

    data_offset = struct.unpack_from('<I', header, 10)[0]
    width = struct.unpack_from('<i', header, 18)[0]
    height = struct.unpack_from('<i', header, 22)[0]
    bpp = struct.unpack_from('<H', header, 28)[0]

    top_down = height < 0
    if height < 0:
        height = -height

    if bpp != 16:
        raise ValueError(f"Expected 16bpp, got {bpp}")

    row_size = ((width * 2 + 3) // 4) * 4
    pixels = []

    with open(bmp_path, 'rb') as f:
        f.seek(data_offset)
        for row in range(height):
            row_data = f.read(row_size)
            for x in range(width):
                px = struct.unpack_from('<H', row_data, x * 2)[0]
                pixels.append(px)

    return width, height, pixels


def write_header(name, width, height, pixels, output_path):
    """Write a PROGMEM C header file."""
    guard = f"LOKI_ASSET_{name.upper()}_H"
    with open(output_path, 'w') as f:
        f.write(f"// Auto-generated from {name}.bmp — {width}x{height} RGB565\n")
        f.write(f"#ifndef {guard}\n#define {guard}\n\n")
        f.write("#include <Arduino.h>\n\n")
        f.write(f"#define {name.upper()}_W {width}\n")
        f.write(f"#define {name.upper()}_H {height}\n\n")
        f.write(f"static const uint16_t {name}_data[{len(pixels)}] PROGMEM = {{\n")

        # Write 12 values per line
        for i in range(0, len(pixels), 12):
            chunk = pixels[i:i+12]
            line = ", ".join(f"0x{px:04X}" for px in chunk)
            if i + 12 < len(pixels):
                f.write(f"    {line},\n")
            else:
                f.write(f"    {line}\n")

        f.write("};\n\n")
        f.write(f"#endif // {guard}\n")

    size_kb = os.path.getsize(output_path) / 1024
    print(f"  {name}.h ({width}x{height}, {size_kb:.0f}KB)")


def main():
    if len(sys.argv) < 3:
        print(f"Usage: {sys.argv[0]} <sdcard_dir> <output_dir>")
        sys.exit(1)

    sdcard_dir = sys.argv[1]
    output_dir = sys.argv[2]
    loki_dir = os.path.join(sdcard_dir, 'loki')

    os.makedirs(output_dir, exist_ok=True)

    print("Converting BMP assets to PROGMEM headers...\n")

    # Background
    bg_path = os.path.join(loki_dir, 'bg.bmp')
    if os.path.exists(bg_path):
        w, h, px = bmp_to_array(bg_path)
        write_header('bg', w, h, px, os.path.join(output_dir, 'asset_bg.h'))

    # Character sprites
    print("\n  Character sprites:")
    states = ['idle', 'scan', 'attack', 'ftp', 'telnet', 'steal', 'vuln']
    all_sprites = {}

    for state in states:
        frame = 1
        while True:
            bmp_name = f"{state}{frame}"
            bmp_path = os.path.join(loki_dir, f"{bmp_name}.bmp")
            if not os.path.exists(bmp_path):
                break
            w, h, px = bmp_to_array(bmp_path)
            header_name = f"sprite_{state}{frame}"
            write_header(header_name, w, h, px,
                         os.path.join(output_dir, f"asset_{header_name}.h"))
            if state not in all_sprites:
                all_sprites[state] = []
            all_sprites[state].append(header_name)
            frame += 1

    # Icons
    print("\n  Icons:")
    icons_dir = os.path.join(loki_dir, 'icons')
    icon_names = []
    if os.path.exists(icons_dir):
        for fname in sorted(os.listdir(icons_dir)):
            if fname.endswith('.bmp'):
                name = fname[:-4]
                w, h, px = bmp_to_array(os.path.join(icons_dir, fname))
                header_name = f"icon_{name}"
                write_header(header_name, w, h, px,
                             os.path.join(output_dir, f"asset_{header_name}.h"))
                icon_names.append(header_name)

    # Generate master include + sprite table
    print("\n  Generating asset index...")
    with open(os.path.join(output_dir, 'loki_assets.h'), 'w') as f:
        f.write("// Auto-generated — all Loki PROGMEM assets\n")
        f.write("#ifndef LOKI_ASSETS_H\n#define LOKI_ASSETS_H\n\n")

        # Include background
        f.write('#include "asset_bg.h"\n\n')

        # Include all sprites
        for state, names in all_sprites.items():
            for name in names:
                f.write(f'#include "asset_{name}.h"\n')
        f.write("\n")

        # Include icons
        for icon in icon_names:
            f.write(f'#include "asset_{icon}.h"\n')
        f.write("\n")

        # Sprite frame table
        f.write("// Sprite frame lookup table\n")
        f.write("struct SpriteFrameSet {\n")
        f.write("    const char* name;\n")
        f.write("    const uint16_t* frames[8];\n")
        f.write("    int count;\n")
        f.write("    int w, h;\n")
        f.write("};\n\n")

        f.write(f"#define SPRITE_STATE_COUNT {len(all_sprites)}\n")
        f.write("static const SpriteFrameSet spriteFrames[SPRITE_STATE_COUNT] PROGMEM = {\n")
        for state, names in all_sprites.items():
            ptrs = ", ".join(f"{n}_data" for n in names)
            # Pad to 8
            while len(names) < 8:
                ptrs += ", nullptr"
                names.append(None)
            w_name = names[0] if names[0] else list(all_sprites.values())[0][0]
            f.write(f'    {{"{state}", {{{ptrs}}}, {sum(1 for n in all_sprites[state] if n)}, '
                    f'{w_name.upper()}_W, {w_name.upper()}_H}},\n')
        f.write("};\n\n")

        f.write("#endif // LOKI_ASSETS_H\n")

    # Total size
    total = sum(os.path.getsize(os.path.join(output_dir, f))
                for f in os.listdir(output_dir) if f.endswith('.h'))
    print(f"\n  Total header size: {total / 1024:.0f}KB (source)")
    print(f"  Estimated flash usage: ~{total * 0.35 / 1024:.0f}KB (compiled)")
    print("\nDone!")


if __name__ == '__main__':
    main()
