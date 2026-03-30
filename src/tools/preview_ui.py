#!/usr/bin/env python3
"""
Generate a preview mockup of the Loki CYD UI.
9 stats in a 3x3 grid + XP in the header.

Usage:
    python3 preview_ui.py <loki_theme_dir> <output.png> [--width 320] [--height 480]
"""

import os
import sys
import json
from PIL import Image, ImageDraw, ImageFont


def make_preview(theme_dir, output_path, screen_w=320, screen_h=480):
    images_dir = os.path.join(theme_dir, 'images')
    status_dir = os.path.join(images_dir, 'status')

    with open(os.path.join(theme_dir, 'theme.json')) as f:
        theme = json.load(f)

    bg_color = tuple(theme.get('menu_colors', {}).get('bg', [10, 18, 10]))
    surface_color = tuple(min(c + 10, 255) for c in bg_color)
    elevated_color = tuple(min(c + 20, 255) for c in bg_color)
    border_color = tuple(min(c + 40, 255) for c in bg_color)
    green = (94, 189, 69)
    gold = (255, 200, 0)
    cyan = (0, 255, 255)
    bright = (126, 216, 96)
    hotpink = (255, 106, 176)
    magenta = (255, 0, 255)
    red = (255, 60, 60)
    text_dim = (78, 107, 78)
    text_primary = (212, 232, 212)

    sx = screen_w / 222.0
    sy = screen_h / 480.0

    img = Image.new('RGB', (screen_w, screen_h), bg_color)
    draw = ImageDraw.Draw(img)

    try:
        font_sm = ImageFont.truetype("/System/Library/Fonts/Menlo.ttc", int(11 * min(sx, sy)))
        font_md = ImageFont.truetype("/System/Library/Fonts/Menlo.ttc", int(14 * min(sx, sy)))
        font_lg = ImageFont.truetype("/System/Library/Fonts/Menlo.ttc", int(20 * min(sx, sy)))
    except:
        font_sm = ImageFont.load_default()
        font_md = font_sm
        font_lg = font_sm

    # ── Header ──
    header_h = int(32 * sy)
    draw.rectangle([0, 0, screen_w, header_h], fill=surface_color)
    draw.text((5, header_h // 4), "LOKI", fill=green, font=font_lg)

    # Gold rune icon + XP value
    xp_icon_size = int(header_h * 0.7)
    xp_icon_path = os.path.join(images_dir, 'gold.png')
    xp_icon_x = screen_w // 2 - xp_icon_size - 4
    xp_icon_y = (header_h - xp_icon_size) // 2
    if os.path.exists(xp_icon_path):
        xp_icon = Image.open(xp_icon_path).convert('RGBA')
        xp_icon = xp_icon.resize((xp_icon_size, xp_icon_size), Image.LANCZOS)
        img.paste(xp_icon, (xp_icon_x, xp_icon_y), xp_icon)
    draw.text((xp_icon_x + xp_icon_size + 3, header_h // 4 + 4), "1337", fill=gold, font=font_sm)

    draw.text((screen_w - 70, header_h // 4 + 4), "Scanning", fill=text_dim, font=font_sm)
    draw.line([0, header_h, screen_w, header_h], fill=border_color)

    # ── Stats Grid (3x3 = 9 stats) ──
    stats_y = int(34 * sy)
    rows = 3
    cols = 3
    icon_size = min(int(18 * sx), 22)  # Cap at 22px
    row_h = icon_size + 8              # Icon + padding
    stats_h = row_h * rows
    col_w = screen_w // cols

    # 9 stats in order: target, port, vuln, cred, zombie, data, gold, level, attacks
    grid_stats = [
        ('target',    '12',   cyan),
        ('port',      '47',   bright),
        ('vuln',      '5',    red),
        ('cred',      '3',    hotpink),
        ('zombie',    '2',    magenta),
        ('data',      '814K', text_primary),
        ('networkkb', '8',    text_primary),
        ('level',     '7',    text_primary),
        ('attacks',   '156',  bright),
    ]

    for i, (icon_name, value, color) in enumerate(grid_stats):
        col = i % cols
        row = i // cols
        ix = col * col_w + 4
        iy = stats_y + row * row_h + (row_h - icon_size) // 2

        icon_path = os.path.join(images_dir, f'{icon_name}.png')
        if os.path.exists(icon_path):
            icon = Image.open(icon_path).convert('RGBA')
            icon = icon.resize((icon_size, icon_size), Image.LANCZOS)
            img.paste(icon, (ix, iy), icon)

        draw.text((ix + icon_size + 4, iy + 2), value, fill=color, font=font_sm)

        # Vertical grid lines
        if col > 0:
            gx = col * col_w
            draw.line([gx, stats_y, gx, stats_y + stats_h], fill=border_color)

    # Horizontal grid lines
    for r in range(1, rows):
        gy = stats_y + r * row_h
        draw.line([0, gy, screen_w, gy], fill=border_color)
    draw.line([0, stats_y + stats_h, screen_w, stats_y + stats_h], fill=border_color)

    # ── Frise ──
    frise_y = stats_y + stats_h + int(2 * sy)
    frise_h = int(10 * sy)
    frise_path = os.path.join(images_dir, 'frise.png')
    if os.path.exists(frise_path):
        frise = Image.open(frise_path).convert('RGBA')
        frise = frise.resize((screen_w, frise_h), Image.LANCZOS)
        img.paste(frise, (0, frise_y), frise)

    # ── Status Area ──
    status_y = frise_y + frise_h + int(2 * sy)
    status_h = int(34 * sy)
    draw.rectangle([0, status_y, screen_w, status_y + status_h], fill=surface_color)

    status_icon_path = os.path.join(status_dir, 'NetworkScanner', 'NetworkScanner1.png')
    if os.path.exists(status_icon_path):
        sicon = Image.open(status_icon_path).convert('RGBA')
        si_size = int(28 * sx)
        sicon = sicon.resize((si_size, si_size), Image.LANCZOS)
        si_y = status_y + (status_h - si_size) // 2
        img.paste(sicon, (int(4 * sx), si_y), sicon)

    draw.text((int(38 * sx), status_y + status_h // 3), "Scanning 192.168.1.x", fill=green, font=font_sm)
    draw.line([0, status_y + status_h, screen_w, status_y + status_h], fill=border_color)

    # ── Dialogue Box ──
    dlg_x = int(4 * sx)
    dlg_y = status_y + status_h + int(4 * sy)
    dlg_w = screen_w - int(8 * sx)
    dlg_h = int(54 * sy)
    draw.rounded_rectangle([dlg_x, dlg_y, dlg_x + dlg_w, dlg_y + dlg_h],
                           radius=6, outline=border_color, fill=elevated_color)
    tri_x = screen_w // 2
    draw.polygon([(tri_x - 5, dlg_y + dlg_h), (tri_x + 5, dlg_y + dlg_h),
                  (tri_x, dlg_y + dlg_h + 6)], fill=elevated_color)
    draw.text((dlg_x + 10, dlg_y + 8), "I see you, little host.", fill=text_primary, font=font_sm)
    draw.text((dlg_x + 10, dlg_y + 26), "Mapping the battlefield.", fill=text_primary, font=font_sm)

    # ── Character ──
    sprite_size = int(120 * sx)
    char_x = (screen_w - sprite_size) // 2
    char_y = dlg_y + dlg_h + int(12 * sy)

    char_path = os.path.join(status_dir, 'NetworkScanner', 'NetworkScanner1.png')
    if os.path.exists(char_path):
        char_img = Image.open(char_path).convert('RGBA')
        char_img = char_img.resize((sprite_size, sprite_size), Image.LANCZOS)
        img.paste(char_img, (char_x, char_y), char_img)

    # ── Kill Feed ──
    kf_y = char_y + sprite_size + int(8 * sy)
    draw.line([0, kf_y, screen_w, kf_y], fill=border_color)

    kf_lines = [
        ("[+] HOST 192.168.1.5 (4 ports)", bright),
        ("[*] Camera 192.168.1.5 (Hikvision)", cyan),
        ("[>] RTSP brute 192.168.1.5", cyan),
        ("[!!!] CRACKED 192.168.1.5 admin:12345", hotpink),
    ]

    line_h = int(20 * sy)
    for i, (text, color) in enumerate(kf_lines):
        y = kf_y + 4 + i * line_h
        if y + line_h > screen_h:
            break
        draw.text((3, y), text, fill=color, font=font_sm)

    img.save(output_path)
    print(f"Preview saved: {output_path} ({screen_w}x{screen_h})")


def main():
    if len(sys.argv) < 3:
        print(f"Usage: {sys.argv[0]} <loki_theme_dir> <output.png> [--width W] [--height H]")
        sys.exit(1)

    theme_dir = sys.argv[1]
    output_path = sys.argv[2]
    width = 320
    height = 480

    if '--width' in sys.argv:
        width = int(sys.argv[sys.argv.index('--width') + 1])
    if '--height' in sys.argv:
        height = int(sys.argv[sys.argv.index('--height') + 1])

    make_preview(theme_dir, output_path, width, height)


if __name__ == '__main__':
    main()
