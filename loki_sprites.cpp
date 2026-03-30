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
    for (int row = 0; row < h; row++) {
        for (int col = 0; col < w; col++) {
            uint16_t px = pgm_read_word(&data[row * w + col]);
            if (px != TRANSPARENT_COLOR)
                tft.drawPixel(x + col, y + row, px);
        }
    }
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

        if (key == "name") strncpy(themeConfig.name, val.c_str(), sizeof(themeConfig.name) - 1);
        else if (key == "anim_interval_min") themeConfig.animIntervalMin = val.toInt();
        else if (key == "anim_interval_max") themeConfig.animIntervalMax = val.toInt();
        else if (key == "comment_interval_min") themeConfig.commentIntervalMin = val.toInt();
        else if (key == "comment_interval_max") themeConfig.commentIntervalMax = val.toInt();
        else if (key == "sprite_size") themeConfig.spriteSize = val.toInt();
        else if (key == "animation_mode") themeConfig.animSequential = (val == "sequential");
    }
    f.close();
}

// =============================================================================
// SETUP
// =============================================================================

void setup() {
    // Default config (matches built-in theme)
    strncpy(themeConfig.name, "LOKI", sizeof(themeConfig.name));
    themeConfig.animIntervalMin = PET_ANIM_INTERVAL_MIN;
    themeConfig.animIntervalMax = PET_ANIM_INTERVAL_MAX;
    themeConfig.commentIntervalMin = PET_COMMENT_MIN_MS;
    themeConfig.commentIntervalMax = PET_COMMENT_MAX_MS;
    themeConfig.spriteSize = 175;
    themeConfig.animSequential = true;

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
                strncpy(themeNames[themeCount], entry.name(), 23);
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

    // Count frames per state
    for (int s = 0; s < MAX_STATES && sdStates[s].name[0]; s++) {
        sdStates[s].frameCount = 0;
        for (int f = 1; f <= 20; f++) {
            char path[64];
            snprintf(path, sizeof(path), "%s%s%d.bmp", currentThemePath, sdStates[s].name, f);
            if (SD.exists(path)) {
                sdStates[s].frameCount = f;
            } else break;
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

bool drawCharacterFrame(const char* state, int frame, int x, int y) {
    // Try SD theme first
    if (usingSDTheme) {
        char path[64];
        snprintf(path, sizeof(path), "%s%s%d.bmp", currentThemePath, state, frame);
        if (drawSDBmpTransparent(path, x, y)) return true;
        // SD failed — fall through to PROGMEM
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
