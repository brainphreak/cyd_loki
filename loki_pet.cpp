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
static char comment[64] = "";
static unsigned long lastAnimFrame = 0;
static unsigned long nextAnimInterval = 1500;
static unsigned long lastCommentTime = 0;
static unsigned long lastMoodChange = 0;
static int animFrame = 0;
static int spriteFrame = 1;
static bool useSprites = false;
static bool hasBG = false;

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
    // Read all positions from theme config — theme creators control everything
    LokiThemeConfig& tc = LokiSprites::getThemeConfig();

    ly.headerH = tc.headerH;

    ly.statsY = tc.statsY;
    ly.statColW = SCREEN_WIDTH / tc.statsCols;
    ly.statIconSize = tc.statsIconSize;
    ly.statRowH = tc.statsRowH;
    ly.statsH = ly.statRowH * tc.statsRows;

    int maxStats = tc.statsRows * tc.statsCols;
    for (int i = 0; i < maxStats && i < 9; i++) {
        int col = i % tc.statsCols, row = i / tc.statsCols;
        ly.statX[i] = col * ly.statColW + ly.statIconSize + 6;
        ly.statY[i] = ly.statsY + row * ly.statRowH + ly.statRowH / 2;
    }

    ly.statusY = tc.statusY;
    ly.statusH = tc.statusH;
    ly.statusIconSize = 28;  // PROGMEM status icon size
    ly.statusIconX = tc.statusIconX;
    ly.statusIconY = ly.statusY + (ly.statusH - ly.statusIconSize) / 2;
    ly.statusTextY = ly.statusY + ly.statusH / 2;

    ly.dlgX = tc.dlgX;
    ly.dlgY = tc.dlgY;
    ly.dlgW = tc.dlgW;
    ly.dlgH = tc.dlgH;
    ly.dlgTextX = ly.dlgX + 8;
    ly.dlgTextY = ly.dlgY + 8;
    ly.dlgTextW = ly.dlgW - 16;
    ly.dlgTextH = ly.dlgH - 16;

    ly.charX = tc.charX;
    ly.charY = tc.charY;
    ly.charW = tc.charW;
    ly.charH = tc.charH;

    ly.kfY = tc.kfY;
    ly.kfLineH = tc.kfLineH;
    ly.kfLines = tc.kfLines;
}

// =============================================================================
// SETUP
// =============================================================================

