# Loki CYD Theme Format

Each theme is a self-contained folder on the SD card at `/loki/themes/<name>/`.

## Required Files
- `theme.cfg` — all settings, colors, and layout coordinates
- `bg.bmp` — full-screen background image (320x480 RGB565)
- `idle1.bmp` ... `idleN.bmp` — idle animation frames
- At least one state with animation frames

## Optional Files
- `scan1.bmp` ... `scanN.bmp` — scanning animation
- `attack1.bmp` ... `attackN.bmp` — attacking animation
- `ftp1.bmp`, `telnet1.bmp`, `steal1.bmp`, `vuln1.bmp` ... — per-action animations
- Falls back to `idle` frames if a state has no sprites

## theme.cfg Format

Key-value pairs, one per line. `#` for comments. All coordinates are absolute pixels.

```ini
# === Identity ===
name = LOKI
web_title = LOKI

# === Animation ===
sprite_size = 175
animation_mode = sequential
anim_interval_min = 1500
anim_interval_max = 2000
comment_interval_min = 15000
comment_interval_max = 30000

# === Colors (RGB565 hex) ===
color_bg = 0861
color_surface = 0A43
color_elevated = 1264
color_text = D6B4
color_text_dim = 4CC9
color_accent = 3DE9
color_accent_bright = 7EF6
color_accent_dim = 3464
color_highlight = FE60
color_alert = F81F
color_error = F800
color_success = 07E0
color_cracked = FB56

# === Layout: Header ===
header_y = 0
header_h = 32

# === Layout: XP (in header) ===
xp_x = 168
xp_y = 8

# === Layout: WiFi status (in header) ===
wifi_x = 250
wifi_y = 8

# === Layout: Stats Grid ===
stats_y = 34
stats_rows = 3
stats_cols = 3
stats_row_h = 30
stats_icon_size = 22

# === Layout: Status Bar ===
status_y = 128
status_h = 34
status_icon_x = 4
status_text_x = 36

# === Layout: Dialogue/Comment Bubble ===
dialogue_x = 6
dialogue_y = 168
dialogue_w = 308
dialogue_h = 54

# === Layout: Character ===
char_x = 72
char_y = 230
char_w = 175
char_h = 175

# === Layout: Kill Feed ===
killfeed_y = 418
killfeed_lines = 4
killfeed_line_h = 14
```

## Color Format
Colors are 16-bit RGB565 in hex (no 0x prefix in the cfg file).
Convert from RGB: `((R & 0xF8) << 8) | ((G & 0xFC) << 3) | (B >> 3)`

## Layout Notes
- All coordinates are absolute pixels for the target screen (320x480)
- The background BMP should have static elements (icons, borders, title) pre-rendered
- Dynamic elements (text values, character sprite) are drawn on top at the specified coordinates
- If a layout key is not specified, the firmware uses computed defaults
