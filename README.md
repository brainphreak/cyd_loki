# Loki CYD

**Autonomous Network Recon Virtual Pet for the Cheap Yellow Display (ESP32)**

<p align="center">
  <img src="https://raw.githubusercontent.com/pineapple-pager-projects/pineapple_pager_loki/main/loki.png" alt="Loki" width="200">
</p>

Loki CYD is a Tamagotchi-style pentesting companion that autonomously scans networks, discovers hosts, identifies services, brute forces credentials, and displays everything through an animated virtual pet interface on the ESP32 CYD touchscreen.

<p align="center">
  <img src="screenshots/cyd-loki-screenshot1.png" width="240" alt="Scanning">
  <img src="screenshots/cyd-loki-screenshot2.png" width="240" alt="Brute Force">
  <img src="screenshots/cyd-loki-screenshot3.png" width="240" alt="Host Discovery">
</p>

<p align="center">
  <img src="screenshots/cyd-loki-menu.png" width="240" alt="Menu">
  <img src="screenshots/cyd-loki-hosts.png" width="240" alt="Hosts">
  <img src="screenshots/cyd-loki-credentials.png" width="240" alt="Credentials">
</p>

<p align="center">
  <img src="screenshots/cyd-loki-host-details.png" width="240" alt="Host Details">
  <img src="screenshots/cyd-loki-attacklog.png" width="240" alt="Attack Log">
  <img src="screenshots/cyd-loki-selecttheme.png" width="240" alt="Theme Picker">
</p>

<p align="center">
  <img src="screenshots/cyd-loki-webui.png" width="600" alt="Web UI">
</p>

## Inspiration & Credits

