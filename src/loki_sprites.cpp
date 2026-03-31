// =============================================================================
// Loki CYD — Theme & Sprite System
//
// Two-tier theme system:
//   1. PROGMEM fallback: bg.bmp + 1 still sprite per state (always available)
//   2. SD card themes: full animated themes in /loki/themes/<name>/
//
// If SD card has a "loki" theme, it automatically replaces the built-in one.
// If no SD card, the built-in still images are used.
// =============================================================================

#include "loki_sprites.h"
#include "loki_config.h"
#include "loki_assets.h"  // PROGMEM fallback assets
#include "asset_bg.h"     // Background data for composite transparency
#include <SD.h>
#include <SPI.h>

extern TFT_eSPI tft;

namespace LokiSprites {

// =============================================================================
// STATE
// =============================================================================

static bool sdReady = false;
static bool hasSDTheme = false;
static bool usingSDTheme = false;
static char currentThemePath[48] = {0};
static LokiThemeConfig themeConfig;

#define MAX_THEMES 10
static char themeNames[MAX_THEMES][24];
static int themeCount = 0;

#define MAX_STATES 8
struct StateInfo {
    char name[12];
    int frameCount;
};
static StateInfo sdStates[MAX_STATES] = {
    {"idle", 0}, {"scan", 0}, {"attack", 0}, {"ftp", 0},
    {"telnet", 0}, {"steal", 0}, {"vuln", 0}, {"", 0}
};

// Transparency key: magenta (255,0,255) = 0xF81F
#define TRANSPARENT_COLOR 0xF81F

// =============================================================================
// SD CARD
// =============================================================================

static bool mountSD() {
    SPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
    pinMode(SD_CS, OUTPUT);
    digitalWrite(SD_CS, HIGH);
    return SD.begin(SD_CS, SPI, 4000000);
}

static void unmountSD() {
    SD.end();
}

// =============================================================================
// PROGMEM DRAWING (built-in fallback)
// =============================================================================

static void drawProgmemRGB565(const uint16_t* data, int x, int y, int w, int h) {
    tft.startWrite();
    tft.setAddrWindow(x, y, w, h);
    #define CHUNK_PX 240
    uint16_t buf[CHUNK_PX];
    int total = w * h;
    int offset = 0;
    while (offset < total) {
        int count = min(CHUNK_PX, total - offset);
        for (int i = 0; i < count; i++)
            buf[i] = pgm_read_word(&data[offset + i]);
        tft.pushColors(buf, count);
        offset += count;
    }
    tft.endWrite();
}

static void drawProgmemTransparent(const uint16_t* data, int x, int y, int w, int h) {
    // Composite: replace transparent with PROGMEM background, push full rows
    static uint16_t rowBuf[320];
    tft.startWrite();
    for (int row = 0; row < h; row++) {
        int len = min(w, 320);
        for (int col = 0; col < len; col++) {
            uint16_t px = pgm_read_word(&data[row * w + col]);
            if (px == TRANSPARENT_COLOR) {
                int bgX = x + col, bgY = y + row;
                if (bgX < BG_W && bgY < BG_H)
                    rowBuf[col] = pgm_read_word(&bg_data[bgY * BG_W + bgX]);
                else
                    rowBuf[col] = 0x0861;
            } else {
                rowBuf[col] = px;
            }
        }
        tft.setAddrWindow(x, y + row, len, 1);
        tft.pushColors(rowBuf, len);
    }
    tft.endWrite();
}

// Find PROGMEM sprite set by state name
static const SpriteFrameSet* findProgmemState(const char* state) {
    for (int s = 0; s < SPRITE_STATE_COUNT; s++) {
        const char* name = (const char*)pgm_read_ptr(&spriteFrames[s].name);
        if (strcmp(name, state) == 0) return &spriteFrames[s];
    }
    return nullptr;
}

// =============================================================================
// SD CARD BMP READING
// =============================================================================

static bool readBmpHeader(File& file, int32_t& width, int32_t& height, uint32_t& dataOffset) {
    uint8_t header[66];
    if (file.read(header, 66) != 66) return false;
    if (header[0] != 'B' || header[1] != 'M') return false;
    dataOffset = *(uint32_t*)&header[10];
    width = *(int32_t*)&header[18];
    height = *(int32_t*)&header[22];
    uint16_t bpp = *(uint16_t*)&header[28];
    if (height < 0) height = -height;
    return (bpp == 16);
}

static bool drawSDBmpOpaque(const char* path, int x, int y) {
    if (!mountSD()) return false;
    File file = SD.open(path);
    if (!file) { unmountSD(); return false; }

    int32_t width, height; uint32_t dataOffset;
    if (!readBmpHeader(file, width, height, dataOffset)) {
        file.close(); unmountSD(); return false;
    }

    file.seek(dataOffset);
    int rowSize = ((width * 2 + 3) / 4) * 4;
    tft.startWrite();
    tft.setAddrWindow(x, y, width, height);
    uint8_t rowBuf[rowSize];
    for (int row = 0; row < height; row++) {
        file.read(rowBuf, rowSize);
        tft.pushColors((uint16_t*)rowBuf, width);
    }
    tft.endWrite();
    file.close(); unmountSD();
    return true;
}

static bool drawSDBmpTransparent(const char* path, int x, int y) {
    if (!mountSD()) return false;
    File file = SD.open(path);
    if (!file) { unmountSD(); return false; }

    int32_t width, height; uint32_t dataOffset;
    if (!readBmpHeader(file, width, height, dataOffset)) {
        file.close(); unmountSD(); return false;
    }

    file.seek(dataOffset);
    int rowSize = ((width * 2 + 3) / 4) * 4;
    uint8_t rowBuf[rowSize];

    // Fast composite: replace transparent pixels with PROGMEM background,
    // then push entire row at once. No flash, no pixel-by-pixel drawing.
    // bg_data from asset_bg.h (PROGMEM background for composite)

    tft.startWrite();
    for (int row = 0; row < height; row++) {
        file.read(rowBuf, rowSize);
        uint16_t* pixels = (uint16_t*)rowBuf;

        // Replace magenta pixels with background
        int bgY = y + row;
        for (int col = 0; col < width; col++) {
            if (pixels[col] == TRANSPARENT_COLOR) {
                int bgX = x + col;
                if (bgX < BG_W && bgY < BG_H) {
                    pixels[col] = pgm_read_word(&bg_data[bgY * BG_W + bgX]);
                } else {
                    pixels[col] = 0x0861;  // LOKI_BG_DARK fallback
                }
            }
        }

        // Push entire composited row at once (fast!)
        tft.setAddrWindow(x, y + row, width, 1);
        tft.pushColors(pixels, width);
    }
    tft.endWrite();

    file.close(); unmountSD();
    return true;
}

// =============================================================================
// THEME CONFIG PARSER
// =============================================================================

static void parseThemeConfig(const char* path) {
    if (!SD.exists(path)) return;
    File f = SD.open(path, FILE_READ);
    if (!f) return;

    while (f.available()) {
        String line = f.readStringUntil('\n');
        line.trim();
        if (line.length() == 0 || line.startsWith("#")) continue;
        int eq = line.indexOf('=');
        if (eq < 0) continue;
        String key = line.substring(0, eq); key.trim();
        String val = line.substring(eq + 1); val.trim();

        // Helper: parse hex color (e.g. "3DE9" → 0x3DE9)
        #define PARSE_COLOR(k, field) if (key == k) { themeConfig.field = (uint16_t)strtol(val.c_str(), NULL, 16); }
        #define PARSE_INT(k, field) if (key == k) { themeConfig.field = val.toInt(); }

        if (key == "name") strncpy(themeConfig.name, val.c_str(), sizeof(themeConfig.name) - 1);

        // Animation
        else PARSE_INT("sprite_size", spriteSize)
        else if (key == "animation_mode") themeConfig.animSequential = (val == "sequential");
        else PARSE_INT("anim_interval_min", animIntervalMin)
        else PARSE_INT("anim_interval_max", animIntervalMax)
        else PARSE_INT("comment_interval_min", commentIntervalMin)
        else PARSE_INT("comment_interval_max", commentIntervalMax)

        // Colors
        else PARSE_COLOR("color_bg", colorBg)
        else PARSE_COLOR("color_surface", colorSurface)
        else PARSE_COLOR("color_elevated", colorElevated)
        else PARSE_COLOR("color_text", colorText)
        else PARSE_COLOR("color_text_dim", colorTextDim)
        else PARSE_COLOR("color_accent", colorAccent)
        else PARSE_COLOR("color_accent_bright", colorAccentBright)
        else PARSE_COLOR("color_accent_dim", colorAccentDim)
        else PARSE_COLOR("color_highlight", colorHighlight)
        else PARSE_COLOR("color_alert", colorAlert)
        else PARSE_COLOR("color_error", colorError)
        else PARSE_COLOR("color_success", colorSuccess)
        else PARSE_COLOR("color_cracked", colorCracked)

        // Layout: Header
        else PARSE_INT("header_y", headerY)
        else PARSE_INT("header_h", headerH)
        else PARSE_INT("xp_x", xpX)
        else PARSE_INT("xp_y", xpY)
        else PARSE_INT("wifi_x", wifiX)
        else PARSE_INT("wifi_y", wifiY)

        // Layout: Stats
        else PARSE_INT("stats_y", statsY)
        else PARSE_INT("stats_rows", statsRows)
        else PARSE_INT("stats_cols", statsCols)
        else PARSE_INT("stats_row_h", statsRowH)
        else PARSE_INT("stats_icon_size", statsIconSize)

        // Layout: Status
        else PARSE_INT("status_y", statusY)
        else PARSE_INT("status_h", statusH)
        else PARSE_INT("status_icon_x", statusIconX)
        else PARSE_INT("status_text_x", statusTextX)

        // Layout: Dialogue
        else PARSE_INT("dialogue_x", dlgX)
        else PARSE_INT("dialogue_y", dlgY)
        else PARSE_INT("dialogue_w", dlgW)
        else PARSE_INT("dialogue_h", dlgH)

        // Layout: Character
        else PARSE_INT("char_x", charX)
        else PARSE_INT("char_y", charY)
        else PARSE_INT("char_w", charW)
        else PARSE_INT("char_h", charH)

        // Layout: Kill Feed
        else PARSE_INT("killfeed_y", kfY)
        else PARSE_INT("killfeed_lines", kfLines)
        else PARSE_INT("killfeed_line_h", kfLineH)
        ;

        #undef PARSE_COLOR
        #undef PARSE_INT
    }
    f.close();
}

// =============================================================================
// SETUP
// =============================================================================

void setup() {
    // Default config (matches built-in Loki theme)
    strncpy(themeConfig.name, "LOKI", sizeof(themeConfig.name));

    // Animation defaults
    themeConfig.spriteSize = 175;
    themeConfig.animSequential = true;
    themeConfig.animIntervalMin = PET_ANIM_INTERVAL_MIN;
    themeConfig.animIntervalMax = PET_ANIM_INTERVAL_MAX;
    themeConfig.commentIntervalMin = PET_COMMENT_MIN_MS;
    themeConfig.commentIntervalMax = PET_COMMENT_MAX_MS;

    // Color defaults (Loki green theme)
    themeConfig.colorBg          = 0x0861;  // Dark green-black
    themeConfig.colorSurface     = 0x0A43;
    themeConfig.colorElevated    = 0x1264;
    themeConfig.colorText        = 0xD6B4;  // Light green-white
    themeConfig.colorTextDim     = 0x4CC9;
    themeConfig.colorAccent      = 0x3DE9;  // Green
    themeConfig.colorAccentBright= 0x7EF6;
    themeConfig.colorAccentDim   = 0x3464;
    themeConfig.colorHighlight   = 0xFE60;  // Gold
    themeConfig.colorAlert       = 0xF81F;  // Magenta
    themeConfig.colorError       = 0xF800;  // Red
    themeConfig.colorSuccess     = 0x07E0;  // Green
    themeConfig.colorCracked     = 0xFB56;  // Hot pink

    // Layout defaults (320x480)
    themeConfig.headerY = 0;
    themeConfig.headerH = 32;
    themeConfig.xpX = 162;
    themeConfig.xpY = 16;
    themeConfig.wifiX = 235;
    themeConfig.wifiY = 16;

    themeConfig.statsY = 34;
    themeConfig.statsRows = 3;
    themeConfig.statsCols = 3;
    themeConfig.statsIconSize = min((int)(18 * SCREEN_WIDTH / 222.0f), 22);
    themeConfig.statsRowH = themeConfig.statsIconSize + 8;

    int statsBottom = themeConfig.statsY + themeConfig.statsRowH * themeConfig.statsRows;
    // These values must match what make_background.py produces
    int friseY = statsBottom + 2;
    int friseH = 10;

    themeConfig.statusY = 125;
    themeConfig.statusH = 45;
    themeConfig.statusIconX = 4;
    themeConfig.statusTextX = 41;

    themeConfig.dlgX = 5;
    themeConfig.dlgY = 174;
    themeConfig.dlgW = 309;
    themeConfig.dlgH = 54;

    themeConfig.charX = 72;
    themeConfig.charY = 236;
    themeConfig.charW = 175;
    themeConfig.charH = 175;

    themeConfig.kfY = 416;
    themeConfig.kfLines = 6;
    themeConfig.kfLineH = 10;

    Serial.printf("[THEME] Built-in fallback: bg + %d states\n", SPRITE_STATE_COUNT);

    // Try SD card
    if (!mountSD()) {
        sdReady = false;
        usingSDTheme = false;
        Serial.println("[THEME] No SD card — using built-in theme");
        return;
    }
    sdReady = true;

    // Create directories
    if (!SD.exists("/loki")) SD.mkdir("/loki");
    if (!SD.exists("/loki/themes")) SD.mkdir("/loki/themes");
    if (!SD.exists("/loki/loot")) SD.mkdir("/loki/loot");
    if (!SD.exists("/loki/reports")) SD.mkdir("/loki/reports");

    // Scan for themes
    themeCount = 0;
    File themesDir = SD.open("/loki/themes");
    if (themesDir && themesDir.isDirectory()) {
        File entry = themesDir.openNextFile();
        while (entry && themeCount < MAX_THEMES) {
            if (entry.isDirectory()) {
                // entry.name() may return full path — extract just the folder name
                String fullName = String(entry.name());
                int lastSlash = fullName.lastIndexOf('/');
                String shortName = (lastSlash >= 0) ? fullName.substring(lastSlash + 1) : fullName;
                strncpy(themeNames[themeCount], shortName.c_str(), 23);
                themeNames[themeCount][23] = '\0';
                Serial.printf("[THEME] Found: %s\n", themeNames[themeCount]);
                themeCount++;
            }
            entry = themesDir.openNextFile();
        }
        themesDir.close();
    }

    Serial.printf("[THEME] %d SD themes available\n", themeCount);

    // Auto-load "loki" theme from SD if it exists (upgrades built-in)
    bool loaded = false;
    for (int i = 0; i < themeCount; i++) {
        if (strcmp(themeNames[i], "loki") == 0) {
            unmountSD();
            loaded = loadTheme("loki");
            break;
        }
    }

    if (!loaded) {
        // No loki theme on SD — try first available
        if (themeCount > 0) {
            unmountSD();
            loadTheme(themeNames[0]);
        } else {
            usingSDTheme = false;
            Serial.println("[THEME] No SD themes — using built-in");
            unmountSD();
        }
    }
}

// =============================================================================
// LOAD SD THEME
// =============================================================================

bool loadTheme(const char* themeName) {
    if (!sdReady) return false;
    if (!mountSD()) return false;

    snprintf(currentThemePath, sizeof(currentThemePath), "/loki/themes/%s/", themeName);

    char checkPath[60];
    snprintf(checkPath, sizeof(checkPath), "/loki/themes/%s", themeName);
    if (!SD.exists(checkPath)) {
        Serial.printf("[THEME] Not found: %s\n", checkPath);
        hasSDTheme = false;
        usingSDTheme = false;
        unmountSD();
        return false;
    }

    // Load theme.cfg
    char cfgPath[60];
    snprintf(cfgPath, sizeof(cfgPath), "%stheme.cfg", currentThemePath);
    parseThemeConfig(cfgPath);

    // Count frames per state — look in subfolders
    // Files WITH a number = animation frames, files WITHOUT = status icon
    for (int s = 0; s < MAX_STATES && sdStates[s].name[0]; s++) {
        sdStates[s].frameCount = 0;
        char stateDir[80];
        snprintf(stateDir, sizeof(stateDir), "%s%s", currentThemePath, sdStates[s].name);

        if (SD.exists(stateDir)) {
            File dir = SD.open(stateDir);
            if (dir && dir.isDirectory()) {
                File entry = dir.openNextFile();
                while (entry) {
                    String fname = String(entry.name());
                    int lastSlash = fname.lastIndexOf('/');
                    if (lastSlash >= 0) fname = fname.substring(lastSlash + 1);

                    // Only count .bmp files that have a digit in the name
                    if (fname.endsWith(".bmp")) {
                        bool hasDigit = false;
                        for (int c = 0; c < (int)fname.length(); c++) {
                            if (isdigit(fname.charAt(c))) { hasDigit = true; break; }
                        }
                        if (hasDigit) sdStates[s].frameCount++;
                    }
                    entry = dir.openNextFile();
                }
                dir.close();
            }
        } else {
            // Fallback: try flat files (old format: idle1.bmp, idle2.bmp...)
            for (int f = 1; f <= 999; f++) {
                char path[80];
                snprintf(path, sizeof(path), "%s%s%d.bmp", currentThemePath, sdStates[s].name, f);
                if (SD.exists(path)) {
                    sdStates[s].frameCount = f;
                } else break;
            }
        }

        if (sdStates[s].frameCount > 0)
            Serial.printf("[THEME] %s: %d frames\n", sdStates[s].name, sdStates[s].frameCount);
    }

    hasSDTheme = true;
    usingSDTheme = true;
    Serial.printf("[THEME] Loaded SD: %s (%s)\n", themeConfig.name, themeName);
    unmountSD();
    return true;
}

// =============================================================================
// PUBLIC API
// =============================================================================

LokiThemeConfig& getThemeConfig() { return themeConfig; }
int getThemeCount() { return themeCount; }
const char* getThemeName(int index) {
    if (index < 0 || index >= themeCount) return "";
    return themeNames[index];
}
bool sdAvailable() { return sdReady; }
bool themeLoaded() { return true; }  // Always true — we have built-in fallback
const char* themePath() { return currentThemePath; }

// =============================================================================
// DRAW BACKGROUND
// =============================================================================

bool drawBackground() {
    if (usingSDTheme) {
        char path[64];
        snprintf(path, sizeof(path), "%sbg.bmp", currentThemePath);
        if (drawSDBmpOpaque(path, 0, 0)) return true;
        // SD failed — fall through to PROGMEM
    }
    // Built-in PROGMEM background
    drawProgmemRGB565(bg_data, 0, 0, BG_W, BG_H);
    return true;
}

// =============================================================================
// DRAW CHARACTER FRAME
// =============================================================================

// Find the Nth numbered BMP file in a state subfolder
static bool findNthFrame(const char* stateDir, int n, char* outPath, int outLen) {
    if (!mountSD()) return false;

    File dir = SD.open(stateDir);
    if (!dir || !dir.isDirectory()) { unmountSD(); return false; }

    // Collect numbered BMP filenames, find the nth one (1-based)
    int count = 0;
    File entry = dir.openNextFile();
    while (entry) {
        String fname = String(entry.name());
        int lastSlash = fname.lastIndexOf('/');
        String shortName = (lastSlash >= 0) ? fname.substring(lastSlash + 1) : fname;

        if (shortName.endsWith(".bmp")) {
            bool hasDigit = false;
            for (int c = 0; c < (int)shortName.length(); c++) {
                if (isdigit(shortName.charAt(c))) { hasDigit = true; break; }
            }
            if (hasDigit) {
                count++;
                if (count == n) {
                    snprintf(outPath, outLen, "%s/%s", stateDir, shortName.c_str());
                    dir.close();
                    unmountSD();
                    return true;
                }
            }
        }
        entry = dir.openNextFile();
    }
    dir.close();
    unmountSD();
    return false;
}

bool drawCharacterFrame(const char* state, int frame, int x, int y) {
    // Try SD theme — subfolder format first
    if (usingSDTheme) {
        char stateDir[80];
        snprintf(stateDir, sizeof(stateDir), "%s%s", currentThemePath, state);

        char framePath[120];
        if (findNthFrame(stateDir, frame, framePath, sizeof(framePath))) {
            if (drawSDBmpTransparent(framePath, x, y)) return true;
        }

        // Fallback: try old flat format (state1.bmp, state2.bmp)
        char flatPath[80];
        snprintf(flatPath, sizeof(flatPath), "%s%s%d.bmp", currentThemePath, state, frame);
        if (drawSDBmpTransparent(flatPath, x, y)) return true;
    }

    // Built-in PROGMEM fallback (1 still frame per state)
    const SpriteFrameSet* fs = findProgmemState(state);
    if (!fs) fs = findProgmemState("idle");
    if (!fs) return false;

    int count = pgm_read_word(&fs->count);
    if (count <= 0) return false;

    // Built-in only has frame 1 per state
    const uint16_t* data = (const uint16_t*)pgm_read_ptr(&fs->frames[0]);
    if (!data) return false;

    int w = pgm_read_word(&fs->w);
    int h = pgm_read_word(&fs->h);
    drawProgmemTransparent(data, x, y, w, h);
    return true;
}

// =============================================================================
// FRAME COUNT
// =============================================================================

int getFrameCount(const char* state) {
    // If using SD theme, return SD frame count
    if (usingSDTheme) {
        for (int s = 0; s < MAX_STATES && sdStates[s].name[0]; s++) {
            if (strcmp(sdStates[s].name, state) == 0)
                return sdStates[s].frameCount;
        }
    }
    // Built-in: always 1 frame per state
    const SpriteFrameSet* fs = findProgmemState(state);
    if (fs) return 1;
    return 0;
}

}  // namespace LokiSprites
