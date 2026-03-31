// =============================================================================
// Loki CYD — Virtual Pet Display Module
// Uses pre-rendered composite background from SD card.
// Dynamic elements (character, stat values, status text, comments, kill feed)
// drawn on top at layout-defined coordinates.
// =============================================================================

#include "loki_pet.h"
#include "loki_config.h"
#include "loki_types.h"
#include "loki_sprites.h"
#include "asset_bg.h"
#include "loki_status_icons.h"
#include <TFT_eSPI.h>
#include <WiFi.h>

extern TFT_eSPI tft;

// Shorthand for theme colors — avoids long chains in draw code
#define TC (LokiSprites::getThemeConfig())

namespace LokiPet {

// --- Settings ---
static bool showStatusIcon = true;  // Toggle for status icon display

// --- Internal state ---
static LokiMood currentMood = MOOD_IDLE;
static LokiScore displayScore = {0};
static char statusMain[32] = "Idle";
static char statusSub[32] = "";
static char prevStatusMain[32] = "";  // Track changes to avoid full redraw
static char comment[64] = "";
static unsigned long lastAnimFrame = 0;
static unsigned long nextAnimInterval = 1500;
static unsigned long lastCommentTime = 0;
static unsigned long lastMoodChange = 0;
static int animFrame = 0;
static int spriteFrame = 1;
static bool useSprites = false;
static bool hasBG = false;

// Background tracking — skip SD bg.bmp read when already on screen
static bool bgOnScreen = false;

// Dirty flags — set by Core 0, drawn by Core 1 (thread-safe)
static volatile bool moodDirty = false;
static volatile bool statusDirty = false;
static volatile bool statsDirty = false;
static volatile bool killFeedDirty = false;
static volatile bool commentDirty = false;

// Kill feed
static LokiKillLine killFeed[LOKI_MAX_KILL_LINES];
static int killFeedCount = 0;

// Commentary
static const char* idleComments[] = {
    "Waiting for mischief...",
    "The network sleeps. I do not.",
    "Patience is a virtue. Chaos is better.",
    "I could be cracking things right now.",
    "Every port is a door waiting to open.",
    "Bored. Start a scan already.",
};
static const char* scanComments[] = {
    "Sniffing the network...",
    "Who dares connect to this subnet?",
    "I see you, little host.",
    "Mapping the battlefield.",
    "Every IP tells a story.",
};
static const char* attackComments[] = {
    "Knocking on the door...",
    "admin:admin? Worth a try.",
    "Trying every key in the ring.",
    "Your defaults betray you.",
    "This lock will yield.",
};
static const char* crackedComments[] = {
    "I'M IN!",
    "Another one falls!",
    "Too easy. Next!",
    "Your credentials are mine now.",
    "Password found. Mischief managed.",
};

#define IDLE_COUNT    6
#define SCAN_COUNT    5
#define ATTACK_COUNT  5
#define CRACKED_COUNT 5

// =============================================================================
// LAYOUT — Coordinates for dynamic elements
// Adapted from generated layout.h, but computed at runtime for resolution
// =============================================================================

struct Layout {
    // Header
    int headerH;
    // Stats
    int statsY, statsH, statColW, statRowH, statIconSize;
    int statX[9], statY[9];
    // Frise
    int friseY, friseH;
    // Status
    int statusY, statusH, statusIconX, statusIconY, statusIconSize;
    int statusTextX, statusTextY;
    // Dialogue
    int dlgX, dlgY, dlgW, dlgH;
    int dlgTextX, dlgTextY, dlgTextW, dlgTextH;
    // Character
    int charX, charY, charW, charH;
    // Kill feed
    int kfY, kfLineH, kfLines;
};

static Layout ly;

static void computeLayout() {
    LokiThemeConfig& tc = LokiSprites::getThemeConfig();
    float sx = SCREEN_WIDTH / 222.0f;
    float sy = SCREEN_HEIGHT / 480.0f;

    // Compute defaults first (matches the working built-in layout)
    ly.headerH = 32;

    ly.statsY = 34;
    int iconSz = min((int)(18 * sx), 22);
    ly.statIconSize = iconSz;
    ly.statRowH = iconSz + 8;
    ly.statColW = SCREEN_WIDTH / 3;
    ly.statsH = ly.statRowH * 3;

    for (int i = 0; i < 9; i++) {
        int col = i % 3, row = i / 3;
        ly.statX[i] = col * ly.statColW + ly.statIconSize + 11;
        ly.statY[i] = ly.statsY + row * ly.statRowH + ly.statRowH / 2;
    }

    int statsBottom = ly.statsY + ly.statsH;
    ly.statusY = statsBottom + 1;  // No frise, directly after stats
    ly.statusH = 45;
    ly.statusIconSize = 42;
    ly.statusIconX = 4;
    ly.statusIconY = ly.statusY + (ly.statusH - ly.statusIconSize) / 2;
    ly.statusTextY = ly.statusY + ly.statusH / 2;

    ly.dlgX = (int)(4 * sx);
    ly.dlgY = ly.statusY + ly.statusH + 4;
    ly.dlgW = SCREEN_WIDTH - (int)(8 * sx);
    ly.dlgH = 54;
    ly.dlgTextX = ly.dlgX + 8;
    ly.dlgTextY = ly.dlgY + 8;
    ly.dlgTextW = ly.dlgW - 16;
    ly.dlgTextH = ly.dlgH - 16;

    ly.charX = (SCREEN_WIDTH - 175) / 2;
    ly.charY = ly.dlgY + ly.dlgH + 12;
    ly.charW = 175;
    ly.charH = 175;

    int kfTop = ly.charY + ly.charH + 8;
    ly.kfY = kfTop;
    ly.kfLineH = max(10, (SCREEN_HEIGHT - kfTop) / 4);
    ly.kfLines = min(4, (SCREEN_HEIGHT - ly.kfY) / ly.kfLineH);

    // Override with theme config values if an SD theme has custom layout
    if (LokiSprites::themeLoaded() && tc.headerH > 0) {
        ly.headerH = tc.headerH;

        if (tc.statsY > 0) ly.statsY = tc.statsY;
        if (tc.statsIconSize > 0) ly.statIconSize = tc.statsIconSize;
        if (tc.statsRowH > 0) ly.statRowH = tc.statsRowH;
        if (tc.statsCols > 0) ly.statColW = SCREEN_WIDTH / tc.statsCols;
        if (tc.statsRows > 0) ly.statsH = ly.statRowH * tc.statsRows;

        int cols = tc.statsCols > 0 ? tc.statsCols : 3;
        for (int i = 0; i < 9; i++) {
            int col = i % cols, row = i / cols;
            ly.statX[i] = col * ly.statColW + ly.statIconSize + 11;
            ly.statY[i] = ly.statsY + row * ly.statRowH + ly.statRowH / 2;
        }

        if (tc.statusY > 0) { ly.statusY = tc.statusY; ly.statusIconY = ly.statusY + (ly.statusH - ly.statusIconSize) / 2; ly.statusTextY = ly.statusY + ly.statusH / 2; }
        if (tc.statusH > 0) ly.statusH = tc.statusH;
        if (tc.statusIconX > 0) ly.statusIconX = tc.statusIconX;

        if (tc.dlgX > 0) ly.dlgX = tc.dlgX;
        if (tc.dlgY > 0) ly.dlgY = tc.dlgY;
        if (tc.dlgW > 0) ly.dlgW = tc.dlgW;
        if (tc.dlgH > 0) ly.dlgH = tc.dlgH;
        ly.dlgTextX = ly.dlgX + 8;
        ly.dlgTextY = ly.dlgY + 8;
        ly.dlgTextW = ly.dlgW - 16;
        ly.dlgTextH = ly.dlgH - 16;

        if (tc.charX > 0) ly.charX = tc.charX;
        if (tc.charY > 0) ly.charY = tc.charY;
        if (tc.charW > 0) ly.charW = tc.charW;
        if (tc.charH > 0) ly.charH = tc.charH;

        if (tc.kfY > 0) ly.kfY = tc.kfY;
        if (tc.kfLineH > 0) ly.kfLineH = tc.kfLineH;
        if (tc.kfLines > 0) ly.kfLines = tc.kfLines;
    }
}

// =============================================================================
// SETUP
// =============================================================================

void setup(bool fullInit) {
    if (fullInit) {
        memset(killFeed, 0, sizeof(killFeed));
        killFeedCount = 0;
        currentMood = MOOD_IDLE;
        lastCommentTime = millis();
        LokiSprites::setup();
    }

    computeLayout();
    spriteFrame = 1;

    useSprites = LokiSprites::themeLoaded() && LokiSprites::getFrameCount("idle") > 0;
    hasBG = LokiSprites::themeLoaded();
    bgOnScreen = false;  // Force bg redraw after theme switch

    if (hasBG) Serial.println("[PET] Theme loaded from SD");
    else Serial.println("[PET] No theme — using fallback graphics");
}

// =============================================================================
// MOOD → SPRITE STATE
// =============================================================================

static const char* moodToState(LokiMood mood) {
    switch (mood) {
        case MOOD_SCANNING:  return "scan";
        case MOOD_ATTACKING: return "attack";
        case MOOD_CRACKED:   return "attack";
        case MOOD_STEALING:  return "steal";
        default:             return "idle";
    }
}

static const char* moodToStr(LokiMood mood) {
    switch (mood) {
        case MOOD_SCANNING:  return "Scanning";
        case MOOD_ATTACKING: return "Attacking";
        case MOOD_CRACKED:   return "CRACKED!";
        case MOOD_STEALING:  return "Stealing";
        case MOOD_SLEEPING:  return "Zzz...";
        case MOOD_HAPPY:     return "Happy";
        case MOOD_BORED:     return "Bored";
        default:             return "Idle";
    }
}

// =============================================================================
// REDRAW: Background (only when needed)
// =============================================================================

static void redrawBackground() {
    if (hasBG) {
        LokiSprites::drawBackground();
    } else {
        tft.fillScreen(TC.colorBg);
        tft.fillRect(0, 0, SCREEN_WIDTH, ly.headerH, TC.colorSurface);
        tft.fillRect(0, ly.statusY, SCREEN_WIDTH, ly.statusH, TC.colorSurface);
        tft.drawLine(0, ly.kfY - 4, SCREEN_WIDTH, ly.kfY - 4, TC.colorAccentDim);
    }
}

// =============================================================================
// DRAW DYNAMIC: Header (LOKI + XP + mood)
// =============================================================================

// Clear functions for different area types:
// - Header/stat text: use surface color (flat area next to baked icons)
// - Character/dialogue: use bg color (flat open area)
// - Kill feed: always black
// - PROGMEM fallback: pixel-perfect restore from built-in bg data

static void clearWithColor(int x, int y, int w, int h, uint16_t color) {
    tft.fillRect(x, y, w, h, color);
}

static void restoreFromProgmem(int x, int y, int w, int h) {
    if (x >= 0 && y >= 0 && x + w <= BG_W && y + h <= BG_H && w <= BG_W) {
        static uint16_t rowBuf[320];
        tft.startWrite();
        for (int row = y; row < y + h; row++) {
            int len = min(w, 320);
            tft.setAddrWindow(x, row, len, 1);
            for (int col = 0; col < len; col++) {
                rowBuf[col] = pgm_read_word(&bg_data[row * BG_W + x + col]);
            }
            tft.pushColors(rowBuf, len);
        }
        tft.endWrite();
    }
}

static void clearArea(int x, int y, int w, int h, uint16_t color) {
    if (!useSprites) {
        // No SD theme — use PROGMEM pixel-perfect restore
        restoreFromProgmem(x, y, w, h);
    } else {
        // SD theme — fill with flat color
        clearWithColor(x, y, w, h, color);
    }
}

static void drawHeader() {
    // Title "LOKI" and XP icon are baked into the background
    // Restore the areas where dynamic text goes, then draw new text

    // XP value — position from theme config or default
    int xpX = (TC.xpVal.x >= 0) ? TC.xpVal.x : (SCREEN_WIDTH / 2 - 4 + 22 - 16);
    int xpY = (TC.xpVal.y >= 0) ? TC.xpVal.y : (ly.headerH / 2);
    clearArea(xpX, 2, 60, ly.headerH - 4, TC.colorSurface);
    char xpBuf[12];
    snprintf(xpBuf, sizeof(xpBuf), "%lu", (unsigned long)displayScore.xp);
    tft.setTextDatum(TC.xpVal.datum);
    tft.setTextFont(TC.xpVal.font);
    tft.setTextSize(TC.xpVal.fontSize);
    tft.setTextColor(TC.xpVal.color);
    tft.drawString(xpBuf, xpX, xpY);
    tft.setTextFont(1);

    // WiFi status — position from theme config or default
    int wifiX = (TC.wifiText.x >= 0) ? TC.wifiText.x : (SCREEN_WIDTH - 85);
    int wifiY = (TC.wifiText.y >= 0) ? TC.wifiText.y : (ly.headerH / 2);
    int wifiDrawX = (TC.wifiText.datum == 5) ? (SCREEN_WIDTH - 5) : wifiX;  // MR_DATUM=5 draws from right
    clearArea(wifiX, 2, SCREEN_WIDTH - wifiX, ly.headerH - 4, TC.colorSurface);
    tft.setTextDatum(TC.wifiText.datum);
    tft.setTextFont(TC.wifiText.font);
    tft.setTextSize(TC.wifiText.fontSize);
    bool wifiUp = (WiFi.status() == WL_CONNECTED);
    tft.setTextColor(wifiUp ? TC.wifiColorOn : TC.wifiColorOff);
    tft.drawString(wifiUp ? "Connected" : "Offline", wifiDrawX, wifiY);
    tft.setTextFont(1);

    tft.setTextDatum(TL_DATUM);
}

// =============================================================================
// DRAW DYNAMIC: Stat values (numbers only — icons are in background)
// =============================================================================

static void drawStatValues() {
    // 9 stats matching 3x3 grid: target, port, vuln, cred, zombie, data, networkkb, level, attacks
    // Zombie = cracked hosts (unique IPs with credentials)
    // Level = derived from other stats (matching original Loki formula)
    // NetworkKB = total identified services
    uint32_t zombies = displayScore.servicesCracked;  // Each cracked service = zombie
    uint32_t networkkb = displayScore.hostsFound;      // Hosts in knowledge base
    uint32_t level = (uint32_t)(networkkb * 0.1 + displayScore.servicesCracked * 0.2 +
                     displayScore.filesStolen * 0.1 + zombies * 0.5 +
                     displayScore.totalScans + displayScore.vulnsFound * 0.01);

    uint32_t values[9] = {
        displayScore.hostsFound, displayScore.portsFound, displayScore.vulnsFound,
        displayScore.servicesCracked, zombies, displayScore.filesStolen,
        networkkb, level, displayScore.totalScans,
    };

    for (int i = 0; i < 9; i++) {
        const ElementStyle& es = TC.stat[i];

        // Use per-stat position or grid default
        int sx = (es.x >= 0) ? es.x : ly.statX[i];
        int sy = (es.y >= 0) ? es.y : (ly.statY[i] + 1);

        // Clear only the text area — tight bounds to avoid wiping baked-in grid lines
        // Leave 1px margin from grid lines on each side
        int col = i % 3;
        int clearX = sx;
        int clearY = sy - 8;
        int clearW = ly.statColW - ly.statIconSize - 14;  // Stay inside grid cell
        int clearH = 16;  // Font 2 height
        // Don't extend past the column's right grid line
        int colRight = (col + 1) * ly.statColW - 2;
        if (clearX + clearW > colRight) clearW = colRight - clearX;
        clearArea(clearX, clearY, clearW, clearH, TC.colorBg);

        tft.setTextFont(es.font);
        tft.setTextSize(es.fontSize);
        tft.setTextDatum(es.datum);
        tft.setTextColor(es.color);

        char buf[10];
        snprintf(buf, sizeof(buf), "%lu", (unsigned long)values[i]);
        tft.drawString(buf, sx, sy);
    }

    tft.setTextFont(1);
    tft.setTextDatum(TL_DATUM);
}

// =============================================================================
// DRAW DYNAMIC: Status line (with optional icon)
// =============================================================================

static void drawStatusIconOnScreen(const char* state, int x, int y) {
    // Try SD theme 42x42 status icon first
    if (LokiSprites::drawStatusIcon(state, x, y)) return;

    // Fall back to PROGMEM status icons (loki only)
    for (int i = 0; i < STATUS_ICON_COUNT; i++) {
        const char* name = (const char*)pgm_read_ptr(&statusIcons[i].name);
        if (strcmp(name, state) == 0) {
            const uint16_t* data = (const uint16_t*)pgm_read_ptr(&statusIcons[i].data);
            if (data) {
                static uint16_t iconBuf[320];
                tft.startWrite();
                for (int row = 0; row < STATUS_ICON_SIZE; row++) {
                    int len = min(STATUS_ICON_SIZE, 320);
                    for (int col = 0; col < len; col++) {
                        uint16_t px = pgm_read_word(&data[row * STATUS_ICON_SIZE + col]);
                        if (px == 0xF81F) {
                            int bgX = x + col, bgY = y + row;
                            if (bgX < BG_W && bgY < BG_H)
                                iconBuf[col] = pgm_read_word(&bg_data[bgY * BG_W + bgX]);
                            else
                                iconBuf[col] = 0x0861;
                        } else {
                            iconBuf[col] = px;
                        }
                    }
                    tft.setAddrWindow(x, y + row, len, 1);
                    tft.pushColors(iconBuf, len);
                }
                tft.endWrite();
            }
            return;
        }
    }
}

static void drawStatusFull();  // Forward declaration

static void drawStatus() {
    int textX;
    int iconSize = TC.statusIconSize > 0 ? TC.statusIconSize : 42;
    if (showStatusIcon) {
        textX = ly.statusIconX + iconSize + 9;
    } else {
        textX = 8;
    }

    // Check if line 1 (action name) changed — if so, full redraw with icon
    bool mainChanged = (strcmp(statusMain, prevStatusMain) != 0);
    if (mainChanged) {
        strncpy(prevStatusMain, statusMain, sizeof(prevStatusMain) - 1);
        drawStatusFull();
        return;
    }

    // Only line 2 changed — just clear and redraw line 2 area
    const ElementStyle& s2 = TC.statusLine2;
    int line2X = (s2.x >= 0) ? s2.x : textX;
    int line2Y = (s2.y >= 0) ? s2.y : (ly.statusY + 23);
    // Clear just line 2 area
    clearArea(line2X, line2Y - 2, SCREEN_WIDTH - line2X - 2, 18, TC.colorSurface);

    if (statusSub[0]) {
        tft.setTextFont(s2.font);
        tft.setTextSize(s2.fontSize);
        tft.setTextDatum(s2.datum);
        tft.setTextColor(s2.color);
        tft.drawString(statusSub, line2X, line2Y);
        tft.setTextFont(1);
        tft.setTextDatum(TL_DATUM);
    }
}

// Full status redraw — icon + both lines (called when action name changes or on first draw)
static void drawStatusFull() {
    // Clear entire status area
    clearArea(0, ly.statusY, SCREEN_WIDTH, ly.statusH, TC.colorSurface);

    int textX;
    if (showStatusIcon) {
        int iconSize = TC.statusIconSize > 0 ? TC.statusIconSize : 42;
        int iconY = (TC.statusIconY >= 0) ? TC.statusIconY : (ly.statusY + (ly.statusH - iconSize) / 2);
        int iconX = ly.statusIconX;
        drawStatusIconOnScreen(moodToState(currentMood), iconX, iconY);
        textX = iconX + iconSize + 9;
    } else {
        textX = 8;
    }

    // Line 1: Action name
    const ElementStyle& s1 = TC.statusLine1;
    int line1X = (s1.x >= 0) ? s1.x : textX;
    int line1Y = (s1.y >= 0) ? s1.y : (ly.statusY + 5);
    tft.setTextFont(s1.font);
    tft.setTextSize(s1.fontSize);
    tft.setTextDatum(s1.datum);
    tft.setTextColor(s1.color);
    tft.drawString(statusMain, line1X, line1Y);

    // Line 2: Detail text
    if (statusSub[0]) {
        const ElementStyle& s2 = TC.statusLine2;
        int line2X = (s2.x >= 0) ? s2.x : textX;
        int line2Y = (s2.y >= 0) ? s2.y : (ly.statusY + 23);
        tft.setTextFont(s2.font);
        tft.setTextSize(s2.fontSize);
        tft.setTextDatum(s2.datum);
        tft.setTextColor(s2.color);
        tft.drawString(statusSub, line2X, line2Y);
    }

    tft.setTextFont(1);
    tft.setTextDatum(TL_DATUM);
}

// Forward declaration
static void drawCharacterFallback();

// =============================================================================
// DRAW DYNAMIC: Character sprite
// =============================================================================

static void drawCharacter() {
    if (useSprites) {
        const char* state = moodToState(currentMood);
        int maxFrames = LokiSprites::getFrameCount(state);
        if (maxFrames <= 0) { state = "idle"; maxFrames = LokiSprites::getFrameCount("idle"); }

        if (maxFrames > 0) {
            // Center sprite in character area (size from theme)
            int sprSize = LokiSprites::getThemeConfig().spriteSize;
            int sprX = ly.charX + (ly.charW - sprSize) / 2;
            int sprY = ly.charY + (ly.charH - sprSize) / 2;

            // No separate clear needed — the composite draw replaces transparent
            // pixels with the PROGMEM background in a single fast pass.

            // Sequential or random frame selection based on theme config
            if (LokiSprites::getThemeConfig().animSequential) {
                LokiSprites::drawCharacterFrame(state, spriteFrame, sprX, sprY);
                spriteFrame++;
                if (spriteFrame > maxFrames) spriteFrame = 1;
            } else {
                int rndFrame = random(1, maxFrames + 1);
                LokiSprites::drawCharacterFrame(state, rndFrame, sprX, sprY);
            }
        }
    } else {
        drawCharacterFallback();
    }
}

static void drawCharacterFallback() {
    // Clear character area
    tft.fillRect(ly.charX, ly.charY, ly.charW, ly.charH, TC.colorBg);

    int centerX = ly.charX + ly.charW / 2;
    int centerY = ly.charY + ly.charH / 2;
    int radius = ly.charW / 4;

    uint16_t fc = TC.colorAccent;
    if (currentMood == MOOD_ATTACKING) fc = TC.colorCracked;
    if (currentMood == MOOD_SLEEPING) fc = TC.colorAccentDim;
    if (currentMood == MOOD_CRACKED) fc = TC.colorHighlight;

    tft.drawCircle(centerX, centerY, radius, fc);
    // Horns
    tft.drawLine(centerX - radius/2, centerY - radius, centerX - radius/2 - 8, centerY - radius - 15, fc);
    tft.drawLine(centerX + radius/2, centerY - radius, centerX + radius/2 + 8, centerY - radius - 15, fc);
    // Eyes
    int eyeY = centerY - 6;
    if (currentMood == MOOD_SLEEPING || animFrame % 20 == 0) {
        tft.drawLine(centerX - 10 - 3, eyeY, centerX - 10 + 3, eyeY, fc);
        tft.drawLine(centerX + 10 - 3, eyeY, centerX + 10 + 3, eyeY, fc);
    } else {
        tft.fillCircle(centerX - 10, eyeY, 2, fc);
        tft.fillCircle(centerX + 10, eyeY, 2, fc);
    }
    // Mouth
    int mY = centerY + 6;
    if (currentMood == MOOD_HAPPY || currentMood == MOOD_CRACKED) {
        tft.drawLine(centerX - 6, mY, centerX, mY + 4, fc);
        tft.drawLine(centerX, mY + 4, centerX + 6, mY, fc);
    } else {
        tft.drawLine(centerX - 5, mY, centerX + 5, mY, fc);
    }
}

// =============================================================================
// DRAW DYNAMIC: Dialogue box (comment text)
// =============================================================================

static void drawDialogue() {
    // Restore the baked-in speech bubble background to clear old text
    clearArea(ly.dlgX, ly.dlgY, ly.dlgW, ly.dlgH + 10, TC.colorElevated); // +10 for tail

    if (!comment[0]) return;

    const ElementStyle& cs = TC.commentText;
    tft.setTextColor(cs.color);
    tft.setTextFont(cs.font);
    tft.setTextSize(cs.fontSize);

    int curX = (cs.x >= 0) ? cs.x : ly.dlgTextX;
    int curY = (cs.y >= 0) ? cs.y : ly.dlgTextY;
    int startX = curX;
    int maxX = ly.dlgTextX + ly.dlgTextW;
    int maxY = ly.dlgTextY + ly.dlgTextH;
    int lineH = (cs.font == 2) ? 18 : ((cs.font == 4) ? 28 : 10);

    const char* p = comment;
    while (*p && curY < maxY - lineH) {
        char word[32];
        int wi = 0;
        while (*p && *p != ' ' && wi < 30) word[wi++] = *p++;
        word[wi] = '\0';
        if (*p == ' ') p++;

        int wordW = tft.textWidth(word);
        if (curX + wordW > maxX && curX > startX) {
            curX = startX;
            curY += lineH;
            if (curY >= maxY - lineH) break;
        }
        tft.setCursor(curX, curY);
        tft.print(word);
        tft.print(" ");
        curX += wordW + tft.textWidth(" ");
    }
    tft.setTextFont(1);
}

// =============================================================================
// DRAW DYNAMIC: Kill feed (bottom area)
// =============================================================================

static void drawKillFeed() {
    // Clear kill feed area with theme's kill feed background color
    int kfH = ly.kfLineH * ly.kfLines;
    tft.fillRect(0, ly.kfY, SCREEN_WIDTH, kfH, TC.kfBgColor);

    tft.setTextFont(TC.kfFont);
    tft.setTextSize(1);
    int startIdx = killFeedCount - ly.kfLines;
    if (startIdx < 0) startIdx = 0;

    for (int i = startIdx; i < killFeedCount && (i - startIdx) < ly.kfLines; i++) {
        int y = ly.kfY + (i - startIdx) * ly.kfLineH;
        tft.setTextColor(killFeed[i].color);
        tft.setCursor(3, y);
        tft.print(killFeed[i].text);
    }
    tft.setTextFont(1);
}

// =============================================================================
// COMMENTARY SYSTEM
// =============================================================================

static void updateCommentary() {
    unsigned long now = millis();
    LokiThemeConfig& tc = LokiSprites::getThemeConfig();
    unsigned long interval = tc.commentIntervalMin + random(0, tc.commentIntervalMax - tc.commentIntervalMin);
    if (now - lastCommentTime < interval) return;
    lastCommentTime = now;

    // Try theme-specific comments from SD first
    const char* state = moodToState(currentMood);
    char themeBuf[64];
    if (LokiSprites::getRandomComment(state, themeBuf, sizeof(themeBuf))) {
        setComment(themeBuf);
        return;
    }

    // Fall back to hardcoded loki comments
    const char** comments;
    int count;
    switch (currentMood) {
        case MOOD_SCANNING:  comments = scanComments;    count = SCAN_COUNT;    break;
        case MOOD_ATTACKING: comments = attackComments;  count = ATTACK_COUNT;  break;
        case MOOD_CRACKED:   comments = crackedComments; count = CRACKED_COUNT; break;
        default:             comments = idleComments;    count = IDLE_COUNT;    break;
    }
    setComment(comments[random(0, count)]);
}

// =============================================================================
// PUBLIC API
// =============================================================================

void drawPetScreen() {
    redrawBackground();
    drawHeader();
    drawStatValues();
    prevStatusMain[0] = '\0';  // Force full status redraw
    drawStatus();
    drawCharacter();
    drawDialogue();
    drawKillFeed();
}

void invalidateBackground() {
    bgOnScreen = false;
}

void loop() {
    unsigned long now = millis();

    // Process dirty flags from Core 0 (thread-safe drawing on Core 1 only)
    if (moodDirty) {
        moodDirty = false;
        drawHeader();
        prevStatusMain[0] = '\0';  // Mood changed — force full status redraw (new icon)
        drawStatus();
        drawCharacter();
    }
    if (statusDirty) {
        statusDirty = false;
        drawStatus();
    }
    if (statsDirty) {
        statsDirty = false;
        drawStatValues();
    }
    if (killFeedDirty) {
        killFeedDirty = false;
        drawKillFeed();
    }
    if (commentDirty) {
        commentDirty = false;
        drawDialogue();
    }

    // Animate character at random intervals from theme config
    if (now - lastAnimFrame >= nextAnimInterval) {
        lastAnimFrame = now;
        LokiThemeConfig& tc = LokiSprites::getThemeConfig();
        nextAnimInterval = tc.animIntervalMin + random(0, tc.animIntervalMax - tc.animIntervalMin);
        animFrame++;

        const char* state = moodToState(currentMood);
        if (LokiSprites::getFrameCount(state) > 1) {
            drawCharacter();
        }
    }

    updateCommentary();

    // Refresh header every 3 seconds to update WiFi status
    static unsigned long lastHeaderRefresh = 0;
    if (now - lastHeaderRefresh > 3000) {
        lastHeaderRefresh = now;
        drawHeader();
    }

    // Revert CRACKED mood after 5 seconds
    if (currentMood == MOOD_CRACKED && now - lastMoodChange > 5000) {
        currentMood = MOOD_HAPPY;
        moodDirty = true;
    }
}

void setMood(LokiMood mood) {
    if (mood != currentMood) {
        currentMood = mood;
        lastMoodChange = millis();
        spriteFrame = 1;
        moodDirty = true;
        if (mood == MOOD_CRACKED) {
            strncpy(comment, crackedComments[random(0, CRACKED_COUNT)], sizeof(comment) - 1);
            commentDirty = true;
        }
    }
}

LokiMood getMood() { return currentMood; }

void updateStats(const LokiScore& score) {
    if (memcmp(&displayScore, &score, sizeof(LokiScore)) != 0) {
        displayScore = score;
        statsDirty = true;
    }
}

void setStatus(const char* main, const char* sub) {
    strncpy(statusMain, main, sizeof(statusMain) - 1);
    if (sub) strncpy(statusSub, sub, sizeof(statusSub) - 1);
    else statusSub[0] = '\0';
    statusDirty = true;
}

void addKillLine(const char* text, uint16_t color) {
    // Thread-safe: only update data, don't draw (Core 0 calls this)
    // Core 1 will draw on next loop iteration
    if (killFeedCount < LOKI_MAX_KILL_LINES) {
        strncpy(killFeed[killFeedCount].text, text, 51);
        killFeed[killFeedCount].color = color;
        killFeedCount++;
    } else {
        memmove(&killFeed[0], &killFeed[1], sizeof(LokiKillLine) * (LOKI_MAX_KILL_LINES - 1));
        strncpy(killFeed[LOKI_MAX_KILL_LINES - 1].text, text, 51);
        killFeed[LOKI_MAX_KILL_LINES - 1].color = color;
    }
    killFeedDirty = true;
}

void setComment(const char* text) {
    strncpy(comment, text, sizeof(comment) - 1);
    commentDirty = true;
}

void clearKillFeed() {
    killFeedCount = 0;
    memset(killFeed, 0, sizeof(killFeed));
    killFeedDirty = true;
}

void setShowStatusIcon(bool show) {
    showStatusIcon = show;
    drawStatus();
}

bool getShowStatusIcon() {
    return showStatusIcon;
}

int getKillFeedCount() {
    return killFeedCount;
}

void getKillFeedLine(int idx, char* buf, int bufLen, uint16_t* color) {
    if (idx >= 0 && idx < killFeedCount) {
        strncpy(buf, killFeed[idx].text, bufLen - 1);
        buf[bufLen - 1] = '\0';
        *color = killFeed[idx].color;
    } else {
        buf[0] = '\0';
        *color = 0;
    }
}

// Theme-aware kill feed colors
uint16_t kfInfo()    { return TC.kfColorInfo; }
uint16_t kfFound()   { return TC.kfColorFound; }
uint16_t kfSuccess() { return TC.kfColorSuccess; }
uint16_t kfCracked() { return TC.kfColorCracked; }
uint16_t kfDim()     { return TC.kfColorDim; }
uint16_t kfAttack()  { return TC.kfColorAttack; }
uint16_t kfError()   { return TC.kfColorError; }
uint16_t kfXp()      { return TC.kfColorXp; }

}  // namespace LokiPet

