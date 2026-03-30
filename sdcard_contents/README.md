# SD Card Contents for Loki CYD

Copy the `loki/` folder to the root of your MicroSD card.

## Structure
```
SD card root/
└── loki/
    ├── themes/        ← Theme folders (generate with tools)
    │   ├── loki/
    │   ├── bjorn/
    │   ├── clown/
    │   ├── knight/
    │   ├── loki_dark/
    │   └── pirate/
    ├── loot/           ← Stolen files (auto-created)
    ├── reports/         ← Scan reports (auto-created)
    └── creds.txt        ← Custom wordlist (optional)
```

## Generating Themes
Themes are too large for git (~55MB for all 6). Generate them using:

```bash
python3 tools/make_theme_sdcard.py <original_loki_theme_dir> sdcard_contents --name <theme_name> --screen 320x480 --sprite 175
```

Or generate all at once — see the main README for instructions.

## Custom Wordlist
Create `loki/creds.txt` on the SD card with one credential per line:
```
username:password
admin:secret
root:toor
```
Lines starting with # are comments. Lines without : assume username is "admin".

