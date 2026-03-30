# Loki CYD — Theme Development Guide

Create custom themes for your Loki CYD with unique characters, colors, and layouts.

## Theme Structure

Each theme is a self-contained folder on the SD card:

```
/loki/themes/my_theme/
├── theme.cfg           ← Settings, colors, and layout
├── bg.bmp              ← Full-screen background (320x480 RGB565)
├── idle1.bmp           ← Idle animation frame 1
├── idle2.bmp           ← Idle animation frame 2
├── ...                 ← As many frames as you want
├── scan1.bmp           ← Network scanning animation
├── attack1.bmp         ← Brute force animation
├── ftp1.bmp            ← FTP attack animation
├── telnet1.bmp         ← Telnet attack animation
├── steal1.bmp          ← File stealing animation
└── vuln1.bmp           ← Vulnerability scan animation
```

## Required Files

- **`theme.cfg`** — Configuration file (see below)
- **`bg.bmp`** — Full-screen background image, 320x480 pixels, RGB565 format
- **At least `idle1.bmp`** — One idle frame minimum

## Optional Animation Frames

Each state can have any number of frames. The firmware auto-detects how many exist by looking for sequential numbered files (e.g., `idle1.bmp`, `idle2.bmp`, ... `idle89.bmp`).

If a state has no frames, it falls back to the `idle` frames.

| State | Folder in Original Loki | When Displayed |
|-------|------------------------|----------------|
| `idle` | IDLE | Default / waiting |
| `scan` | NetworkScanner | Scanning network |
| `attack` | SSHBruteforce | Brute forcing |
| `ftp` | FTPBruteforce | FTP attack |
| `telnet` | TelnetBruteforce | Telnet attack |
| `steal` | StealFilesSSH | File exfiltration |
| `vuln` | NmapVulnScanner | Vulnerability scan |

## Character Sprite Format

- **Size**: Defined by `sprite_size` in theme.cfg (typically 175x175)
- **Format**: RGB565 BMP (16-bit, BI_BITFIELDS, top-down)
- **Transparency**: Magenta `(255, 0, 255)` / RGB565 `0xF81F` is the transparency key
- **Edge blending**: Semi-transparent edges should be pre-blended against a dark background to avoid fringing

### Converting from PNG

Use the included tool:
```bash
python3 tools/convert_sprites.py <source_theme_dir> <output_dir> --size 175
```

This handles:
- Resizing to the target size
- Alpha channel → magenta transparency key
- Edge anti-aliasing blended against dark background
- RGB565 BMP output

## Background Image (bg.bmp)

The background contains all **static** UI elements baked in:
- Title text ("LOKI" or your theme name)
- XP icon (gold rune)
- Stat grid with 9 icons and grid lines
- Status bar background
- Speech bubble outline with tail
- Kill feed separator
- Any decorative elements

**Dynamic elements** are drawn on top at coordinates specified in theme.cfg:
- XP value (number)
- WiFi status text
- 9 stat values (numbers)
- Status icon + text
- Comment text inside speech bubble
- Character animation sprite
- Kill feed text lines

### Generating a Background

```bash
python3 tools/make_background.py <source_theme_dir> <output_dir> --width 320 --height 480
```

## theme.cfg Reference

