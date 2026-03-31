// Auto-generated — all Loki PROGMEM assets
#ifndef LOKI_ASSETS_H
#define LOKI_ASSETS_H

#include "asset_bg.h"

#include "asset_sprite_idle1.h"
#include "asset_sprite_scan1.h"
#include "asset_sprite_attack1.h"
#include "asset_sprite_ftp1.h"
#include "asset_sprite_telnet1.h"
#include "asset_sprite_steal1.h"
#include "asset_sprite_vuln1.h"

// Status icons are in loki_status_icons.h (asset_sicon_*.h)

// Sprite frame lookup table
struct SpriteFrameSet {
    const char* name;
    const uint16_t* frames[8];
    int count;
    int w, h;
};

#define SPRITE_STATE_COUNT 7
static const SpriteFrameSet spriteFrames[SPRITE_STATE_COUNT] PROGMEM = {
    {"idle", {sprite_idle1_data, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr}, 1, SPRITE_IDLE1_W, SPRITE_IDLE1_H},
    {"scan", {sprite_scan1_data, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr}, 1, SPRITE_SCAN1_W, SPRITE_SCAN1_H},
    {"attack", {sprite_attack1_data, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr}, 1, SPRITE_ATTACK1_W, SPRITE_ATTACK1_H},
    {"ftp", {sprite_ftp1_data, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr}, 1, SPRITE_FTP1_W, SPRITE_FTP1_H},
    {"telnet", {sprite_telnet1_data, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr}, 1, SPRITE_TELNET1_W, SPRITE_TELNET1_H},
    {"steal", {sprite_steal1_data, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr}, 1, SPRITE_STEAL1_W, SPRITE_STEAL1_H},
    {"vuln", {sprite_vuln1_data, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr}, 1, SPRITE_VULN1_W, SPRITE_VULN1_H},
};

#endif // LOKI_ASSETS_H
