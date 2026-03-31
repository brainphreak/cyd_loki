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
    anim_mode = theme_json.get('animation_mode', 'sequential')

    # --- Compute colors (RGB → RGB565) ---
    def rgb565(r, g, b):
        return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)

    def rgb565_hex(r, g, b):
        return f"{rgb565(r, g, b):04X}"

    bg_rgb = tuple(theme_json.get('bg_color', [10, 18, 10]))
    text_rgb = tuple(theme_json.get('text_color', [0, 0, 0]))
    accent_rgb = tuple(theme_json.get('accent_color', [0, 0, 0]))
    title_rgb = tuple(theme_json.get('title_font_color', [100, 190, 90]))
    menu = theme_json.get('menu_colors', {})
    dim_rgb = tuple(menu.get('dim', [70, 85, 70]))
    warning_rgb = tuple(menu.get('warning', [180, 160, 50]))

    is_light = sum(bg_rgb) > 384

    if is_light:
        surface_rgb = tuple(max(c - 15, 0) for c in bg_rgb)
        elevated_rgb = tuple(max(c - 25, 0) for c in bg_rgb)
    else:
        surface_rgb = tuple(min(c + 10, 255) for c in bg_rgb)
        elevated_rgb = tuple(min(c + 20, 255) for c in bg_rgb)

    # Web colors for accent bright/dim (parse from hex if available)
    web = theme_json.get('web_colors', {})
    def hex_to_rgb(h):
        h = h.lstrip('#')
        return (int(h[0:2], 16), int(h[2:4], 16), int(h[4:6], 16))

    accent_bright_rgb = hex_to_rgb(web['accent_bright']) if 'accent_bright' in web else tuple(min(c + 40, 255) for c in accent_rgb)
    accent_dim_rgb = hex_to_rgb(web['accent_dim']) if 'accent_dim' in web else tuple(max(c - 40, 0) for c in accent_rgb)
    text_dim_rgb = hex_to_rgb(web['text_muted']) if 'text_muted' in web else dim_rgb

    # Mood-specific colors
    highlight_rgb = warning_rgb  # Gold/XP color
    success_rgb = accent_rgb if not is_light else (20, 130, 20)  # Darker green for light bg
    error_rgb = (200, 50, 50)
    cracked_rgb = (255, 100, 180)
    alert_rgb = (200, 80, 200)

    # WiFi text colors — must be readable on surface_rgb background
    if is_light:
        wifi_on_rgb = text_rgb           # Same as regular text for light themes
        wifi_off_rgb = (140, 140, 140)   # Medium grey, visible but muted
    else:
        wifi_on_rgb = success_rgb        # Theme accent for dark themes
        wifi_off_rgb = text_dim_rgb      # Dim text

    # --- Compute layout (must match make_background.py) ---
    sx = screen_w / 222.0
    sy = screen_h / 480.0

    header_y = 0
    header_h = int(32 * sy)
    icon_size = min(int(18 * sx), 22)
    row_h = icon_size + 8
    stats_y = int(34 * sy)
    col_w = screen_w // 3
    stats_h = row_h * 3

    stats_bottom = stats_y + stats_h
    status_y = stats_bottom + 1
    status_h = 45

    dlg_x = int(4 * sx)
    dlg_y = status_y + status_h + int(4 * sy)
    dlg_w = screen_w - int(8 * sx)
    dlg_h = int(54 * sy)

    char_w_px = sprite_size
    char_x = (screen_w - char_w_px) // 2
    char_y = dlg_y + dlg_h + int(8 * sy) + 4  # +4 for bubble tail
    char_h_px = sprite_size

    kf_y = char_y + char_h_px + int(8 * sy)
    kf_line_h = max(10, (screen_h - kf_y) // 6)
    kf_lines = min(6, (screen_h - kf_y) // kf_line_h)

    # XP value position (right of baked-in gold icon)
    xp_icon_size = int(header_h * 0.7)
    xp_icon_x = screen_w // 2 - xp_icon_size - 4
    xp_val_x = xp_icon_x + xp_icon_size + 2
    xp_val_y = header_h // 2

    # WiFi text position
    wifi_x = screen_w - 85
    wifi_y = header_h // 2

    # Status icon/text
    status_icon_x = 4
    status_icon_size = 42
    status_icon_y = status_y + (status_h - status_icon_size) // 2
    status_text_x = status_icon_x + status_icon_size + 9

    # Per-stat positions (right of grid icon)
    stat_positions = []
    for i in range(9):
        col = i % 3
        row = i // 3
        sx_pos = col * col_w + icon_size + 11
        sy_pos = stats_y + row * row_h + row_h // 2 + 1
        stat_positions.append((sx_pos, sy_pos))

    # Comment text position (inside dialogue box)
    comment_x = dlg_x + 8
    comment_y = dlg_y + 8

    # Kill feed colors from theme — use accent/text colors to stay on-theme
    if is_light:
        kf_info = (0, 100, 180)       # Blue for light themes
        kf_found = accent_bright_rgb
        kf_success = accent_rgb
        kf_cracked = cracked_rgb
        kf_dim = text_dim_rgb
        kf_attack = alert_rgb
        kf_error = error_rgb
        kf_xp = highlight_rgb
    else:
        kf_info = (0, 255, 255)       # Cyan for dark themes
        kf_found = accent_bright_rgb
        kf_success = accent_rgb
        kf_cracked = cracked_rgb
        kf_dim = text_dim_rgb
        kf_attack = (255, 0, 255)     # Magenta
        kf_error = (255, 0, 0)
        kf_xp = highlight_rgb

    # --- Write theme.cfg ---
    with open(cfg_path, 'w') as f:
        f.write(f"# Loki CYD Theme Configuration\n")
        f.write(f"# Theme: {display_name}\n\n")

        # Basic settings
        f.write(f"name = {display_name}\n")
        f.write(f"sprite_size = {sprite_size}\n")
        f.write(f"animation_mode = {anim_mode}\n")
        f.write(f"anim_interval_min = {anim_min}\n")
        f.write(f"anim_interval_max = {anim_max}\n")
        f.write(f"comment_interval_min = {comment_min}\n")
        f.write(f"comment_interval_max = {comment_max}\n")

        # Base palette
        f.write(f"\n# Base palette (RGB565 hex)\n")
        f.write(f"color_bg = {rgb565_hex(*bg_rgb)}\n")
        f.write(f"color_surface = {rgb565_hex(*surface_rgb)}\n")
        f.write(f"color_elevated = {rgb565_hex(*elevated_rgb)}\n")
        f.write(f"color_text = {rgb565_hex(*text_rgb)}\n")
        f.write(f"color_text_dim = {rgb565_hex(*text_dim_rgb)}\n")
        f.write(f"color_accent = {rgb565_hex(*accent_rgb)}\n")
        f.write(f"color_accent_bright = {rgb565_hex(*accent_bright_rgb)}\n")
        f.write(f"color_accent_dim = {rgb565_hex(*accent_dim_rgb)}\n")
        f.write(f"color_highlight = {rgb565_hex(*highlight_rgb)}\n")
        f.write(f"color_alert = {rgb565_hex(*alert_rgb)}\n")
        f.write(f"color_error = {rgb565_hex(*error_rgb)}\n")
        f.write(f"color_success = {rgb565_hex(*success_rgb)}\n")
        f.write(f"color_cracked = {rgb565_hex(*cracked_rgb)}\n")

        # Layout: Header
        f.write(f"\n# Layout\n")
        f.write(f"header_y = {header_y}\n")
        f.write(f"header_h = {header_h}\n")
        f.write(f"xp_x = {xp_icon_x}\n")
        f.write(f"xp_y = {xp_val_y}\n")
        f.write(f"wifi_x = {wifi_x}\n")
        f.write(f"wifi_y = {wifi_y}\n")

        # Layout: Stats
        f.write(f"stats_y = {stats_y}\n")
        f.write(f"stats_rows = 3\n")
        f.write(f"stats_cols = 3\n")
        f.write(f"stats_row_h = {row_h}\n")
        f.write(f"stats_icon_size = {icon_size}\n")

        # Layout: Status
        f.write(f"status_y = {status_y}\n")
        f.write(f"status_h = {status_h}\n")
        f.write(f"status_icon_x = {status_icon_x}\n")
        f.write(f"status_text_x = {status_text_x}\n")

        # Layout: Dialogue
        f.write(f"dialogue_x = {dlg_x}\n")
        f.write(f"dialogue_y = {dlg_y}\n")
        f.write(f"dialogue_w = {dlg_w}\n")
        f.write(f"dialogue_h = {dlg_h}\n")

        # Layout: Character
        f.write(f"char_x = {char_x}\n")
        f.write(f"char_y = {char_y}\n")
        f.write(f"char_w = {char_w_px}\n")
        f.write(f"char_h = {char_h_px}\n")

        # Layout: Kill Feed
        f.write(f"killfeed_y = {kf_y}\n")
        f.write(f"killfeed_lines = {kf_lines}\n")
        f.write(f"killfeed_line_h = {kf_line_h}\n")

        # =========================================================
        # Per-element customization
        # =========================================================
        f.write(f"\n# Per-element: XP value\n")
        f.write(f"xp_val_x = {xp_val_x}\n")
        f.write(f"xp_val_y = {xp_val_y}\n")
        f.write(f"xp_val_font = 2\n")
        f.write(f"xp_val_color = {rgb565_hex(*highlight_rgb)}\n")
        f.write(f"xp_val_datum = 3\n")  # ML_DATUM

        f.write(f"\n# Per-element: WiFi status\n")
        f.write(f"wifi_text_x = {wifi_x}\n")
        f.write(f"wifi_text_y = {wifi_y}\n")
        f.write(f"wifi_text_font = 2\n")
        f.write(f"wifi_text_datum = 5\n")  # MR_DATUM
        f.write(f"wifi_color_on = {rgb565_hex(*wifi_on_rgb)}\n")
        f.write(f"wifi_color_off = {rgb565_hex(*wifi_off_rgb)}\n")

        f.write(f"\n# Per-element: Status bar\n")
        f.write(f"status_line1_x = {status_text_x}\n")
        f.write(f"status_line1_y = {status_y + 5}\n")
        f.write(f"status_line1_font = 2\n")
        f.write(f"status_line1_color = {rgb565_hex(*accent_rgb)}\n")
        f.write(f"status_line1_datum = 0\n")  # TL_DATUM
        f.write(f"status_line2_x = {status_text_x}\n")
        f.write(f"status_line2_y = {status_y + 23}\n")
        f.write(f"status_line2_font = 2\n")
        f.write(f"status_line2_color = {rgb565_hex(*text_rgb)}\n")
        f.write(f"status_line2_datum = 0\n")  # TL_DATUM
        f.write(f"status_icon_y = {status_icon_y}\n")
        f.write(f"status_icon_size = {status_icon_size}\n")

        f.write(f"\n# Per-element: Comment/dialogue\n")
        f.write(f"comment_text_x = {comment_x}\n")
        f.write(f"comment_text_y = {comment_y}\n")
        f.write(f"comment_text_font = 2\n")
        f.write(f"comment_text_color = {rgb565_hex(*text_rgb)}\n")

        # Per-stat positions
        f.write(f"\n# Per-stat positions and styles\n")
        for i, (sx_pos, sy_pos) in enumerate(stat_positions):
            f.write(f"stat{i}_x = {sx_pos}\n")
            f.write(f"stat{i}_y = {sy_pos}\n")
            f.write(f"stat{i}_font = 2\n")
            f.write(f"stat{i}_color = {rgb565_hex(*text_rgb)}\n")
            f.write(f"stat{i}_datum = 3\n")  # ML_DATUM

        # Kill feed colors
        f.write(f"\n# Kill feed colors\n")
        f.write(f"kf_color_info = {rgb565_hex(*kf_info)}\n")
        f.write(f"kf_color_found = {rgb565_hex(*kf_found)}\n")
        f.write(f"kf_color_success = {rgb565_hex(*kf_success)}\n")
        f.write(f"kf_color_cracked = {rgb565_hex(*kf_cracked)}\n")
        f.write(f"kf_color_dim = {rgb565_hex(*kf_dim)}\n")
        f.write(f"kf_color_attack = {rgb565_hex(*kf_attack)}\n")
        f.write(f"kf_color_error = {rgb565_hex(*kf_error)}\n")
        f.write(f"kf_color_xp = {rgb565_hex(*kf_xp)}\n")
        f.write(f"kf_font = 1\n")
        f.write(f"kf_bg_color = 0000\n")  # Always black

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
    # Edge blending: character sprites blend against bg_color (they sit on main bg)
    # Status icons blend against surface_color (they sit on the status bar)
    theme_bg = tuple(theme_json.get('bg_color', [10, 18, 10]))
    icon_blend_bg = surface_rgb  # Status icons sit on surface-colored status bar

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

        # Convert status icon (file without number) at 42x42 for status bar
        # Use auto-background removal: detect corner color and replace with magenta
        status_icon = os.path.join(src, f"{folder}.png")
        if not os.path.exists(status_icon):
            status_icon = os.path.join(src, f"{folder}.bmp")
        if os.path.exists(status_icon):
            icon_out = os.path.join(state_dir, f"{state_name}_icon.bmp")
            png_to_rgb565_bmp(status_icon, icon_out, 42, bg_color, blend_bg=icon_blend_bg)

        # Convert animation frames
        frames = select_frames(src, prefix, count)
        for i, frame_path in enumerate(frames):
            out_path = os.path.join(state_dir, f"{state_name}{i + 1}.bmp")
            png_to_rgb565_bmp(frame_path, out_path, sprite_size, bg_color, blend_bg=theme_bg)
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