```ini
# === Identity ===
name = MY THEME

# === Animation ===
sprite_size = 175
animation_mode = sequential    # "sequential" or "random"
anim_interval_min = 1500       # Min ms between frames
anim_interval_max = 2000       # Max ms between frames
comment_interval_min = 15000   # Min ms between comments
comment_interval_max = 30000   # Max ms between comments

# === Colors (RGB565 hex, no 0x prefix) ===
color_bg = 10A2               # Main background
color_surface = 1903           # Header / status bar background
color_elevated = 2144          # Speech bubble fill
color_text = 868D              # Primary text (stat values, comments)
color_text_dim = 42A8          # Secondary text (status detail)
color_accent = 65EB            # Accent color (status action name)
color_accent_bright = 868D     # Bright accent
color_accent_dim = 42A8        # Dim accent
color_highlight = B506         # XP value color
color_alert = 5DAA             # Alert color
color_error = 61E7             # Error color
color_success = 5DAA           # Success / connected
color_cracked = B506           # Cracked credential highlight

# === Layout (absolute pixel coordinates for 320x480) ===
header_y = 0
header_h = 32
xp_x = 162                    # XP value X position (right of gold icon)
xp_y = 16                     # XP value Y position (vertically centered)
wifi_x = 235                  # WiFi status X
wifi_y = 16                   # WiFi status Y
stats_y = 34                  # Stats grid top
stats_rows = 3
stats_cols = 3
stats_row_h = 30              # Height per stat row
stats_icon_size = 22           # Stat icon size (baked into bg)
status_y = 125                # Status bar top
status_h = 45                 # Status bar height
status_icon_x = 4             # Status icon X
status_text_x = 41            # Status text X (right of icon)
dialogue_x = 5                # Speech bubble X
dialogue_y = 174              # Speech bubble Y
dialogue_w = 309              # Speech bubble width
dialogue_h = 54               # Speech bubble height
char_x = 72                   # Character sprite X
char_y = 236                  # Character sprite Y
char_w = 175                  # Character area width
char_h = 175                  # Character area height
killfeed_y = 416              # Kill feed top
killfeed_lines = 6            # Number of log lines
killfeed_line_h = 10          # Height per log line
```

## RGB565 Color Conversion

Colors in theme.cfg use 16-bit RGB565 hex format (4 hex digits, no `0x` prefix).

To convert from RGB888:
```python
def rgb_to_565(r, g, b):
    return '{:04X}'.format(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3))

# Example: green (94, 189, 69)
print(rgb_to_565(94, 189, 69))  # → "5DAA"
```

Common colors:
| Color | RGB | RGB565 |
|-------|-----|--------|
| Black | 0,0,0 | 0000 |
| White | 255,255,255 | FFFF |
| Red | 255,0,0 | F800 |
| Green | 0,255,0 | 07E0 |
| Blue | 0,0,255 | 001F |
| Yellow | 255,255,0 | FFE0 |
| Cyan | 0,255,255 | 07FF |
| Magenta (transparent) | 255,0,255 | F81F |

## Layout Customization

All coordinates are absolute pixels for the target screen (320x480). When creating a custom theme:

1. Design your background image with static elements positioned where you want them
2. Set the coordinates in theme.cfg to match where you placed things in the background
3. Dynamic text and sprites will be drawn at those coordinates

If you omit a layout value from theme.cfg, the firmware uses sensible defaults.

## Creating a Theme from Scratch

1. **Design your character** — Create sprite sheets for each state (idle, scan, attack, etc.). Export as numbered PNGs with transparency.

2. **Design your background** — Create a 320x480 image with your static elements (title, icons, borders, decorations). Export as PNG.

3. **Convert sprites**:
   ```bash
   python3 tools/convert_sprites.py my_sprites/ output/ --size 175
   ```

4. **Convert background**:
   ```bash
   python3 tools/make_background.py my_theme/ output/ --width 320 --height 480
   ```
   Or manually convert your PNG to RGB565 BMP.

5. **Write theme.cfg** — Copy the reference above and customize colors and coordinates.

6. **Copy to SD card** — Place everything in `/loki/themes/my_theme/`

7. **Test** — Boot the CYD and select your theme from the menu.

## Tips

- **Animation mode**: Use `sequential` for smooth looping animations, `random` for varied idle poses
- **Sprite count**: More frames = smoother animation but more SD card space (~60KB per 175x175 frame)
- **Transparency**: Use exact magenta `(255,0,255)` for transparent areas. Avoid magenta in your actual artwork.
- **Edge blending**: Anti-aliased edges should blend into a dark color matching your theme's background, not into magenta
- **Background**: Bake as much as possible into the background image — it's drawn once and is fast. Dynamic text redraws are slower.
- **Testing**: Use the web UI screenshot at `http://<cyd-ip>/screenshot` to check your theme remotely