This project is a port of [Loki](https://github.com/pineapple-pager-projects/pineapple_pager_loki) (originally built for the WiFi Pineapple Pager) to the ESP32 Cheap Yellow Display platform.

The original Loki was itself inspired by [Bjorn](https://github.com/infinition/Bjorn) — the autonomous network reconnaissance Tamagotchi.

Special thanks to [HaleHound-CYD](https://github.com/JesseCHale/HaleHound-CYD) by JesseCHale — an excellent offensive security toolkit for the CYD that demonstrated what's possible on this hardware. HaleHound's IoT Recon module, touch handling, display architecture, and SPI bus management provided invaluable reference for understanding the CYD platform. Some early development was done as a fork of HaleHound before Loki CYD became its own project.

## Features

### Virtual Pet UI
- Animated character with per-state sprite animations (idle, scanning, attacking, stealing, etc.)
- 3x3 stat grid with themed icons (Hosts, Ports, Vulns, Creds, Zombies, Data, NetworkKB, Level, Attacks)
- XP counter with gold rune icon
- Status bar with 42x42 animated status icon + two-line status text
- Speech bubble with randomized Loki-themed commentary
- 6-line color-coded attack log (kill feed)
- WiFi connection status indicator

### Network Reconnaissance
- **ARP Host Discovery** — same method as `nmap -sn` on local networks. Batched ARP scanning finds all alive hosts on the subnet.
- **Port Scanning** — TCP connect scan on 9 target ports per discovered host
- **Service Identification** — Banner grabbing for HTTP, FTP, SSH, Telnet, MySQL with device fingerprinting
- **MAC OUI Vendor Lookup** — 9,673 vendor entries with binary search identify device manufacturers (Apple, Samsung, Cisco, etc.)
- **Device Classification** — Vendor + port analysis classifies devices as Phone, Laptop, Router, Camera, NAS, Printer, IoT, Server, etc.

### Brute Force Attacks
- **SSH** — Real SSH password authentication via LibSSH-ESP32
- **FTP** — USER/PASS protocol with anonymous login detection
- **Telnet** — Login/password prompt detection with shell verification
- **HTTP/HTTPS** — Basic auth brute force (ports 80, 443, 8080)
- **MySQL** — Empty password / no-auth detection
- **SMB** — Service detection and negotiate probe (full NTLM in development)
- **RDP** — Service detection (full CredSSP/NLA in development)

### Credential Management
- Loki's original dictionary: 11 usernames × 20 passwords = 242 credential combinations
- Custom wordlist support via SD card (`/loki/creds.txt`)
- All cracked credentials stored with IP, port, username, password
- Credentials persist to SPIFFS flash (survives reboots)
- Downloadable via web UI

### Theme System
- **Built-in fallback theme** — 7 still sprites + background in PROGMEM, works without SD card
- **SD card animated themes** — Full animation with 50-300 frames per theme
- **6 themes included** — Loki, Loki Dark, Bjorn, Clown, Knight, Pirate
- **Fully customizable** — Colors, layout coordinates, animation timing all configurable via `theme.cfg`
- **Theme picker** — Switch themes from the touchscreen menu
- See [THEME_DEVELOPMENT.md](THEME_DEVELOPMENT.md) for creating custom themes

### Touch Interface
- 11-item main menu (WiFi, Auto, Web UI, Manual, Hosts, Credentials, Attack Log, Stats, Theme, Brightness, Back)
- WiFi network picker with signal strength bars
- On-screen QWERTY keyboard (lower/upper/symbols) with show/hide password toggle
- Scrollable host list with device details
- Manual attack selection per host
- Touch calibration on first boot (3.5" displays)
- Brightness control (25/50/75/100%)

### Web UI
- Dashboard with live stats at `http://<cyd-ip>/`
- `/creds` — JSON credentials download
- `/log` — Plain text attack log
- `/screenshot` — Live BMP screenshot of the display
- `/files` — SPIFFS file listing
- `/download?file=<path>` — Download any stored file
- `/stats` — JSON stats API
- `/start` and `/stop` — Scan control

### Persistence
- **WiFi credentials** — saved to NVS, auto-reconnects on boot
- **Scan results** — credentials, devices, attack log saved to SPIFFS
- **Scores** — XP and all stats persist across reboots
- **Web UI setting** — on/off state persists
- **SD card** — optional, used for themes, loot storage, custom wordlists, scan reports

## Supported Hardware

| Board | Display | Status |
|-------|---------|--------|
| QDtech E32R35T | 3.5" ST7796 320x480 | Primary target, fully tested |
| ESP32-2432S028 | 2.8" ILI9341 240x320 | Builds, needs testing |
| QDtech E32R28T | 2.8" ILI9341 (Type-C) | Builds, needs testing |

## Installation

### Requirements
- [PlatformIO](https://platformio.org/) (CLI or VSCode extension)
- ESP32 CYD board
- MicroSD card (optional, for themes and loot storage)

### Build & Flash
```bash
# Clone
git clone https://github.com/brainphreak/cyd_loki.git
cd cyd_loki

# Build for 3.5" display
pio run -e esp32-e32r35t

# Flash
pio run -e esp32-e32r35t -t upload
```

### SD Card Setup (Optional)
Generate themes using the included tools:
```bash
python3 tools/make_theme_sdcard.py <loki_theme_dir> <output_dir> --name loki --screen 320x480 --sprite 175
```

Copy the `loki/` folder to the SD card root:
```
SD card/
└── loki/
    ├── themes/
    │   ├── loki/
    │   ├── bjorn/
    │   └── ...
    ├── loot/
    ├── reports/
    └── creds.txt (optional custom wordlist)
```

## Feature Comparison

### Loki CYD vs Original Loki (Pager)

| Feature | Pager Loki | CYD Loki | Status |
|---------|-----------|----------|--------|
| ARP Host Discovery | ✅ (via nmap) | ✅ (native ARP) | ✅ Complete |
| Port Scanning | ✅ (40 ports) | ✅ (9 ports) | ⚡ Expanding |
| SSH Brute Force | ✅ (paramiko) | ✅ (LibSSH-ESP32) | ✅ Complete |
| FTP Brute Force | ✅ | ✅ | ✅ Complete |
| Telnet Brute Force | ✅ | ✅ | ✅ Complete |
| HTTP Brute Force | ✅ | ✅ | ✅ Complete |
| SMB Brute Force | ✅ (pysmb) | 🔄 Detection only | 🚧 In Progress |
| MySQL Brute Force | ✅ (pymysql) | 🔄 Empty password | 🚧 In Progress |
| RDP Brute Force | ✅ (xfreerdp) | 🔄 Detection only | 🚧 In Progress |
| Vulnerability Scanning | ✅ (nmap NSE) | ❌ | 📋 Planned |
| File Stealing (FTP) | ✅ | ❌ | 📋 Planned |
| File Stealing (SSH) | ✅ | ❌ | 📋 Planned |
| File Stealing (SMB) | ✅ | ❌ | 📋 Planned |
| File Stealing (Telnet) | ✅ | ❌ | 📋 Planned |
| SQL Data Theft | ✅ | ❌ | 📋 Planned |
| MAC OUI Vendor Lookup | ✅ | ✅ (9,673 entries) | ✅ Complete |
| Device Classification | ✅ | ✅ | ✅ Complete |
| OS Detection | ✅ (nmap) | ❌ | 📋 Planned |
| Hostname Resolution | ✅ (DNS/NetBIOS/mDNS) | ❌ | 📋 Planned |
| Virtual Pet UI | ✅ | ✅ | ✅ Complete |
| Theme System | ✅ (6 themes) | ✅ (6 themes) | ✅ Complete |
| Theme Colors | ✅ | ✅ | ✅ Complete |
| Theme Layout Override | ✅ | ✅ | ✅ Complete |
| Character Animations | ✅ (all frames) | ✅ (all frames) | ✅ Complete |
| Sequential/Random Anim | ✅ | ✅ | ✅ Complete |
| Commentary System | ✅ | ✅ | ✅ Complete |
| Web UI — Dashboard | ✅ (full SPA) | 🔄 Basic | 🚧 In Progress |
| Web UI — Hosts Tab | ✅ | 🔄 JSON API | 🚧 In Progress |
| Web UI — Attacks Tab | ✅ | ❌ | 📋 Planned |
| Web UI — Loot Tab | ✅ | 🔄 JSON API | 🚧 In Progress |
| Web UI — Config Tab | ✅ | ❌ | 📋 Planned |
| Web UI — Terminal | ✅ | ❌ | N/A (no shell) |
| Web UI — Display Tab | ✅ | ✅ (screenshot) | ✅ Complete |
| Battery Indicator | ✅ | ❌ | N/A (no battery) |
| App Handoff | ✅ | ❌ | N/A (single app) |
| Manual Target Entry | ✅ | 🔄 | 🚧 In Progress |
| Attack Log Persistence | ✅ | ✅ (SPIFFS) | ✅ Complete |
| Credential Persistence | ✅ | ✅ (SPIFFS + NVS) | ✅ Complete |
| WiFi Auto-Reconnect | ✅ | ✅ | ✅ Complete |
| Brightness Control | N/A | ✅ | ✅ Complete |
| Touch Calibration | N/A | ✅ | ✅ Complete |
| Screenshot Capture | ✅ | ✅ (via web) | ✅ Complete |

## Architecture

- **Dual-core**: Core 0 = recon engine, Core 1 = UI + touch + web server
- **Thread-safe**: Dirty flag system prevents SPI bus conflicts between cores
- **Flash**: ~72% used (with PROGMEM fallback theme)
- **RAM**: ~25% used
- See [ARCHITECTURE.md](ARCHITECTURE.md) for full technical documentation

## Tools

| Tool | Description |
|------|-------------|
| `tools/make_background.py` | Generate composite background BMP for a theme |
| `tools/convert_sprites.py` | Convert PNG character sprites to RGB565 BMP |
| `tools/make_theme_sdcard.py` | Build complete SD card theme package |
| `tools/bmp_to_progmem.py` | Convert BMP to PROGMEM C header arrays |
| `tools/make_oui_db.py` | Generate OUI vendor lookup table from nmap database |
| `tools/preview_ui.py` | Generate preview mockup of the UI |

## License

This project builds upon work from multiple open-source projects. Please respect their respective licenses.

## Links

- [Original Loki (WiFi Pineapple Pager)](https://github.com/pineapple-pager-projects/pineapple_pager_loki)
- [Bjorn](https://github.com/infinition/Bjorn)
- [HaleHound-CYD](https://github.com/JesseCHale/HaleHound-CYD)
