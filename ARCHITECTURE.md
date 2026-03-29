# Loki CYD — Architecture & Technical Documentation

## Overview

Loki CYD is an autonomous network reconnaissance virtual pet for the ESP32-based Cheap Yellow Display. It scans networks, discovers hosts, identifies services, brute forces credentials, and displays results through a Tamagotchi-style UI.

**Hardware:** ESP32 dual-core @ 240MHz, 320KB SRAM, 4MB flash
**Display:** ILI9341 (2.8") or ST7796 (3.5") TFT with XPT2046 touch
**Architecture:** Core 0 = recon engine, Core 1 = UI + touch + web server

---

## Phase 1: Host Discovery (ARP Scan)

### Method
We use **ARP scanning** — the same method as `nmap -sn` on local networks. This is the most reliable host discovery method because:

- Every device on a local network **must** respond to ARP requests — it's how IP-to-MAC resolution works at Layer 2
- No firewall can block ARP (it operates below IP)
- Responses are fast (typically <1ms)
- No false negatives — if a device is on the network, it will respond

### Implementation
```
1. Set ARP_TABLE_SIZE=255 (build flag) so the table can hold the entire /24 subnet
2. Send ARP requests for all 254 IPs in the subnet (~250ms total)
3. Wait 3 seconds for all replies to arrive
4. Read the ARP table — every entry with a valid MAC = alive host
```

### Code Location
`loki_recon.cpp` — `sendArpRequest()`, `isHostInArpTable()`, Phase 1a section

### lwIP Functions Used
- `etharp_request(netif, ipaddr)` — sends an ARP WHO-HAS request
- `etharp_find_addr(netif, ipaddr, &eth, &ip)` — checks if an IP has a cached MAC

### Performance
- **Time:** ~3.5 seconds for a full /24 scan (254 hosts)
- **Memory:** 6KB for the ARP table (255 × 24 bytes)
- **Reliability:** 100% for local network hosts

---

## Phase 2: Port Scanning

