#!/usr/bin/env python3
"""
Generate a composite full-screen background image for Loki CYD.

Bakes in: header bar, stat grid with icons, frise divider, grid lines,
dialogue box outline, kill feed area — everything STATIC.

Dynamic elements (character sprite, stat numbers, status text, comments,
status icon) are drawn on top by the firmware at fixed coordinates.

Usage:
    python3 make_background.py <loki_theme_dir> <output_dir> [--width 320] [--height 480]

Output:
    <output_dir>/loki/bg.bmp          — Main background (RGB565)
    <output_dir>/loki/bg_noicon.bmp   — Background with no status icon area
    <output_dir>/loki/layout.h        — C header with coordinate #defines
"""

import os
import sys
import struct
import json
from PIL import Image, ImageDraw, ImageFont

def rgb_to_tuple(rgb_list):
    return tuple(rgb_list)

def save_rgb565_bmp(img, output_path):
    """Save a PIL Image as RGB565 BMP."""
    rgb = img.convert("RGB")
    width, height = rgb.size
    row_size = ((width * 2 + 3) // 4) * 4
    pixel_data_size = row_size * height
    header_size = 14 + 40 + 12
    file_size = header_size + pixel_data_size

    with open(output_path, 'wb') as f:
        f.write(struct.pack('<2sIHHI', b'BM', file_size, 0, 0, header_size))
        f.write(struct.pack('<IiiHHIIiiII',
            40, width, -height, 1, 16, 3, pixel_data_size, 0, 0, 0, 0))
        f.write(struct.pack('<III', 0xF800, 0x07E0, 0x001F))

        pixels = rgb.load()
        for y in range(height):
            row = bytearray()
            for x in range(width):
                r, g, b = pixels[x, y]
                rgb565 = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
                row += struct.pack('<H', rgb565)
            while len(row) < row_size:
                row += b'\x00'
            f.write(row)

    print(f"  Saved: {os.path.basename(output_path)} ({width}x{height})")


def make_background(theme_dir, output_dir, screen_w=320, screen_h=480):
    """Generate the composite background image."""

    # Load theme config
    theme_path = os.path.join(theme_dir, 'theme.json')
    with open(theme_path) as f:
        theme = json.load(f)

    images_dir = os.path.join(theme_dir, 'images')
    os.makedirs(os.path.join(output_dir, 'loki'), exist_ok=True)

    # Colors from theme
    bg_color = rgb_to_tuple(theme.get('menu_colors', {}).get('bg', [10, 18, 10]))
    title_color = rgb_to_tuple(theme.get('menu_colors', {}).get('title', [100, 190, 90]))
    text_color = rgb_to_tuple(theme.get('text_color', [0, 0, 0]))
    accent_color = rgb_to_tuple(theme.get('accent_color', [0, 0, 0]))
    dim_color = rgb_to_tuple(theme.get('menu_colors', {}).get('dim', [70, 85, 70]))
    surface_color = tuple(min(c + 10, 255) for c in bg_color)
    elevated_color = tuple(min(c + 20, 255) for c in bg_color)
    border_color = tuple(min(c + 40, 255) for c in bg_color)

    # ─── Layout coordinates (portrait, scaled to screen) ───
    # These proportions are based on the original Loki 222x480 layout
    sx = screen_w / 222.0
    sy = screen_h / 480.0

    layout = {
        'header':    {'x': 0, 'y': 0, 'w': screen_w, 'h': int(32 * sy)},
        'stats':     {'x': 0, 'y': int(34 * sy), 'w': screen_w,
                      'cols': 3, 'rows': 3, 'icon_size': min(int(18 * sx), 22),
                      'row_h_override': min(int(18 * sx), 22) + 8},
        'frise':     {'x': 0, 'y': 0, 'w': screen_w, 'h': int(10 * sy)},  # y set after stats
        'status':    {'x': 0, 'y': 0, 'w': screen_w, 'h': int(34 * sy),
                      'icon_size': int(30 * sx), 'icon_x': int(4 * sx),
                      'text_x_icon': int(38 * sx), 'text_x_noicon': int(6 * sx)},
        'dialogue':  {'x': int(4 * sx), 'y': 0, 'w': screen_w - int(8 * sx), 'h': int(54 * sy)},
        'character': {'x': int((screen_w - int(120 * sx)) / 2), 'y': 0,
                      'w': int(120 * sx), 'h': int(120 * sx)},
        'killfeed':  {'x': 0, 'y': 0, 'w': screen_w, 'h': 0},
    }

    # ─── Compute dynamic y positions after stats height is known ───
    stats_bottom = layout['stats']['y'] + layout['stats'].get('row_h_override', 30) * layout['stats']['rows']
    layout['frise']['y'] = stats_bottom + 2
    layout['status']['y'] = layout['frise']['y'] + layout['frise']['h'] + 2
    layout['dialogue']['y'] = layout['status']['y'] + layout['status']['h'] + int(4 * sy)
    layout['character']['y'] = layout['dialogue']['y'] + layout['dialogue']['h'] + int(8 * sy)
    kf_top = layout['character']['y'] + layout['character']['h'] + int(8 * sy)
    layout['killfeed']['y'] = kf_top
    layout['killfeed']['h'] = screen_h - kf_top

    # ─── Create background image ───
    img = Image.new('RGB', (screen_w, screen_h), bg_color)
    draw = ImageDraw.Draw(img)

    # Header bar
    h = layout['header']
    draw.rectangle([h['x'], h['y'], h['x'] + h['w'], h['y'] + h['h']], fill=surface_color)

    # Title "LOKI" baked into background
    try:
        title_font = ImageFont.truetype("/System/Library/Fonts/Menlo.ttc", int(h['h'] * 0.65))
        xp_font = ImageFont.truetype("/System/Library/Fonts/Menlo.ttc", int(h['h'] * 0.38))
    except:
        title_font = ImageFont.load_default()
        xp_font = title_font

    draw.text((5, h['h'] // 6), theme.get('display_name', 'LOKI'), fill=title_color, font=title_font)

    # XP icon (gold rune) + "XP:" label baked in (value drawn dynamically)
    xp_icon_size = int(h['h'] * 0.7)
    xp_icon_path = os.path.join(images_dir, 'gold.png')
    xp_icon_x = screen_w // 2 - xp_icon_size - 4
    xp_icon_y = (h['h'] - xp_icon_size) // 2
    if os.path.exists(xp_icon_path):
        xp_icon = Image.open(xp_icon_path).convert('RGBA')
        xp_icon = xp_icon.resize((xp_icon_size, xp_icon_size), Image.LANCZOS)
        img.paste(xp_icon, (xp_icon_x, xp_icon_y), xp_icon)
    # Value will be drawn dynamically right after the icon

    draw.line([0, h['y'] + h['h'], screen_w, h['y'] + h['h']], fill=border_color)

    # Stats grid
    s = layout['stats']
    col_w = s['w'] // s['cols']
    icon_size = s['icon_size']
    row_h = s.get('row_h_override', icon_size + 8)
    s['h'] = row_h * s['rows']  # Recompute total height

    # 9 stat icons in 3x3 grid
    stat_icons = ['target', 'port', 'vuln', 'cred', 'zombie', 'data', 'networkkb', 'level', 'attacks']
    for i, icon_name in enumerate(stat_icons):
        col = i % 3
        row = i // 3
        ix = s['x'] + col * col_w + 4
        iy = s['y'] + row * row_h + (row_h - icon_size) // 2

        icon_path = os.path.join(images_dir, f'{icon_name}.png')
        if os.path.exists(icon_path):
            icon = Image.open(icon_path).convert('RGBA')
            icon = icon.resize((icon_size, icon_size), Image.LANCZOS)
            img.paste(icon, (ix, iy), icon)

        # Grid separator lines (vertical)
        if col > 0:
            gx = s['x'] + col * col_w
            draw.line([gx, s['y'], gx, s['y'] + s['h']], fill=border_color)

    # Horizontal separators between all stat rows
    for r in range(1, s['rows']):
        gy = s['y'] + r * row_h
        draw.line([s['x'], gy, s['x'] + s['w'], gy], fill=border_color)

    # Stats bottom border
    draw.line([0, s['y'] + s['h'], screen_w, s['y'] + s['h']], fill=border_color)

    # Frise (decorative divider)
    fr = layout['frise']
    frise_path = os.path.join(images_dir, 'frise.png')
    if os.path.exists(frise_path):
        frise = Image.open(frise_path).convert('RGBA')
        frise = frise.resize((fr['w'], fr['h']), Image.LANCZOS)
        img.paste(frise, (fr['x'], fr['y']), frise)
    else:
        # Fallback: draw a simple line
        mid_y = fr['y'] + fr['h'] // 2
        draw.line([4, mid_y, screen_w - 4, mid_y], fill=border_color, width=2)

    # Status area background
    st = layout['status']
    draw.rectangle([st['x'], st['y'], st['x'] + st['w'], st['y'] + st['h']], fill=surface_color)
    draw.line([0, st['y'] + st['h'], screen_w, st['y'] + st['h']], fill=border_color)

    # Dialogue box outline (speech bubble border + tail pointing down to character)
    dl = layout['dialogue']
    draw.rounded_rectangle(
        [dl['x'], dl['y'], dl['x'] + dl['w'], dl['y'] + dl['h']],
        radius=6, outline=border_color, fill=elevated_color
    )
    # Bubble tail (triangle pointing down toward character)
    tri_x = screen_w // 2
    tri_top = dl['y'] + dl['h']
    tri_h = 8
    draw.polygon(
        [(tri_x - 6, tri_top), (tri_x + 6, tri_top), (tri_x, tri_top + tri_h)],
        fill=elevated_color, outline=border_color
    )
    # Cover the border line inside the tail base so it looks connected
    draw.line([tri_x - 5, tri_top, tri_x + 5, tri_top], fill=elevated_color)

    # Character area — just a subtle background (sprite drawn on top)
    ch = layout['character']
    # Leave as bg_color (transparent area for sprite)

    # Kill feed area — subtle background
    kf = layout['killfeed']
    draw.rectangle([kf['x'], kf['y'], kf['x'] + kf['w'], kf['y'] + kf['h']], fill=bg_color)
    draw.line([0, kf['y'], screen_w, kf['y']], fill=border_color)

    # ─── Save main background (with status icon space) ───
    save_rgb565_bmp(img, os.path.join(output_dir, 'loki', 'bg.bmp'))

    # ─── Save no-icon variant (status area has no icon space) ───
    # Just the same image — the firmware handles the text offset
    save_rgb565_bmp(img, os.path.join(output_dir, 'loki', 'bg_noicon.bmp'))

    # ─── Generate C header with layout coordinates ───
    header_path = os.path.join(output_dir, 'loki', 'layout.h')
    with open(header_path, 'w') as f:
        f.write(f"// Auto-generated layout for {screen_w}x{screen_h}\n")
        f.write(f"// Theme: {theme.get('display_name', 'Loki')}\n\n")
        f.write("#ifndef LOKI_LAYOUT_H\n#define LOKI_LAYOUT_H\n\n")

        f.write(f"#define BG_W {screen_w}\n")
        f.write(f"#define BG_H {screen_h}\n\n")

        for section, coords in layout.items():
            prefix = section.upper()
            for key, val in coords.items():
                if isinstance(val, int):
                    f.write(f"#define LY_{prefix}_{key.upper()} {val}\n")
            f.write("\n")

        # Stat value positions (right of icon, per cell)
        f.write("// Stat value positions (x, y) for each of 6 stats\n")
        for i in range(6):
            col = i % 3
            row = i // 3
            vx = s['x'] + col * col_w + icon_size + 6
            vy = s['y'] + row * row_h + row_h // 2
            f.write(f"#define LY_STAT{i}_X {vx}\n")
            f.write(f"#define LY_STAT{i}_Y {vy}\n")
        f.write("\n")

        # Status text positions
        f.write(f"#define LY_STATUS_TEXT_X_ICON {st['text_x_icon']}\n")
        f.write(f"#define LY_STATUS_TEXT_X_NOICON {st['text_x_noicon']}\n")
        f.write(f"#define LY_STATUS_ICON_X {st['icon_x']}\n")
        f.write(f"#define LY_STATUS_ICON_Y {st['y'] + (st['h'] - st['icon_size']) // 2}\n")
        f.write(f"#define LY_STATUS_ICON_SIZE {st['icon_size']}\n")
        f.write(f"#define LY_STATUS_TEXT_Y {st['y'] + st['h'] // 2}\n\n")

        # Dialogue text position
        f.write(f"#define LY_DIALOGUE_TEXT_X {dl['x'] + 8}\n")
        f.write(f"#define LY_DIALOGUE_TEXT_Y {dl['y'] + 8}\n")
        f.write(f"#define LY_DIALOGUE_TEXT_W {dl['w'] - 16}\n")
        f.write(f"#define LY_DIALOGUE_TEXT_H {dl['h'] - 16}\n\n")

        # Character sprite position
        f.write(f"#define LY_CHAR_X {ch['x']}\n")
        f.write(f"#define LY_CHAR_Y {ch['y']}\n")
        f.write(f"#define LY_CHAR_W {ch['w']}\n")
        f.write(f"#define LY_CHAR_H {ch['h']}\n\n")

        # Kill feed positions
        kf_line_h = max(12, (kf['h']) // 4)
        f.write(f"#define LY_KILLFEED_X 3\n")
        f.write(f"#define LY_KILLFEED_Y {kf['y'] + 4}\n")
        f.write(f"#define LY_KILLFEED_LINE_H {kf_line_h}\n")
        f.write(f"#define LY_KILLFEED_LINES {min(4, kf['h'] // kf_line_h)}\n\n")

        f.write("#endif // LOKI_LAYOUT_H\n")

    print(f"  Saved: layout.h")

    # Print layout summary
    print(f"\n  Layout summary ({screen_w}x{screen_h}):")
    for section, coords in layout.items():
        y = coords.get('y', 0)
        h = coords.get('h', 0)
        print(f"    {section:12s}: y={y:3d}  h={h:3d}  ({y}-{y+h})")


def main():
    if len(sys.argv) < 3:
        print(f"Usage: {sys.argv[0]} <loki_theme_dir> <output_dir> [--width W] [--height H]")
        sys.exit(1)

    theme_dir = sys.argv[1]
    output_dir = sys.argv[2]
    width = 320
    height = 480

    if '--width' in sys.argv:
        width = int(sys.argv[sys.argv.index('--width') + 1])
    if '--height' in sys.argv:
        height = int(sys.argv[sys.argv.index('--height') + 1])

    print(f"Generating {width}x{height} composite background...")
    make_background(theme_dir, output_dir, width, height)
    print("\nDone!")


if __name__ == '__main__':
    main()
