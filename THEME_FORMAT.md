# Loki CYD Theme Format

Each theme is a self-contained folder on the SD card at `/loki/themes/<name>/`.

## Folder Structure

```
/loki/themes/<name>/
    theme.cfg           ← All settings, colors, layout, per-element styles
    bg.bmp              ← Full-screen background (320x480 RGB565)
    comments.txt        ← Per-state commentary lines
    idle/               ← Idle state sprites
        idle_icon.bmp   ← 42x42 status icon (transparent bg)
        idle1.bmp       ← Animation frame 1
        idle2.bmp       ← Animation frame 2
        ...
    scan/               ← Scanning state
        scan_icon.bmp
        scan1.bmp ...
    attack/             ← Attacking state
        attack_icon.bmp
        attack1.bmp ...
    ftp/                ← FTP brute force
    telnet/             ← Telnet brute force
    steal/              ← File stealing
    vuln/               ← Vulnerability scanning
```

## Sprite Files

- **Status icons** (`<state>_icon.bmp`) — 42x42 RGB565, magenta (0xF81F) transparency. Edge pixels blended against the theme's surface color.
- **Animation frames** (`<state>N.bmp`) — 175x175 RGB565, magenta transparency. Edge pixels blended against the theme's bg color.
- Files WITH a number in the name = animation frames (sorted by number)
- Files WITHOUT a number = status icon for that state
- If a state has no subfolder, the firmware falls back to `idle` frames

## comments.txt Format

```ini
[idle]
Waiting for mischief...
The network sleeps. I do not.

[scan]
Sniffing the network...
Who dares connect to this subnet?

[attack]
Knocking on the door...
Your defaults betray you.
```

## theme.cfg Format

Key-value pairs, one per line. `#` for comments. All coordinates are absolute pixels for 320x480.

### Basic Settings

```ini
name = LOKI Dark
sprite_size = 175
animation_mode = sequential    # or "random"
anim_interval_min = 1500       # ms between frames
anim_interval_max = 2000
comment_interval_min = 15000   # ms between commentary
comment_interval_max = 30000
```

### Base Palette (RGB565 hex)

```ini
color_bg = 0000                # Main background
color_surface = 0841           # Header/status bar panels
color_elevated = 10A2          # Dialogue box fill
color_text = 0647              # Primary text
color_text_dim = 0283          # Muted text
color_accent = 0465            # Primary accent (action names)
color_accent_bright = 2DAA     # Bright accent
color_accent_dim = 0320        # Dim accent
color_highlight = CDA6         # XP/gold values
color_alert = CA99             # Alert messages
color_error = C986             # Error text
color_success = 0465           # Success/connected
color_cracked = FB36           # Cracked credentials
```

### Layout Coordinates

```ini
# Header
header_y = 0
header_h = 32

# Stats Grid (3x3)
stats_y = 34
stats_rows = 3
stats_cols = 3
stats_row_h = 30
stats_icon_size = 22

# Status Bar
status_y = 125
status_h = 45
status_icon_x = 4
status_text_x = 55

# Dialogue/Comment Bubble
dialogue_x = 5
dialogue_y = 174
dialogue_w = 309
dialogue_h = 54

# Character Sprite
char_x = 72
char_y = 240
char_w = 175
char_h = 175

# Kill Feed
killfeed_y = 423
killfeed_lines = 5
killfeed_line_h = 10
```

### Per-Element Styles

Every dynamic UI element can have its own position, font, color, and text alignment.

**Font values:** 1 = 8px monospace, 2 = 16px proportional (default), 4 = 26px large

**Datum values (text alignment):** 0=TL, 1=TC, 2=TR, 3=ML, 4=MC, 5=MR, 6=BL, 7=BC, 8=BR

```ini
# XP value (in header, right of gold icon)
xp_val_x = 158
xp_val_y = 16
xp_val_font = 2
xp_val_color = CDA6            # Gold
xp_val_datum = 3                # ML (middle-left)

# WiFi status text (in header, right side)
wifi_text_x = 235
wifi_text_y = 16
wifi_text_font = 2
wifi_text_datum = 5             # MR (middle-right)
wifi_color_on = 0465            # Connected color
wifi_color_off = 0283           # Offline color

# Status bar line 1 (action name, e.g. "NetworkScanner")
status_line1_x = 55
status_line1_y = 130
status_line1_font = 2
status_line1_color = 0465       # Accent color
status_line1_datum = 0          # TL

# Status bar line 2 (detail text, e.g. "192.168.1.1")
status_line2_x = 55
status_line2_y = 148
status_line2_font = 2
status_line2_color = 0647       # Text color
status_line2_datum = 0          # TL

# Status icon position/size
status_icon_y = 126
status_icon_size = 42

# Comment/dialogue text
comment_text_x = 13
comment_text_y = 182
comment_text_font = 2
comment_text_color = 0647
```

### Per-Stat Styles (9 stats)

Each of the 9 stats can have individual position, font, color, and alignment.

Stats order: 0=Targets, 1=Ports, 2=Vulns, 3=Creds, 4=Zombies, 5=Data, 6=NetworkKB, 7=Level, 8=Attacks

```ini
stat0_x = 33
stat0_y = 50
stat0_font = 2
stat0_color = 0647
stat0_datum = 3

stat1_x = 139
stat1_y = 50
stat1_font = 2
stat1_color = 0647
stat1_datum = 3

# ... stat2 through stat8 follow the same pattern
```

### Kill Feed Colors

8 color types for different message categories in the kill feed.

```ini
kf_color_info = 07FF            # Info/status (cyan)
kf_color_found = 2DAA           # Host/port discovered
kf_color_success = 0465         # Successful operations
kf_color_cracked = FB36         # Credentials cracked!
kf_color_dim = 0283             # Blocked/locked/failed
kf_color_attack = F81F          # Attack start (magenta)
kf_color_error = F800           # Errors (red)
kf_color_xp = CDA6             # XP gains (gold)
kf_font = 1                    # Font for kill feed (1=small, 2=medium)
kf_bg_color = 0000             # Kill feed background (usually black)
```

## Color Format

Colors are 16-bit RGB565 in hex (no `0x` prefix in the cfg file).

Convert from 8-bit RGB:
```
RGB565 = ((R & 0xF8) << 8) | ((G & 0xFC) << 3) | (B >> 3)
```

Example: RGB(0, 140, 40) → `0x0465`

## Building Themes

Use the included tools to generate themes from the original Loki project's assets:

```bash
# Generate a complete theme (bg.bmp + sprites + icons + theme.cfg)
python3 src/tools/make_theme_sdcard.py <loki_theme_dir> <output_dir> --name <name>

# Generate comments from theme's comments.json
python3 src/tools/make_comments.py <loki_theme_dir> <output_dir>/<name>/comments.txt
```

The tools automatically:
- Compute per-element positions matching the background layout
- Derive RGB565 colors from the theme's RGB palette
- Edge-blend character sprites against bg_color
- Edge-blend status icons against surface_color
- Auto-detect and remove white/colored backgrounds from non-alpha source images
- Upscale small source sprites to fill the target frame size
