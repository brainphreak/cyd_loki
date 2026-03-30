#!/usr/bin/env python3
"""
Generate a compact OUI lookup table for Loki CYD.
Selects the most common/useful vendors and generates a PROGMEM C header.

Usage:
    python3 make_oui_db.py <nmap-mac-prefixes-file> <output.h>
"""

import sys
import os
from collections import defaultdict

# Vendors we care about — prioritized list
# These map vendor name keywords to short display names
VENDOR_MAP = {
    # Mobile devices
    'Apple': 'Apple',
    'Samsung': 'Samsung',
    'Google': 'Google',
    'OnePlus': 'OnePlus',
    'Xiaomi': 'Xiaomi',
    'Huawei Device': 'Huawei',
    'Huawei Technologies': 'Huawei',
    'Oppo': 'Oppo',
    'vivo Mobile': 'Vivo',
    'Motorola Mobility': 'Motorola',
    'LG Electronics': 'LG',
    'Sony Mobile': 'Sony',
    'Nokia': 'Nokia',
    'Realme': 'Realme',

    # Computers
    'Dell': 'Dell',
    'Hewlett Packard': 'HP',
    'Lenovo': 'Lenovo',
    'ASUSTek': 'ASUS',
    'Acer': 'Acer',
    'Intel Corporate': 'Intel',
    'Intel': 'Intel',
    'Microsoft': 'Microsoft',
    'Micro-Star': 'MSI',

    # Network equipment
    'Cisco Systems': 'Cisco',
    'Cisco': 'Cisco',
    'Ubiquiti': 'Ubiquiti',
    'Netgear': 'Netgear',
    'TP-Link': 'TP-Link',
    'TP-LINK': 'TP-Link',
    'D-Link': 'D-Link',
    'Linksys': 'Linksys',
    'Aruba': 'Aruba',
    'MikroTik': 'MikroTik',
    'Juniper': 'Juniper',
    'Zyxel': 'Zyxel',

    # IoT / Smart Home
    'Espressif': 'ESP32/ESP8266',
    'Amazon Technologies': 'Amazon',
    'Amazon': 'Amazon',
    'Sonos': 'Sonos',
    'Ring': 'Ring',
    'Shelly': 'Shelly',
    'Tuya': 'Tuya',
    'Philips Lighting': 'Philips Hue',
    'Signify': 'Philips Hue',
    'Nest': 'Nest',
    'ecobee': 'ecobee',
    'Meross': 'Meross',

    # NAS
    'Synology': 'Synology',
    'QNAP': 'QNAP',
    'Buffalo': 'Buffalo',

    # Cameras
    'Hikvision': 'Hikvision',
    'Dahua': 'Dahua',
    'Reolink': 'Reolink',
    'Axis Communications': 'Axis',
    'Amcrest': 'Amcrest',
    'Foscam': 'Foscam',

    # Media
    'Roku': 'Roku',
    'Nintendo': 'Nintendo',
    'Sony Interactive': 'PlayStation',
    'Chromecast': 'Chromecast',

    # Printers
    'Brother': 'Brother',
    'Canon': 'Canon',
    'Epson': 'Epson',
    'Xerox': 'Xerox',

    # Servers / VM
    'VMware': 'VMware',
    'Oracle': 'Oracle',
    'Raspberry Pi': 'Raspberry Pi',
    'Supermicro': 'Supermicro',

    # Other
    'Broadcom': 'Broadcom',
    'Qualcomm': 'Qualcomm',
    'Realtek': 'Realtek',
    'Tesla': 'Tesla',
    'Arris': 'Arris',
}


