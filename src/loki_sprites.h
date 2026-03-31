#ifndef LOKI_SPRITES_H
#define LOKI_SPRITES_H

#include <Arduino.h>
#include <TFT_eSPI.h>

// =============================================================================
// Loki CYD — Theme & Sprite System (SD Card)
// Themes stored on SD card at /loki/themes/<name>/
// Each theme contains: bg.bmp, theme.cfg, character sprites, icons
// =============================================================================

// Per-element style: position, font, color, alignment
// Values of -1 mean "use default"
struct ElementStyle {
    int16_t x, y;           // Position
    int8_t  font;           // TFT font (1=8px, 2=16px proportional, 4=26px)
    int8_t  fontSize;       // Text size multiplier (default 1)
    uint16_t color;         // RGB565 color
    int8_t  datum;          // Text alignment (TL_DATUM=0, TC=1, TR=2, ML=3, MC=4, MR=5, BL=6, BC=7, BR=8)
};

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

    // Colors (RGB565) — base palette
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

    // =========================================================================
    // Per-element customization
    // Each dynamic element can override position, font, color, alignment
    // =========================================================================

    // Per-stat styles (9 stats: target, port, vuln, cred, zombie, data, networkkb, level, attacks)
    ElementStyle stat[9];

    // XP value display
    ElementStyle xpVal;

    // WiFi status text
    ElementStyle wifiText;
    uint16_t wifiColorOn;       // Color when connected
    uint16_t wifiColorOff;      // Color when offline

    // Status bar line 1 (action name e.g. "NetworkScanner")
    ElementStyle statusLine1;

    // Status bar line 2 (detail text e.g. "192.168.1.1")
    ElementStyle statusLine2;

    // Status icon
    int statusIconY;            // Y position (computed from statusY if -1)
    int statusIconSize;         // Icon size (default 42)

    // Comment/dialogue text
    ElementStyle commentText;

    // Kill feed colors (per message type)
    uint16_t kfColorInfo;       // Info messages (cyan)
    uint16_t kfColorFound;      // Host/port found (bright green)
    uint16_t kfColorSuccess;    // Success messages (green)
    uint16_t kfColorCracked;    // Cracked! (hot pink)
    uint16_t kfColorDim;        // Blocked/locked (dim)
    uint16_t kfColorAttack;     // Attack start (magenta)
    uint16_t kfColorError;      // Errors (red)
    uint16_t kfColorXp;         // XP gains (gold)
    int8_t   kfFont;            // Kill feed font (default 1)
    uint16_t kfBgColor;         // Kill feed background (default black)
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
const char* getThemeName(int index);          // Folder name (for loading)
const char* getThemeDisplayName(int index);   // Display name (from cfg)

// Draw the full-screen background from SD card
bool drawBackground();

// Draw a character sprite for the given state and frame (with transparency)
// state: "idle", "scan", "attack", "steal", "vuln", "ftp", "telnet"
// frame: 1-based frame number
bool drawCharacterFrame(const char* state, int frame, int x, int y);

// Get the number of available frames for a state
int getFrameCount(const char* state);

// Draw status icon from SD theme (the numberless file in the state subfolder)
bool drawStatusIcon(const char* state, int x, int y);

// Get a random comment for the given state from theme's comments.txt
bool getRandomComment(const char* state, char* buf, int bufLen);

// Is SD card available?
bool sdAvailable();

// Is a theme loaded?
bool themeLoaded();

// Current theme path prefix (e.g. "/loki/themes/loki/")
const char* themePath();

}  // namespace LokiSprites

#endif // LOKI_SPRITES_H
