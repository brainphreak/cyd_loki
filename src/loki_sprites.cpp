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
#include "asset_splash.h" // PROGMEM splash screen
#include <SD.h>
#include <SPI.h>
#include <Preferences.h>

extern TFT_eSPI tft;

namespace LokiSprites {

// =============================================================================
// STATE
// =============================================================================

static SemaphoreHandle_t sdMutex = NULL;
static bool sdReady = false;
static bool hasSDTheme = false;
static bool usingSDTheme = false;
static char currentThemePath[48] = {0};
static LokiThemeConfig themeConfig;

#define MAX_THEMES 10
static char themeNames[MAX_THEMES][24];       // Folder names (for path building)
static char themeDisplayNames[MAX_THEMES][24]; // Display names (from theme.cfg)
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

void sdLock() {
    if (!sdMutex) sdMutex = xSemaphoreCreateMutex();
    xSemaphoreTake(sdMutex, portMAX_DELAY);
}

void sdUnlock() {
    if (sdMutex) xSemaphoreGive(sdMutex);
}

static bool sdMounted = false;

static bool ensureSD() {
    if (sdMounted) return true;
    SPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
    pinMode(SD_CS, OUTPUT);
    digitalWrite(SD_CS, HIGH);
    sdMounted = SD.begin(SD_CS, SPI, 4000000);
    return sdMounted;
}

static bool mountSD() {
    sdLock();
    if (ensureSD()) return true;
    sdUnlock();
    return false;
}

static void unmountSD() {
    sdUnlock();
}

bool sdMount() { return mountSD(); }
void sdUnmount() { unmountSD(); }

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

static bool drawSDBmpTransparent(const char* path, int x, int y, uint16_t compositeColor) {
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

    tft.startWrite();
    for (int row = 0; row < height; row++) {
        file.read(rowBuf, rowSize);
        uint16_t* pixels = (uint16_t*)rowBuf;

        // Replace magenta transparent pixels with the specified background color
        // For SD themes: compositeColor is colorBg (character) or colorSurface (status icon)
        // For PROGMEM fallback: pixel-perfect from bg_data
        int bgY = y + row;
        for (int col = 0; col < width; col++) {
            if (pixels[col] == TRANSPARENT_COLOR) {
                if (usingSDTheme) {
                    pixels[col] = compositeColor;
                } else {
                    int bgX = x + col;
                    if (bgX < BG_W && bgY < BG_H) {
                        pixels[col] = pgm_read_word(&bg_data[bgY * BG_W + bgX]);
                    } else {
                        pixels[col] = 0x0861;
                    }
                }
            }
        }

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

        // Per-element: XP value
        else if (key == "xp_val_x")     themeConfig.xpVal.x = val.toInt();
        else if (key == "xp_val_y")     themeConfig.xpVal.y = val.toInt();
        else if (key == "xp_val_font")  themeConfig.xpVal.font = val.toInt();
        else if (key == "xp_val_color") themeConfig.xpVal.color = (uint16_t)strtol(val.c_str(), NULL, 16);
        else if (key == "xp_val_datum") themeConfig.xpVal.datum = val.toInt();

        // Per-element: WiFi text
        else if (key == "wifi_text_x")     themeConfig.wifiText.x = val.toInt();
        else if (key == "wifi_text_y")     themeConfig.wifiText.y = val.toInt();
        else if (key == "wifi_text_font")  themeConfig.wifiText.font = val.toInt();
        else if (key == "wifi_text_datum") themeConfig.wifiText.datum = val.toInt();
        else PARSE_COLOR("wifi_color_on", wifiColorOn)
        else PARSE_COLOR("wifi_color_off", wifiColorOff)

        // Per-element: Status line 1 (action name)
        else if (key == "status_line1_x")     themeConfig.statusLine1.x = val.toInt();
        else if (key == "status_line1_y")     themeConfig.statusLine1.y = val.toInt();
        else if (key == "status_line1_font")  themeConfig.statusLine1.font = val.toInt();
        else if (key == "status_line1_color") themeConfig.statusLine1.color = (uint16_t)strtol(val.c_str(), NULL, 16);
        else if (key == "status_line1_datum") themeConfig.statusLine1.datum = val.toInt();

        // Per-element: Status line 2 (detail text)
        else if (key == "status_line2_x")     themeConfig.statusLine2.x = val.toInt();
        else if (key == "status_line2_y")     themeConfig.statusLine2.y = val.toInt();
        else if (key == "status_line2_font")  themeConfig.statusLine2.font = val.toInt();
        else if (key == "status_line2_color") themeConfig.statusLine2.color = (uint16_t)strtol(val.c_str(), NULL, 16);
        else if (key == "status_line2_datum") themeConfig.statusLine2.datum = val.toInt();

        // Per-element: Status icon
        else if (key == "status_icon_y")    themeConfig.statusIconY = val.toInt();
        else if (key == "status_icon_size") themeConfig.statusIconSize = val.toInt();

        // Per-element: Comment/dialogue text
        else if (key == "comment_text_x")     themeConfig.commentText.x = val.toInt();
        else if (key == "comment_text_y")     themeConfig.commentText.y = val.toInt();
        else if (key == "comment_text_font")  themeConfig.commentText.font = val.toInt();
        else if (key == "comment_text_color") themeConfig.commentText.color = (uint16_t)strtol(val.c_str(), NULL, 16);

        // Per-element: Kill feed colors
        else PARSE_COLOR("kf_color_info", kfColorInfo)
        else PARSE_COLOR("kf_color_found", kfColorFound)
        else PARSE_COLOR("kf_color_success", kfColorSuccess)
        else PARSE_COLOR("kf_color_cracked", kfColorCracked)
        else PARSE_COLOR("kf_color_dim", kfColorDim)
        else PARSE_COLOR("kf_color_attack", kfColorAttack)
        else PARSE_COLOR("kf_color_error", kfColorError)
        else PARSE_COLOR("kf_color_xp", kfColorXp)
        else if (key == "kf_font") themeConfig.kfFont = val.toInt();
        else PARSE_COLOR("kf_bg_color", kfBgColor)

        // Per-stat styles (stat0..stat8)
        else if (key.startsWith("stat") && key.length() > 4 && isdigit(key.charAt(4))) {
            int idx = key.charAt(4) - '0';
            if (idx >= 0 && idx < 9) {
                String suffix = key.substring(5);  // e.g. "_x", "_y", "_font", "_color", "_datum"
                if (suffix == "_x")     themeConfig.stat[idx].x = val.toInt();
                else if (suffix == "_y")     themeConfig.stat[idx].y = val.toInt();
                else if (suffix == "_font")  themeConfig.stat[idx].font = val.toInt();
                else if (suffix == "_color") themeConfig.stat[idx].color = (uint16_t)strtol(val.c_str(), NULL, 16);
                else if (suffix == "_datum") themeConfig.stat[idx].datum = val.toInt();
            }
        }
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

    // Color defaults (matches PROGMEM loki_dark bg.bmp)
    themeConfig.colorBg          = 0x0000;  // Pure black
    themeConfig.colorSurface     = 0x0841;
    themeConfig.colorElevated    = 0x10A2;
    themeConfig.colorText        = 0x0647;  // Green text
    themeConfig.colorTextDim     = 0x0283;
    themeConfig.colorAccent      = 0x0465;  // Dark green
    themeConfig.colorAccentBright= 0x2DAA;
    themeConfig.colorAccentDim   = 0x0320;
    themeConfig.colorHighlight   = 0xCDA6;  // Gold
    themeConfig.colorAlert       = 0xCA99;
    themeConfig.colorError       = 0xC986;  // Red
    themeConfig.colorSuccess     = 0x0465;  // Green
    themeConfig.colorCracked     = 0xFB36;  // Hot pink

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

    // Per-element defaults: -1 means "use computed default"
    for (int i = 0; i < 9; i++) {
        themeConfig.stat[i] = {-1, -1, 2, 1, themeConfig.colorText, 3}; // ML_DATUM
    }
    themeConfig.xpVal = {-1, -1, 2, 1, themeConfig.colorHighlight, 3};  // ML_DATUM
    themeConfig.wifiText = {-1, -1, 2, 1, 0, 5};                       // MR_DATUM
    themeConfig.wifiColorOn = themeConfig.colorSuccess;
    themeConfig.wifiColorOff = themeConfig.colorTextDim;

    themeConfig.statusLine1 = {-1, -1, 2, 1, themeConfig.colorAccent, 0};  // TL_DATUM
    themeConfig.statusLine2 = {-1, -1, 2, 1, themeConfig.colorText, 0};    // TL_DATUM
    themeConfig.statusIconY = -1;
    themeConfig.statusIconSize = 42;

    themeConfig.commentText = {-1, -1, 2, 1, themeConfig.colorText, 0};

    // Kill feed colors — default to loki palette
    themeConfig.kfColorInfo    = 0x07FF;  // LOKI_CYAN
    themeConfig.kfColorFound   = 0x7EF6;  // LOKI_BRIGHT
    themeConfig.kfColorSuccess = 0x3DE9;  // LOKI_GREEN
    themeConfig.kfColorCracked = 0xFB56;  // LOKI_HOTPINK
    themeConfig.kfColorDim     = 0x4CC9;  // LOKI_TEXT_DIM
    themeConfig.kfColorAttack  = 0xF81F;  // LOKI_MAGENTA
    themeConfig.kfColorError   = 0xF800;  // LOKI_RED
    themeConfig.kfColorXp      = 0xFE60;  // LOKI_GOLD
    themeConfig.kfFont         = 1;
    themeConfig.kfBgColor      = 0x0000;  // Black

    Serial.printf("[THEME] Built-in fallback: bg + %d states\n", SPRITE_STATE_COUNT);

    // Try SD card
    if (!mountSD()) {
        sdReady = false;
        usingSDTheme = false;
        Serial.println("[THEME] No SD card — using built-in theme");
        Serial.println("[THEME] To use themes, copy the loki/ folder to your SD card root");
        return;
    }
    sdReady = true;
    Serial.println("[THEME] SD card mounted OK");

    // Check for common mis-copy: sdcard_contents/ folder copied instead of loki/
    if (SD.exists("/sdcard_contents/loki/themes")) {
        Serial.println("[THEME] WARNING: Found /sdcard_contents/loki/ — wrong path!");
        Serial.println("[THEME] Copy the loki/ folder INSIDE sdcard_contents/ to SD root");
        Serial.println("[THEME] Expected: SD:/loki/themes/  Got: SD:/sdcard_contents/loki/themes/");
    }

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

                // Read display name from theme.cfg
                char cfgPath[64];
                snprintf(cfgPath, sizeof(cfgPath), "/loki/themes/%s/theme.cfg", themeNames[themeCount]);
                strncpy(themeDisplayNames[themeCount], themeNames[themeCount], 23); // Default to folder name
                File cfg = SD.open(cfgPath, FILE_READ);
                if (cfg) {
                    while (cfg.available()) {
                        String line = cfg.readStringUntil('\n');
                        line.trim();
                        if (line.startsWith("name")) {
                            int eq = line.indexOf('=');
                            if (eq > 0) {
                                String val = line.substring(eq + 1);
                                val.trim();
                                strncpy(themeDisplayNames[themeCount], val.c_str(), 23);
                                themeDisplayNames[themeCount][23] = '\0';
                                break;
                            }
                        }
                    }
                    cfg.close();
                }

                Serial.printf("[THEME] Found: %s (%s)\n", themeNames[themeCount], themeDisplayNames[themeCount]);
                themeCount++;
            }
            entry = themesDir.openNextFile();
        }
        themesDir.close();
    }

    // Sort themes alphabetically by display name
    for (int i = 0; i < themeCount - 1; i++) {
        for (int j = i + 1; j < themeCount; j++) {
            if (strcasecmp(themeDisplayNames[i], themeDisplayNames[j]) > 0) {
                char tmp[24];
                strncpy(tmp, themeNames[i], 23); strncpy(themeNames[i], themeNames[j], 23); strncpy(themeNames[j], tmp, 23);
                strncpy(tmp, themeDisplayNames[i], 23); strncpy(themeDisplayNames[i], themeDisplayNames[j], 23); strncpy(themeDisplayNames[j], tmp, 23);
            }
        }
    }

    Serial.printf("[THEME] %d SD themes available\n", themeCount);
    if (themeCount == 0) {
        Serial.println("[THEME] No themes found at /loki/themes/");
        Serial.println("[THEME] Make sure SD card has: /loki/themes/<name>/theme.cfg");
        if (!SD.exists("/loki/themes")) {
            Serial.println("[THEME] /loki/themes/ directory does not exist!");
        }
    }

    // Load saved theme from NVS, default to "loki_dark"
    Preferences p;
    p.begin("loki", true);
    String savedTheme = p.getString("theme", "loki_dark");
    p.end();

    bool loaded = false;
    unmountSD();

    // Try saved theme first
    for (int i = 0; i < themeCount; i++) {
        if (strcmp(themeNames[i], savedTheme.c_str()) == 0) {
            loaded = loadTheme(savedTheme.c_str());
            break;
        }
    }

    // Fall back to loki_dark, then first available
    if (!loaded) {
        for (int i = 0; i < themeCount; i++) {
            if (strcmp(themeNames[i], "loki_dark") == 0) {
                loaded = loadTheme("loki_dark");
                break;
            }
        }
    }
    if (!loaded && themeCount > 0) {
        loadTheme(themeNames[0]);
    } else if (!loaded) {
        usingSDTheme = false;
        Serial.println("[THEME] No SD themes — using built-in");
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
const char* getThemeDisplayName(int index) {
    if (index < 0 || index >= themeCount) return "";
    return themeDisplayNames[index];
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
// DRAW SPLASH IMAGE
// =============================================================================

bool drawSplash() {
    // Try SD theme's splash.bmp first
    if (usingSDTheme) {
        char path[64];
        snprintf(path, sizeof(path), "%ssplash.bmp", currentThemePath);
        if (drawSDBmpOpaque(path, 0, 0)) return true;
    }
    // Fall back to PROGMEM loki splash
    drawProgmemRGB565(splash_data, 0, 0, SPLASH_W, SPLASH_H);
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
            if (drawSDBmpTransparent(framePath, x, y, themeConfig.colorBg)) return true;
        }

        // Fallback: try old flat format (state1.bmp, state2.bmp)
        char flatPath[80];
        snprintf(flatPath, sizeof(flatPath), "%s%s%d.bmp", currentThemePath, state, frame);
        if (drawSDBmpTransparent(flatPath, x, y, themeConfig.colorBg)) return true;
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
// STATUS ICON FROM SD THEME
// =============================================================================

bool drawStatusIcon(const char* state, int x, int y) {
    if (!usingSDTheme) return false;

    // Look for the 42x42 status icon: <state>/<state>_icon.bmp
    // Composite against surface color since icons sit on the status bar
    char iconPath[100];
    snprintf(iconPath, sizeof(iconPath), "%s%s/%s_icon.bmp", currentThemePath, state, state);
    return drawSDBmpTransparent(iconPath, x, y, themeConfig.colorSurface);
}

// =============================================================================
// THEME COMMENTS
// =============================================================================

bool getRandomComment(const char* state, char* buf, int bufLen) {
    if (!usingSDTheme) return false;
    if (!mountSD()) return false;

    char path[80];
    snprintf(path, sizeof(path), "%scomments.txt", currentThemePath);

    File f = SD.open(path, FILE_READ);
    if (!f) { unmountSD(); return false; }

    // Find the [state] section and count lines
    char section[16];
    snprintf(section, sizeof(section), "[%s]", state);
    bool inSection = false;
    int lineCount = 0;

    // First pass: count lines in this section
    while (f.available()) {
        String line = f.readStringUntil('\n');
        line.trim();
        if (line.startsWith("[")) {
            inSection = (line == section);
            continue;
        }
        if (inSection && line.length() > 0) lineCount++;
    }

    if (lineCount == 0) {
        f.close(); unmountSD();
        return false;
    }

    // Pick a random line
    int target = random(0, lineCount);

    // Second pass: find that line
    f.seek(0);
    inSection = false;
    int current = 0;
    while (f.available()) {
        String line = f.readStringUntil('\n');
        line.trim();
        if (line.startsWith("[")) {
            inSection = (line == section);
            continue;
        }
        if (inSection && line.length() > 0) {
            if (current == target) {
                strncpy(buf, line.c_str(), bufLen - 1);
                buf[bufLen - 1] = '\0';
                f.close(); unmountSD();
                return true;
            }
            current++;
        }
    }

    f.close(); unmountSD();
    return false;
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
