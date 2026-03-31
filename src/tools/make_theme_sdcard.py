#!/usr/bin/env python3
"""
Build a complete self-contained theme folder for the Loki CYD SD card.

Usage:
    python3 make_theme_sdcard.py <loki_theme_dir> <output_dir> [--name loki] [--screen 320x480] [--sprite 175]

Creates:
    <output_dir>/loki/themes/<name>/
        theme.cfg
        bg.bmp
        idle1.bmp ... idle6.bmp
        scan1.bmp ... scan4.bmp
        attack1.bmp ... attack4.bmp
        ftp1.bmp ... telnet1.bmp ... steal1.bmp ... vuln1.bmp ...
"""

import os
import sys
import json

# Import our existing tools
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from convert_sprites import png_to_rgb565_bmp, select_frames
from make_background import make_background


def main():
    if len(sys.argv) < 3:
        print(f"Usage: {sys.argv[0]} <loki_theme_dir> <output_dir> [--name NAME] [--screen WxH] [--sprite SIZE]")
        sys.exit(1)

    theme_dir = sys.argv[1]
    output_dir = sys.argv[2]
    theme_name = "loki"
    screen_w, screen_h = 320, 480
    sprite_size = 175

    if '--name' in sys.argv:
        theme_name = sys.argv[sys.argv.index('--name') + 1]
    if '--screen' in sys.argv:
        dims = sys.argv[sys.argv.index('--screen') + 1].split('x')
        screen_w, screen_h = int(dims[0]), int(dims[1])
    if '--sprite' in sys.argv:
        sprite_size = int(sys.argv[sys.argv.index('--sprite') + 1])

    # Load theme.json for settings
    theme_json_path = os.path.join(theme_dir, 'theme.json')
    with open(theme_json_path) as f:
        theme_json = json.load(f)

    # Create theme folder
    theme_folder = os.path.join(output_dir, 'loki', 'themes', theme_name)
    os.makedirs(theme_folder, exist_ok=True)
    os.makedirs(os.path.join(output_dir, 'loki', 'loot'), exist_ok=True)
    os.makedirs(os.path.join(output_dir, 'loki', 'reports'), exist_ok=True)

    print(f"Building theme '{theme_name}' for {screen_w}x{screen_h} (sprites: {sprite_size}px)")
    print(f"Output: {theme_folder}")
    print()

    # --- 1. Generate theme.cfg ---
    anim_min = int(theme_json.get('image_display_delaymin', 1.5) * 1000)
    anim_max = int(theme_json.get('image_display_delaymax', 2.0) * 1000)
    comment_min = int(theme_json.get('comment_delaymin', 15) * 1000)
    comment_max = int(theme_json.get('comment_delaymax', 30) * 1000)
    display_name = theme_json.get('display_name', theme_name.upper())

    cfg_path = os.path.join(theme_folder, 'theme.cfg')
    with open(cfg_path, 'w') as f:
        f.write(f"# Loki CYD Theme Configuration\n")
        f.write(f"# Theme: {display_name}\n\n")
        f.write(f"name = {display_name}\n")
        f.write(f"sprite_size = {sprite_size}\n")
        f.write(f"anim_interval_min = {anim_min}\n")
        f.write(f"anim_interval_max = {anim_max}\n")
        f.write(f"comment_interval_min = {comment_min}\n")
        f.write(f"comment_interval_max = {comment_max}\n")
        anim_mode = theme_json.get('animation_mode', 'sequential')
        f.write(f"animation_mode = {anim_mode}\n")
    print(f"  theme.cfg (anim: {anim_min}-{anim_max}ms, mode: {anim_mode})")

    # --- 2. Generate background ---
    print(f"\n  Background ({screen_w}x{screen_h}):")
    # Temporarily redirect output
    make_background(theme_dir, theme_folder.rstrip('/') + '/../..', screen_w, screen_h)
    # Move bg.bmp into theme folder
    src_bg = os.path.join(output_dir, 'loki', 'bg.bmp')
    dst_bg = os.path.join(theme_folder, 'bg.bmp')
    if os.path.exists(src_bg):
        os.rename(src_bg, dst_bg)
    # Clean up extra files
    for cleanup in ['bg_noicon.bmp', 'layout.h']:
        p = os.path.join(output_dir, 'loki', cleanup)
        if os.path.exists(p):
            os.remove(p)

    # --- 3. Generate character sprites ---
    status_dir = os.path.join(theme_dir, 'images', 'status')
    bg_color = (255, 0, 255)  # Magenta transparency key

    # Map internal state names to original Loki folder names
    states = {
        'idle':   ('IDLE', 'IDLE', 0),               # 0 = ALL frames
        'scan':   ('NetworkScanner', 'NetworkScanner', 0),
        'attack': ('SSHBruteforce', 'SSHBruteforce', 0),
        'ftp':    ('FTPBruteforce', 'FTPBruteforce', 0),
        'telnet': ('TelnetBruteforce', 'TelnetBruteforce', 0),
        'steal':  ('StealFilesSSH', 'StealFilesSSH', 0),
        'vuln':   ('NmapVulnScanner', 'NmapVulnScanner', 0),
    }

    print(f"\n  Sprites ({sprite_size}x{sprite_size}):")
    total_sprites = 0
    for state_name, (folder, prefix, count) in states.items():
        src = os.path.join(status_dir, folder)
        if not os.path.exists(src):
            print(f"    SKIP {state_name}")
            continue

        # Create subfolder for this state (using internal name)
        state_dir = os.path.join(theme_folder, state_name)
        os.makedirs(state_dir, exist_ok=True)

        # Convert status icon (file without number)
        status_icon = os.path.join(src, f"{folder}.png")
        if not os.path.exists(status_icon):
            status_icon = os.path.join(src, f"{folder}.bmp")
        if os.path.exists(status_icon):
            icon_out = os.path.join(state_dir, f"{state_name}.bmp")
            png_to_rgb565_bmp(status_icon, icon_out, sprite_size, bg_color)

        # Convert animation frames
        frames = select_frames(src, prefix, count)
        for i, frame_path in enumerate(frames):
            out_path = os.path.join(state_dir, f"{state_name}{i + 1}.bmp")
            png_to_rgb565_bmp(frame_path, out_path, sprite_size, bg_color)
            total_sprites += 1

    # --- 4. Summary ---
    total_size = sum(
        os.path.getsize(os.path.join(theme_folder, f))
        for f in os.listdir(theme_folder)
        if os.path.isfile(os.path.join(theme_folder, f))
    )

    print(f"\n  Summary:")
    print(f"    Sprites: {total_sprites}")
    print(f"    Total size: {total_size / 1024:.0f}KB ({total_size / 1024 / 1024:.1f}MB)")
    print(f"\n  Copy '{output_dir}/loki/' folder to your SD card root.")


if __name__ == '__main__':
    main()
