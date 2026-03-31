#!/usr/bin/env python3
"""
Convert Loki theme comments.json to a simple text format for CYD SD card.

Output format (comments.txt):
    [IDLE]
    Comment line 1
    Comment line 2
    [NetworkScanner]
    Comment line 1
    ...

Lines are truncated to 60 chars max for the CYD display.
"""

import json
import sys
import os

# Map original state names to our internal names
STATE_MAP = {
    'IDLE': 'idle',
    'NetworkScanner': 'scan',
    'SSHBruteforce': 'attack',
    'FTPBruteforce': 'ftp',
    'TelnetBruteforce': 'telnet',
    'StealFilesSSH': 'steal',
    'StealFilesFTP': 'steal',
    'StealFilesSMB': 'steal',
    'StealFilesTelnet': 'steal',
    'StealFilesRDP': 'steal',
    'StealDataSQL': 'steal',
    'NmapVulnScanner': 'vuln',
    'RDPBruteforce': 'attack',
    'SMBBruteforce': 'attack',
    'SQLBruteforce': 'attack',
}

MAX_LEN = 60

def convert(input_path, output_path):
    with open(input_path) as f:
        data = json.load(f)

    # Group by our internal state names
    grouped = {}
    for orig_state, comments in data.items():
        internal = STATE_MAP.get(orig_state, 'idle')
        if internal not in grouped:
            grouped[internal] = []
        for c in comments:
            # Truncate long comments
            if len(c) > MAX_LEN:
                c = c[:MAX_LEN-3] + '...'
            if c not in grouped[internal]:  # Deduplicate
                grouped[internal].append(c)

    with open(output_path, 'w') as f:
        for state in ['idle', 'scan', 'attack', 'ftp', 'telnet', 'steal', 'vuln']:
            if state in grouped:
                f.write(f'[{state}]\n')
                for comment in grouped[state]:
                    f.write(f'{comment}\n')

    total = sum(len(v) for v in grouped.values())
    print(f'  {total} comments in {len(grouped)} states')

if __name__ == '__main__':
    if len(sys.argv) < 3:
        print(f'Usage: {sys.argv[0]} <comments.json> <output.txt>')
        sys.exit(1)
    convert(sys.argv[1], sys.argv[2])
