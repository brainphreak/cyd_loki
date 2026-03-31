# SD Card Contents for Loki CYD

Copy the `loki/` folder to the root of a FAT32-formatted MicroSD card.

## Structure
```
SD card root/
└── loki/
    ├── themes/           ← 8 themes included (pre-built)
    │   ├── bjorn/
    │   ├── clown/
    │   ├── clown_dark/
    │   ├── knight/
    │   ├── loki/
    │   ├── loki_dark/    ← Default theme
    │   ├── pirate/
    │   └── pirate_dark/
    ├── loot/             ← Stolen files (auto-created)
    ├── reports/          ← Scan reports (auto-created)
    ├── oui.txt           ← Full 35K+ MAC OUI vendor database
    └── creds.txt         ← Custom wordlist (optional)
```

## Each Theme Contains
```
<theme>/
    theme.cfg             ← Colors, layout, per-element styles
    bg.bmp                ← 320x480 RGB565 background
    comments.txt          ← Per-state commentary
    idle/                 ← Idle sprites + status icon
    scan/                 ← Scanning sprites + icon
    attack/               ← Attacking sprites + icon
    ftp/                  ← FTP brute force sprites + icon
    telnet/               ← Telnet brute force sprites + icon
    steal/                ← File stealing sprites + icon
    vuln/                 ← Vuln scanning sprites + icon
```

See [THEME_FORMAT.md](../THEME_FORMAT.md) for the full theme.cfg specification.

## Regenerating Themes

Themes can be regenerated from the original Loki project's theme assets:

```bash
python3 src/tools/make_theme_sdcard.py <original_loki_theme_dir> sdcard_contents --name <theme_name>
```

## Custom Wordlist

Create `loki/creds.txt` on the SD card with one credential per line:
```
username:password
admin:secret
root:toor
```
Lines starting with `#` are comments. Lines without `:` assume username is "admin".

These credentials are added ON TOP of the 242 built-in combinations.