def main():
    if len(sys.argv) < 3:
        print(f"Usage: {sys.argv[0]} <nmap-mac-prefixes> <output.h>")
        sys.exit(1)

    oui_path = sys.argv[1]
    output_path = sys.argv[2]

    # Parse OUI file
    entries = []
    with open(oui_path, 'r', errors='replace') as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith('#'):
                continue
            parts = line.split(None, 1)
            if len(parts) != 2 or len(parts[0]) != 6:
                continue
            prefix = parts[0].upper()
            vendor_full = parts[1]

            # Match against our vendor map
            for keyword, short_name in VENDOR_MAP.items():
                if keyword.lower() in vendor_full.lower():
                    entries.append((prefix, short_name))
                    break

    # Deduplicate — keep first match per prefix
    seen = set()
    unique = []
    for prefix, name in entries:
        if prefix not in seen:
            seen.add(prefix)
            unique.append((prefix, name))

    # Sort by prefix for binary search
    unique.sort(key=lambda x: x[0])

    print(f"Selected {len(unique)} OUI entries from {len(VENDOR_MAP)} vendor patterns")

    # Calculate memory
    # Each entry: 3 bytes (prefix) + 1 byte (vendor index) = 4 bytes
    # Vendor string table separate
    vendor_names = sorted(set(name for _, name in unique))
    vendor_idx = {name: i for i, name in enumerate(vendor_names)}

    prefix_bytes = len(unique) * 3
    index_bytes = len(unique)
    name_bytes = sum(len(n) + 1 for n in vendor_names)
    total = prefix_bytes + index_bytes + name_bytes

    print(f"Vendor names: {len(vendor_names)}")
    print(f"Memory: {total} bytes ({total/1024:.1f}KB)")

    # Generate C header
    with open(output_path, 'w') as f:
        f.write("// Auto-generated OUI lookup table\n")
        f.write(f"// {len(unique)} entries, {len(vendor_names)} vendors, {total/1024:.1f}KB\n\n")
        f.write("#ifndef LOKI_OUI_DB_H\n#define LOKI_OUI_DB_H\n\n")
        f.write("#include <Arduino.h>\n\n")

        # Vendor name table
        f.write(f"#define OUI_VENDOR_COUNT {len(vendor_names)}\n")
        f.write("static const char* const oui_vendors[] PROGMEM = {\n")
        for name in vendor_names:
            f.write(f'    "{name}",\n')
        f.write("};\n\n")

        # OUI prefix table (3 bytes each, stored as uint32_t with vendor index in high byte)
        f.write(f"#define OUI_ENTRY_COUNT {len(unique)}\n\n")

        # Store as packed: prefix (3 bytes) + vendor_index (1 byte) = uint32_t
        f.write("// Packed: [vendor_idx:8][prefix_hi:8][prefix_mid:8][prefix_lo:8]\n")
        f.write("static const uint32_t oui_table[] PROGMEM = {\n")
        for i, (prefix, name) in enumerate(unique):
            p = int(prefix, 16)
            vi = vendor_idx[name]
            packed = (vi << 24) | p
            if i > 0 and i % 8 == 0:
                f.write("\n")
            f.write(f"    0x{packed:08X},")
        f.write("\n};\n\n")

        # Binary search lookup function
        f.write("""
// Lookup vendor name by MAC address bytes (first 3 bytes)
static const char* oui_lookup(uint8_t mac0, uint8_t mac1, uint8_t mac2) {
    uint32_t prefix = ((uint32_t)mac0 << 16) | ((uint32_t)mac1 << 8) | mac2;

    // Binary search
    int lo = 0, hi = OUI_ENTRY_COUNT - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        uint32_t entry = pgm_read_dword(&oui_table[mid]);
        uint32_t entryPrefix = entry & 0x00FFFFFF;

        if (entryPrefix == prefix) {
            uint8_t vendorIdx = (entry >> 24) & 0xFF;
            return (const char*)pgm_read_ptr(&oui_vendors[vendorIdx]);
        } else if (entryPrefix < prefix) {
            lo = mid + 1;
        } else {
            hi = mid - 1;
        }
    }
    return nullptr;  // Not found
}
""")

        f.write("#endif // LOKI_OUI_DB_H\n")

    print(f"Generated: {output_path}")


if __name__ == '__main__':
    main()
