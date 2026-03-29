#ifndef LOKI_STATUS_ICONS_H
#define LOKI_STATUS_ICONS_H

#include "asset_sicon_idle.h"
#include "asset_sicon_scan.h"
#include "asset_sicon_attack.h"
#include "asset_sicon_ftp.h"
#include "asset_sicon_telnet.h"
#include "asset_sicon_steal.h"
#include "asset_sicon_vuln.h"

#define STATUS_ICON_SIZE 28

struct StatusIconEntry {
    const char* name;
    const uint16_t* data;
};

static const StatusIconEntry statusIcons[] PROGMEM = {
    {"idle",   sicon_idle_data},
    {"scan",   sicon_scan_data},
    {"attack", sicon_attack_data},
    {"ftp",    sicon_ftp_data},
    {"telnet", sicon_telnet_data},
    {"steal",  sicon_steal_data},
    {"vuln",   sicon_vuln_data},
};

#define STATUS_ICON_COUNT 7

#endif // LOKI_STATUS_ICONS_H
