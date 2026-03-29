#ifndef LOKI_SPRITES_H
#define LOKI_SPRITES_H

#include <Arduino.h>
#include <TFT_eSPI.h>

// =============================================================================
// Loki CYD — Theme & Sprite System (SD Card)
// Themes stored on SD card at /loki/themes/<name>/
// Each theme contains: bg.bmp, theme.cfg, character sprites, icons
// =============================================================================

// Theme config loaded from theme.cfg
struct LokiThemeConfig {
    char name[32];           // Display name
    int animIntervalMin;     // Min ms between animation frames
    int animIntervalMax;     // Max ms between animation frames
    int commentIntervalMin;  // Min ms between comments
    int commentIntervalMax;  // Max ms between comments
    int spriteSize;          // Character sprite dimension (e.g. 175)
};

namespace LokiSprites {

// Initialize — mount SD card, scan for themes
void setup();

// Load a theme by folder name (e.g. "loki")
bool loadTheme(const char* themeName);

// Get current theme config
LokiThemeConfig& getThemeConfig();

// Get list of available theme names
int getThemeCount();
const char* getThemeName(int index);

// Draw the full-screen background from SD card
bool drawBackground();

// Draw a character sprite for the given state and frame (with transparency)
// state: "idle", "scan", "attack", "steal", "vuln", "ftp", "telnet"
// frame: 1-based frame number
bool drawCharacterFrame(const char* state, int frame, int x, int y);

// Get the number of available frames for a state
int getFrameCount(const char* state);

// Is SD card available?
bool sdAvailable();

// Is a theme loaded?
bool themeLoaded();

// Current theme path prefix (e.g. "/loki/themes/loki/")
const char* themePath();

}  // namespace LokiSprites

#endif // LOKI_SPRITES_H