void setup() {
    memset(killFeed, 0, sizeof(killFeed));
    killFeedCount = 0;
    currentMood = MOOD_IDLE;
    lastCommentTime = millis();

    computeLayout();

    LokiSprites::setup();
    useSprites = LokiSprites::themeLoaded() && LokiSprites::getFrameCount("idle") > 0;
    hasBG = LokiSprites::themeLoaded();

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

static void restoreBackgroundStrip(int x, int y, int w, int h) {
    // Restore background from PROGMEM (always available as fallback bg)
    // This works for both built-in and SD themes since the bg is baked in PROGMEM
    if (x >= 0 && y >= 0 && x + w <= BG_W && y + h <= BG_H && w <= BG_W) {
        static uint16_t rowBuf[320];  // Static buffer — max screen width
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
    } else {
        tft.fillRect(x, y, w, h, TC.colorBg);
    }
}

static void drawHeader() {
    // Title "LOKI" and XP icon are baked into the background
    // Restore the areas where dynamic text goes, then draw new text

    // XP value
    int textH = 10;
    restoreBackgroundStrip(TC.xpX, TC.xpY, 55, textH + 2);
    char xpBuf[12];
    snprintf(xpBuf, sizeof(xpBuf), "%lu", (unsigned long)displayScore.xp);
    tft.setTextDatum(ML_DATUM);
    tft.setTextSize(1);
    tft.setTextColor(TC.colorHighlight);
    tft.drawString(xpBuf, TC.xpX, TC.xpY + textH / 2);

    // WiFi status
    restoreBackgroundStrip(TC.wifiX, TC.wifiY, SCREEN_WIDTH - TC.wifiX, textH + 2);
    tft.setTextDatum(MR_DATUM);
    bool wifiUp = (WiFi.status() == WL_CONNECTED);
    tft.setTextColor(wifiUp ? TC.colorSuccess : TC.colorTextDim);
    tft.drawString(wifiUp ? "Connected" : "Offline", SCREEN_WIDTH - 5, TC.wifiY + textH / 2);

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
    uint16_t colors[9] = {
        TC.colorAccent, TC.colorAccentBright, TC.colorError,
        TC.colorCracked, TC.colorAlert, TC.colorText,
        TC.colorText, TC.colorText, TC.colorAccentBright,
    };

    tft.setTextSize(1);
    tft.setTextDatum(ML_DATUM);

    for (int i = 0; i < 9; i++) {
        // Restore background behind the value area
        int clearX = ly.statX[i];
        int clearY = ly.statY[i] - 5;
        int clearW = ly.statColW - ly.statIconSize - 10;
        restoreBackgroundStrip(clearX, clearY, clearW, 12);

        char buf[10];
        snprintf(buf, sizeof(buf), "%lu", (unsigned long)values[i]);
        tft.setTextColor(colors[i], TC.colorBg);
        tft.drawString(buf, ly.statX[i], ly.statY[i]);
    }

    tft.setTextDatum(TL_DATUM);
}

// =============================================================================
// DRAW DYNAMIC: Status line (with optional icon)
// =============================================================================

static void drawStatusIcon(const char* state, int x, int y) {
    // Find and draw the PROGMEM status icon for this state
    for (int i = 0; i < STATUS_ICON_COUNT; i++) {
        const char* name = (const char*)pgm_read_ptr(&statusIcons[i].name);
        if (strcmp(name, state) == 0) {
            const uint16_t* data = (const uint16_t*)pgm_read_ptr(&statusIcons[i].data);
            if (data) {
                // Draw with transparency
                for (int row = 0; row < STATUS_ICON_SIZE; row++) {
                    for (int col = 0; col < STATUS_ICON_SIZE; col++) {
                        uint16_t px = pgm_read_word(&data[row * STATUS_ICON_SIZE + col]);
                        if (px != 0xF81F) {  // Skip magenta
                            tft.drawPixel(x + col, y + row, px);
                        }
                    }
                }
            }
            return;
        }
    }
}

static void drawStatus() {
    // Clear status area
    restoreBackgroundStrip(0, ly.statusY, SCREEN_WIDTH, ly.statusH);

    int textX;
    if (showStatusIcon) {
        // Draw 28x28 status icon from PROGMEM
        int iconY = ly.statusY + (ly.statusH - STATUS_ICON_SIZE) / 2;
        drawStatusIcon(moodToState(currentMood), 4, iconY);
        textX = 4 + STATUS_ICON_SIZE + 4;
    } else {
        textX = 8;
    }

    tft.setTextSize(1);

    // Line 1: Action name
    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(TC.colorAccent);
    tft.drawString(statusMain, textX, ly.statusY + 4);

    // Line 2: Detail text
    if (statusSub[0]) {
        tft.setTextColor(TC.colorTextDim);
        tft.drawString(statusSub, textX, ly.statusY + 4 + 14);
    }

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
    restoreBackgroundStrip(ly.dlgX, ly.dlgY, ly.dlgW, ly.dlgH + 10); // +10 for tail

    if (!comment[0]) return;

    tft.setTextColor(TC.colorText);
    tft.setTextSize(1);

    int curX = ly.dlgTextX;
    int curY = ly.dlgTextY;
    int maxX = ly.dlgTextX + ly.dlgTextW;
    int maxY = ly.dlgTextY + ly.dlgTextH;

    const char* p = comment;
    while (*p && curY < maxY - 8) {
        char word[32];
        int wi = 0;
        while (*p && *p != ' ' && wi < 30) word[wi++] = *p++;
        word[wi] = '\0';
        if (*p == ' ') p++;

        int wordW = tft.textWidth(word);
        if (curX + wordW > maxX && curX > ly.dlgTextX) {
            curX = ly.dlgTextX;
            curY += 12;
            if (curY >= maxY - 8) break;
        }
        tft.setCursor(curX, curY);
        tft.print(word);
        tft.print(" ");
        curX += wordW + tft.textWidth(" ");
    }
}

// =============================================================================
// DRAW DYNAMIC: Kill feed (bottom area)
// =============================================================================

static void drawKillFeed() {
    // Clear kill feed area
    int kfH = ly.kfLineH * ly.kfLines;
    tft.fillRect(0, ly.kfY, SCREEN_WIDTH, kfH, TC.colorBg);

    tft.setTextSize(1);
    int startIdx = killFeedCount - ly.kfLines;
    if (startIdx < 0) startIdx = 0;

    for (int i = startIdx; i < killFeedCount && (i - startIdx) < ly.kfLines; i++) {
        int y = ly.kfY + (i - startIdx) * ly.kfLineH;
        tft.setTextColor(killFeed[i].color);
        tft.setCursor(3, y);
        tft.print(killFeed[i].text);
    }
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
    drawStatus();
    drawCharacter();
    drawDialogue();
    drawKillFeed();
}

void loop() {
    unsigned long now = millis();

    // Process dirty flags from Core 0 (thread-safe drawing on Core 1 only)
    if (moodDirty) {
        moodDirty = false;
        drawHeader();
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

}  // namespace LokiPet