### Method
**TCP connect scan** — attempts a full TCP handshake on each target port for each alive host. This is the most compatible method (works through NAT, doesn't require raw sockets).

### Current Implementation
```
For each alive host:
    For each of 13 target ports:
        TCP connect with 500ms timeout
        If connection succeeds → port is open (set bit in portMask)
        Close connection
```

### Target Ports (9)
| Bit | Port | Service | Attack |
|-----|------|---------|--------|
| 0 | 21 | FTP | Brute force |
| 1 | 22 | SSH | Brute force (LibSSH) |
| 2 | 23 | Telnet | Brute force |
| 3 | 80 | HTTP | Basic auth brute |
| 4 | 443 | HTTPS | Basic auth brute |
| 5 | 445 | SMB | Detection (NTLM TODO) |
| 6 | 3306 | MySQL | Empty password check |
| 7 | 3389 | RDP | Detection (CredSSP TODO) |
| 8 | 8080 | HTTP alt | Basic auth brute |

### Performance Concerns
**Current approach is sequential and slow:**
- 13 ports × 500ms timeout = **6.5 seconds per host** (worst case, all ports closed)
- 20 alive hosts = **~2 minutes** just for port scanning
- Open ports are faster (connect succeeds immediately)

### Potential Improvements
1. **Reduce timeout** — 200-300ms is usually sufficient for local network
2. **Parallel scanning** — scan multiple ports simultaneously using non-blocking sockets or multiple FreeRTOS tasks
3. **Smart ordering** — try most common ports first (80, 22, 443), skip unlikely ports if device type is already identified
4. **Adaptive timeout** — if first port responds in 5ms, reduce timeout for remaining ports on that host
5. **Batch scanning** — the ESP32 can handle ~5 concurrent TCP connections safely

### Code Location
`loki_recon.cpp` — Phase 1b section

---

## Phase 3: Service Identification

### Method
**Banner grabbing** — connect to each open port and read the service banner/greeting.

### Per-Protocol Identification
| Protocol | Method |
|----------|--------|
| HTTP | Send `GET /`, extract `Server:` header, fingerprint known devices |
| RTSP | Send `OPTIONS`, check response |
| FTP | Read 220 greeting banner |
| SSH | Read version string (e.g., `SSH-2.0-OpenSSH_8.9`) |
| Telnet | Read login prompt banner |
| MySQL | Read greeting packet, extract version string |
| SMB | Negotiate protocol |
| MQTT | Set type based on port |
| Modbus | Set type based on port |
| RDP | Set type based on port |

### Device Type Fingerprinting
HTTP responses are checked for known manufacturer strings:
- Hikvision, Dahua, Reolink → Camera
- TP-Link → Router/IoT
- 401 + RTSP port → Camera with auth

### Code Location
`loki_recon.cpp` — Phase 2 section

---

## Phase 4: Brute Force Attacks

### Credential List
Based on the original Loki dictionary — **cross-product approach:**
- 11 usernames × 20 passwords = 220 combinations
- Plus: blank password for each user (11)
- Plus: same-as-username for each user (11)
- **Total: 242 credential pairs per service**

### Usernames
```
admin, root, user, guest, test, ftp, anonymous, Administrator, pi, ubuntu, kali
```

### Passwords
```
admin, password, 123456, root, guest, test, 1234, 12345, password123, admin123,
changeme, letmein, welcome, qwerty, abc123, raspberry, kali, toor, ubuntu, alpine
```

### Attack Methods

| Protocol | Method | Library |
|----------|--------|---------|
| SSH | Real SSH password auth | LibSSH-ESP32 |
| FTP | USER/PASS commands, checks for 230 response | Raw TCP |
| Telnet | Send user/pass, check for shell prompt | Raw TCP |
| HTTP | Basic auth header, check for non-401 response | Raw TCP |
| RTSP | DESCRIBE with Basic auth, check for 200 | Raw TCP |
| MySQL | Greeting + empty password auth packet | Raw TCP |
| SMB | Negotiate probe (full NTLM TODO) | Raw TCP |
| MQTT | CONNECT packet, check CONNACK | Raw TCP |
| Modbus | Read holding registers (no auth) | Raw TCP |
| RDP | Detection only (CredSSP/NLA TODO) | — |

### Safety Features
- **Connection failure limit:** 5 consecutive TCP connect failures → skip host (rate limiting detected)
- **Progress reporting:** Kill feed updates every 10-20 credential attempts
- **Per-port independence:** Each service is brute forced regardless of other service results on the same host

### Code Location
`loki_recon.cpp` — Phase 3 section

---

## Phase 5: Reporting

### Real-time
- Kill feed (bottom of pet screen) — color-coded attack log
- Stats grid — 9 counters updated in real-time
- Status line — current action name (matches original Loki)
- Pet mood — changes based on activity

### Persistent
- **NVS flash:** Scores (XP, hosts, ports, creds, etc.) survive reboots
- **SD card (optional):** `/loki/reports/` — full scan report with IPs, ports, banners, credentials

---

## Thread Safety

The recon engine runs on Core 0, the UI on Core 1. TFT_eSPI is NOT thread-safe.

**Solution:** Core 0 never touches the display. It sets dirty flags and data:
- `killFeedDirty` — new kill feed line added
- `statusDirty` — status text changed
- `moodDirty` — mood changed
- `statsDirty` — scores changed
- `commentDirty` — new commentary

Core 1's `loop()` checks these flags and draws. This prevents the SPI mutex crash that occurs when both cores try to use the display simultaneously.

---

## Theme System

### Two-tier architecture:
1. **PROGMEM fallback** — background + 7 still sprites (1 per state) baked into flash. Always available, no SD card needed.
2. **SD card themes** — full animated themes with multiple frames per state. Auto-detected and loaded on boot.

### SD Card Layout
```
/loki/
  themes/
    loki/              ← self-contained theme folder
      theme.cfg        ← animation timing, sprite size
      bg.bmp           ← full-screen composite background (RGB565)
      idle1.bmp ...    ← character animation frames
      scan1.bmp ...
      attack1.bmp ...
    pirate/            ← another theme
      ...
  loot/                ← stolen files
  reports/             ← scan reports
  creds.txt            ← custom wordlist (optional)
```

### Theme Config (theme.cfg)
```
name = LOKI
sprite_size = 175
anim_interval_min = 1500
anim_interval_max = 2000
comment_interval_min = 15000
comment_interval_max = 30000
```

### Transparency
Character sprites use magenta `(255,0,255)` / RGB565 `0xF81F` as the transparency key. Semi-transparent edge pixels are pre-blended against the dark theme background to avoid fringing.

---

## Web UI

- **Port:** 80
- **Endpoints:** `/` (dashboard), `/stats` (JSON API), `/start`, `/stop`, `/screenshot` (live BMP capture)
- **Screenshot:** Reads framebuffer via `tft.readRect()`, serves as RGB565 BMP with byte-swap correction
- **Setting persisted:** Web UI on/off state saved to NVS, auto-starts on boot if enabled

---

## WiFi

- **Credentials:** Saved to NVS flash, auto-reconnects on boot
- **No auto-scan:** WiFi connects on boot but user must manually start Auto mode
- **Status:** Header shows "Connected" (green) or "Offline" (dim). Tap for WiFi info or connect shortcut.
