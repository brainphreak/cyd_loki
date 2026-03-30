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

    // Animation
    int spriteSize;
    bool animSequential;
    int animIntervalMin;
    int animIntervalMax;
    int commentIntervalMin;
    int commentIntervalMax;

    // Colors (RGB565)
    uint16_t colorBg;
    uint16_t colorSurface;
    uint16_t colorElevated;
    uint16_t colorText;
    uint16_t colorTextDim;
    uint16_t colorAccent;       // Primary accent (green in loki)
    uint16_t colorAccentBright;
    uint16_t colorAccentDim;
    uint16_t colorHighlight;    // XP/gold
    uint16_t colorAlert;        // Magenta
    uint16_t colorError;        // Red
    uint16_t colorSuccess;      // Green
    uint16_t colorCracked;      // Hot pink

    // Layout: Header
    int headerY;
    int headerH;
    int xpX, xpY;
    int wifiX, wifiY;

    // Layout: Stats Grid
    int statsY;
    int statsRows, statsCols;
    int statsRowH;
    int statsIconSize;

    // Layout: Status Bar
    int statusY, statusH;
    int statusIconX, statusTextX;

    // Layout: Dialogue
    int dlgX, dlgY, dlgW, dlgH;

    // Layout: Character
    int charX, charY, charW, charH;

    // Layout: Kill Feed
    int kfY, kfLines, kfLineH;
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
