// =============================================================================
// Loki CYD — Autonomous Network Recon Virtual Pet
// Architecture:
//   Core 0: Recon engine (WiFi scan, port scan, brute force)
//   Core 1: Pet UI (character animation, stats, touch input, web server)
// =============================================================================

#include <TFT_eSPI.h>
#include <SPI.h>
#include <WiFi.h>
#include <SD.h>
#include <Preferences.h>

#include "loki_config.h"
#include "loki_types.h"
#include "loki_pet.h"
#include "loki_recon.h"
#include "loki_score.h"
#include "loki_web.h"
#include "loki_sprites.h"
#include "loki_ui.h"
#include "loki_storage.h"

#define TC (LokiSprites::getThemeConfig())

// Touch driver
#ifdef CYD_28
  #include "CYD28_TouchscreenR.h"
  CYD28_TouchR touch(TOUCH_CS, TOUCH_IRQ);
#endif

TFT_eSPI tft = TFT_eSPI();

// =============================================================================
// STATE
// =============================================================================

static LokiScreen currentScreen = SCREEN_PET;
static bool autonomousMode = false;
static unsigned long lastTouchTime = 0;
static const unsigned long TOUCH_DEBOUNCE_MS = 150;

// Drag/swipe tracking
static bool touching = false;
static bool didScroll = false;  // True if any scrolling happened during this touch
static int touchStartX = 0, touchStartY = 0;
static int touchLastX = 0, touchLastY = 0;
static unsigned long touchStartTime = 0;
static const int DRAG_THRESHOLD = 15;  // px to distinguish drag from tap

// Touch calibration
static Preferences prefs;
static bool touchCalibrated = false;

// Attack log scroll
static int attackLogScroll = 0;
static int attackLogMaxLines = 0;

// =============================================================================
// TOUCH CALIBRATION
// =============================================================================

#ifdef CYD_35
static uint16_t calData[5];

void runTouchCalibration() {
    tft.fillScreen(LOKI_BG_DARK);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(LOKI_GREEN);
    tft.setTextSize(2);
    tft.drawString("TOUCH CALIBRATION", SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2 - 30);
    tft.setTextSize(1);
    tft.setTextColor(LOKI_TEXT);
    tft.drawString("Touch the arrow markers", SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2);
    tft.drawString("on each corner", SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2 + 15);
    tft.setTextDatum(TL_DATUM);

    delay(1000);

    tft.calibrateTouch(calData, LOKI_GREEN, LOKI_BG_DARK, 15);

    // Save calibration
    prefs.begin("loki", false);
    for (int i = 0; i < 5; i++) {
        char key[8];
        snprintf(key, sizeof(key), "cal%d", i);
        prefs.putUShort(key, calData[i]);
    }
    prefs.putBool("calibrated", true);
    prefs.end();

    touchCalibrated = true;
    tft.setTouch(calData);

    tft.fillScreen(LOKI_BG_DARK);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(LOKI_GREEN);
    tft.setTextSize(2);
    tft.drawString("Calibration saved!", SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2);
    tft.setTextDatum(TL_DATUM);
    delay(1000);
}

void clearTouchCalibration() {
    prefs.begin("loki", false);
    prefs.putBool("calibrated", false);
    prefs.end();
}

void loadTouchCalibration() {
    prefs.begin("loki", true);
    touchCalibrated = prefs.getBool("calibrated", false);
    if (touchCalibrated) {
        for (int i = 0; i < 5; i++) {
            char key[8];
            snprintf(key, sizeof(key), "cal%d", i);
            calData[i] = prefs.getUShort(key, 0);
        }
        tft.setTouch(calData);
    }
    prefs.end();
}
#endif

// =============================================================================
// WIFI CREDENTIAL PERSISTENCE
// =============================================================================

void saveWifiCreds(const char* ssid, const char* pass) {
    Preferences p;
    p.begin("lokiwifi", false);
    p.putString("ssid", ssid);
    p.putString("pass", pass);
    p.end();
    Serial.printf("[WIFI] Saved credentials for '%s'\n", ssid);
}

bool loadWifiCreds(char* ssid, int ssidLen, char* pass, int passLen) {
    Preferences p;
    p.begin("lokiwifi", true);
    String s = p.getString("ssid", "");
    String pw = p.getString("pass", "");
    p.end();

    if (s.length() == 0) return false;

    strncpy(ssid, s.c_str(), ssidLen - 1);
    ssid[ssidLen - 1] = '\0';
    strncpy(pass, pw.c_str(), passLen - 1);
    pass[passLen - 1] = '\0';
    Serial.printf("[WIFI] Loaded saved credentials for '%s'\n", ssid);
    return true;
}

void clearWifiCreds() {
    Preferences p;
    p.begin("lokiwifi", false);
    p.clear();
    p.end();
}

void saveWebUISetting(bool enabled) {
    Preferences p;
    p.begin("lokisettings", false);
    p.putBool("webui", enabled);
    p.end();
}

bool loadWebUISetting() {
    Preferences p;
    p.begin("lokisettings", true);
    bool enabled = p.getBool("webui", false);
    p.end();
    return enabled;
}

// =============================================================================
// SETUP
// =============================================================================

void setup() {
    Serial.begin(115200);
    Serial.println();
    Serial.println("=================================");
    Serial.println("  LOKI CYD " LOKI_VERSION);
    Serial.println("  Autonomous Recon Virtual Pet");
    Serial.println("=================================");

    tft.init();
    tft.setRotation(0);
    tft.fillScreen(LOKI_BG_DARK);

    pinMode(TFT_BL_PIN, OUTPUT);
    digitalWrite(TFT_BL_PIN, HIGH);

    // Touch init
    #ifdef CYD_28
      touch.begin();
    #endif

    #ifdef CYD_35
      loadTouchCalibration();
      if (!touchCalibrated) {
          runTouchCalibration();
      }
    #endif

    // Load theme first so splash can use theme colors
    LokiUI::setup();
    LokiPet::setup();

    // --- Splash screen: clean image, no overlays ---
    LokiSprites::drawSplash();
    unsigned long splashStart = millis();

    // Check for recalibrate (hold screen during splash)
    #ifdef CYD_35
      if (touchCalibrated) {
          delay(1500);
          uint16_t tx, ty;
          if (tft.getTouch(&tx, &ty)) {
              Serial.println("[TOUCH] Boot recalibration triggered");
              clearTouchCalibration();
              runTouchCalibration();
              LokiSprites::drawSplash();  // Redraw splash after calibration
              splashStart = millis();
          }
      }
    #endif

    // Load data while splash is showing
    LokiScoreManager::load();
    LokiStorage::setup();
    LokiStorage::loadCredentials();

    // Auto-reconnect WiFi
    char savedSSID[33], savedPass[65];
    if (loadWifiCreds(savedSSID, sizeof(savedSSID), savedPass, sizeof(savedPass))) {
        Serial.printf("[LOKI] Auto-connecting WiFi to '%s'...\n", savedSSID);
        WiFi.mode(WIFI_STA);
        WiFi.begin(savedSSID, savedPass);

        unsigned long t0 = millis();
        while (WiFi.status() != WL_CONNECTED && millis() - t0 < 10000) {
            delay(250);
        }

        if (WiFi.status() == WL_CONNECTED) {
            Serial.printf("[LOKI] WiFi connected: %s\n", WiFi.localIP().toString().c_str());
            // Sync NTP time for timestamps
            configTime(0, 0, "pool.ntp.org", "time.nist.gov");
            Serial.println("[LOKI] NTP time sync started");
            LokiRecon::setWiFi(savedSSID, savedPass);
            if (loadWebUISetting()) {
                LokiWeb::setup();
                Serial.printf("[LOKI] Web UI auto-started on http://%s/\n", WiFi.localIP().toString().c_str());
            }
        } else {
            Serial.println("[LOKI] WiFi auto-connect failed");
        }
    }

    // Ensure splash shows for at least 5 seconds total
    unsigned long elapsed = millis() - splashStart;
    if (elapsed < 5000) delay(5000 - elapsed);

    LokiPet::drawPetScreen();
    Serial.println("[LOKI] Setup complete. Tap screen to interact.");
}

// =============================================================================
// SPLASH SCREEN
// =============================================================================

void drawSplash() {
    tft.fillScreen(TC.colorBg);

    int centerX = SCREEN_WIDTH / 2;
    int titleY = SCREEN_HEIGHT / 3 - 20;

    // Theme display name as title
    tft.setTextDatum(MC_DATUM);
    tft.setTextFont(2);
    tft.setTextSize(4);
    tft.setTextColor(TC.colorAccent);
    tft.drawString(TC.name, centerX, titleY);

    // Subtitle
    tft.setTextSize(1);
    tft.setTextColor(TC.colorTextDim);
    tft.drawString("Autonomous Network Recon", centerX, titleY + 50);
    tft.drawString("Virtual Pet", centerX, titleY + 70);

    // Version
    tft.setTextColor(TC.colorAccentDim);
    tft.drawString(LOKI_VERSION, centerX, titleY + 100);

    // Divider line
    int lineY = titleY + 120;
    tft.drawLine(60, lineY, SCREEN_WIDTH - 60, lineY, TC.colorElevated);

    // Credits
    tft.setTextColor(TC.colorTextDim);
    tft.drawString("github.com/brainphreak/cyd_loki", centerX, lineY + 20);

    // Loading bar outline
    int barX = 60, barY = SCREEN_HEIGHT - 80, barW = SCREEN_WIDTH - 120, barH = 8;
    tft.drawRect(barX, barY, barW, barH, TC.colorElevated);

    tft.setTextFont(1);
    tft.setTextDatum(TL_DATUM);
}

void splashProgress(int percent, const char* msg) {
    int barX = 60, barY = SCREEN_HEIGHT - 80, barW = SCREEN_WIDTH - 120, barH = 8;
    int fillW = (barW - 2) * percent / 100;
    tft.fillRect(barX + 1, barY + 1, fillW, barH - 2, TC.colorAccent);

    // Status text below bar
    tft.fillRect(0, barY + 14, SCREEN_WIDTH, 16, TC.colorBg);
    tft.setTextDatum(MC_DATUM);
    tft.setTextFont(2);
    tft.setTextSize(1);
    tft.setTextColor(TC.colorTextDim);
    tft.drawString(msg, SCREEN_WIDTH / 2, barY + 22);
    tft.setTextFont(1);
    tft.setTextDatum(TL_DATUM);
}

// =============================================================================
// TOUCH HANDLING
// =============================================================================

bool getTouchPoint(int& x, int& y) {
    #ifdef CYD_28
      if (touch.touched()) {
          CYD28_TS_Point p = touch.getPointScaled();
          x = map(p.x, 200, 3700, 0, SCREEN_WIDTH);
          y = map(p.y, 200, 3700, 0, SCREEN_HEIGHT);
          x = constrain(x, 0, SCREEN_WIDTH - 1);
          y = constrain(y, 0, SCREEN_HEIGHT - 1);
          return true;
      }
    #endif
    #ifdef CYD_35
      uint16_t tx, ty;
      if (tft.getTouch(&tx, &ty)) {
          x = tx;
          y = ty;
          return true;
      }
    #endif
    return false;
}

// Check if current screen supports drag scrolling
static bool isScrollableScreen() {
    return currentScreen == SCREEN_DEVICE_LIST ||
           currentScreen == SCREEN_KILL_FEED ||
           currentScreen == SCREEN_WIFI_SCAN;
}

void handleTouch() {
    int tx, ty;
    bool isTouching = getTouchPoint(tx, ty);

    // --- Drag tracking for scrollable screens ---
    if (isScrollableScreen()) {
        if (isTouching && !touching) {
            // Touch start — skip drag tracking if in button bar area
            if (ty >= SCREEN_HEIGHT - 35) {
                // Button area — process as normal tap immediately
                touching = false;
            } else {
                touching = true;
                didScroll = false;
                touchStartX = tx; touchStartY = ty;
                touchLastX = tx; touchLastY = ty;
                touchStartTime = millis();
                return;
            }
        }
        if (isTouching && touching) {
            // Scrollbar drag — only on right side (x > 75% of screen)
            if (touchStartX < SCREEN_WIDTH * 3 / 4) {
                touchLastX = tx; touchLastY = ty;
                return;
            }
            // Map touch Y directly to scroll position within scrollbar track
            int sbTop = 40;  // Scrollbar starts at list top
            int sbBottom = SCREEN_HEIGHT - 35;  // Ends above buttons
            int sbH = sbBottom - sbTop;
            float pct = constrain((float)(ty - sbTop) / sbH, 0.0f, 1.0f);

            didScroll = true;
            if (currentScreen == SCREEN_DEVICE_LIST) {
                int lineH = SCALE_H(20);
                int devCount = LokiRecon::getDeviceCount();
                int maxVisible = (SCREEN_HEIGHT - 70 - 40) / lineH;
                int maxScroll = max(0, devCount - maxVisible);
                int newScroll = (int)(pct * maxScroll);
                if (newScroll != LokiUI::getDevScroll()) {
                    LokiUI::scrollDevices(newScroll);
                    LokiUI::drawDeviceList();
                }
            } else if (currentScreen == SCREEN_KILL_FEED) {
                int maxScroll = max(0, LokiPet::getKillFeedCount() - attackLogMaxLines);
                int newScroll = (int)(pct * maxScroll);
                if (newScroll != attackLogScroll) {
                    attackLogScroll = newScroll;
                    drawAttackLog();
                }
            }
            touchLastY = ty;
            return;
        }
        if (!isTouching && touching) {
            // Touch released
            touching = false;
            if (didScroll || touchStartX >= SCREEN_WIDTH * 3 / 4) {
                // Was a scrollbar interaction — don't select anything
                lastTouchTime = millis();
                return;
            }
            // No scrolling happened + touch was on left side = tap
            tx = touchStartX;
            ty = touchStartY;
            isTouching = true;  // Force tap processing
        }
    }

    if (!isTouching) return;

    unsigned long now = millis();
    if (now - lastTouchTime < TOUCH_DEBOUNCE_MS) return;
    lastTouchTime = now;

    switch (currentScreen) {
        case SCREEN_PET:
            // Tap top-right corner (WiFi status area)
            if (ty < 35 && tx > SCREEN_WIDTH / 2) {
                if (WiFi.status() == WL_CONNECTED) {
                    drawWifiInfo();  // Show IP info
                } else {
                    currentScreen = SCREEN_WIFI_SCAN;  // Shortcut to WiFi connect
                    LokiUI::drawWifiScan();
                }
            } else {
                currentScreen = SCREEN_MENU;
                drawMenu();
            }
            break;

        case SCREEN_MENU:
            handleMenuTouch(tx, ty);
            break;

        case SCREEN_KILL_FEED:
            // Back button (drag handles scrolling)
            if (ty >= SCREEN_HEIGHT - 30) {
                currentScreen = SCREEN_PET;
                LokiPet::drawPetScreen();
            }
            break;

        case SCREEN_STATS:
            currentScreen = SCREEN_PET;
            LokiPet::drawPetScreen();
            break;

        case SCREEN_THEME_PICKER:
            handleThemePickerTouch(tx, ty);
            break;

        case SCREEN_WIFI_SCAN: {
            LokiScreen next = LokiUI::handleWifiScanTouch(tx, ty);
            if (LokiUI::hasWifiSelection()) {
                const char* newSSID = LokiUI::getSelectedSSID();
                const char* newPass = LokiUI::getSelectedPass();
                saveWifiCreds(newSSID, newPass);
                LokiRecon::setWiFi(newSSID, newPass);

                // Connect WiFi (don't auto-scan)
                LokiPet::setStatus("Connecting...");
                WiFi.mode(WIFI_STA);
                WiFi.disconnect();
                delay(200);
                WiFi.begin(newSSID, newPass);

                unsigned long wt = millis();
                while (WiFi.status() != WL_CONNECTED && millis() - wt < 10000) delay(250);

                if (WiFi.status() == WL_CONNECTED) {
                    LokiPet::setStatus("WiFi connected");
                    configTime(0, 0, "pool.ntp.org", "time.nist.gov");
                } else {
                    LokiPet::setStatus("WiFi failed");
                }
                currentScreen = SCREEN_PET;
                LokiPet::drawPetScreen();
            } else if (next != SCREEN_WIFI_SCAN) {
                currentScreen = next;
                if (next == SCREEN_MENU) drawMenu();
            }
            break;
        }

        case SCREEN_WIFI_KEYBOARD: {
            LokiScreen next = LokiUI::handleKeyboardTouch(tx, ty, 64);
            if (LokiUI::hasWifiSelection()) {
                const char* newSSID = LokiUI::getSelectedSSID();
                const char* newPass = LokiUI::getSelectedPass();
                saveWifiCreds(newSSID, newPass);
                LokiRecon::setWiFi(newSSID, newPass);

                currentScreen = SCREEN_PET;
                LokiPet::setStatus("Connecting...");
                LokiPet::drawPetScreen();

                WiFi.mode(WIFI_STA);
                WiFi.disconnect();
                delay(200);
                WiFi.begin(newSSID, newPass);
                unsigned long wt = millis();
                while (WiFi.status() != WL_CONNECTED && millis() - wt < 10000) delay(250);

                if (WiFi.status() == WL_CONNECTED) {
                    LokiPet::setStatus("WiFi connected");
                    configTime(0, 0, "pool.ntp.org", "time.nist.gov");
                } else {
                    LokiPet::setStatus("WiFi failed");
                }
            } else if (next != SCREEN_WIFI_KEYBOARD) {
                currentScreen = next;
                if (next == SCREEN_MENU) drawMenu();
            }
            break;
        }

        case SCREEN_DEVICE_LIST:
            if (ty >= SCREEN_HEIGHT - 35) {
                // Back button (centered)
                currentScreen = SCREEN_MENU;
                drawMenu();
            } else {
                LokiUI::handleDeviceListTouch(tx, ty);
                if (LokiUI::getDetailDevice() >= 0) {
                    currentScreen = SCREEN_DEVICE_DETAIL;
                    LokiUI::drawDeviceDetail(LokiUI::getDetailDevice());
                }
            }
            break;

        case SCREEN_DEVICE_DETAIL:
            LokiUI::handleDeviceDetailTouch(tx, ty);
            // Back button (right half)
            if (ty >= SCREEN_HEIGHT - 30 && tx >= SCREEN_WIDTH / 2) {
                currentScreen = SCREEN_DEVICE_LIST;
                LokiUI::drawDeviceList();
            }
            break;

        case SCREEN_LOOT:
            LokiUI::handleLootTouch(tx, ty);
            if (ty >= SCREEN_HEIGHT - 30 && tx >= SCREEN_WIDTH / 2) {
                currentScreen = SCREEN_MENU;
                drawMenu();
            }
            break;

        default:
            break;
    }
}

// =============================================================================
// MENU
// =============================================================================

#define MENU_ITEMS 7
static const char* menuLabels[MENU_ITEMS] = {
    "Auto",
    "Manual",
    "Hosts",
    "Credentials",
    "Attack Log",
    "Settings",
    "Back"
};

#define SETTINGS_ITEMS 8
static const char* settingsLabels[SETTINGS_ITEMS] = {
    "WiFi",
    "Web UI",
    "Theme",
    "Brightness",
    "Calibrate",
    "Stats",
    "Clear Data",
    "Back"
};

static bool inSettingsMenu = false;

// Brightness levels
static const int brightnessLevels[] = {25, 50, 75, 100};
static int brightnessIdx = 3;  // Start at 100%

static void setBrightness(int percent) {
    int duty = map(percent, 0, 100, 0, 255);
    ledcSetup(0, 5000, 8);
    ledcAttachPin(TFT_BL_PIN, 0);
    ledcWrite(0, duty);
}
static int currentThemeIdx = 0;

static void drawMenuItems(const char** labels, int count, const char* title) {
    tft.fillScreen(LOKI_BG_DARK);

    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(LOKI_GREEN);
    tft.setTextFont(2);
    tft.setTextSize(2);
    tft.drawString(title, SCREEN_WIDTH / 2, 22);

    tft.setTextFont(2);
    tft.setTextSize(2);
    int startY = 50;
    int itemH = (SCREEN_HEIGHT - startY - 10) / count;
    if (itemH > 60) itemH = 60;

    for (int i = 0; i < count; i++) {
        int y = startY + i * itemH;
        uint16_t color = LOKI_TEXT;
        char label[36];

        if (!inSettingsMenu) {
            // Main menu dynamic labels
            if (i == 0) {
                snprintf(label, sizeof(label), "Auto: %s", LokiRecon::isRunning() ? "STOP" : "START");
                if (!WiFi.isConnected()) color = LOKI_TEXT_DIM;
            } else if (i == 1) {
                strncpy(label, "Manual", sizeof(label));
                if (!WiFi.isConnected()) color = LOKI_TEXT_DIM;
            } else {
                strncpy(label, labels[i], sizeof(label));
            }
        } else {
            // Settings menu dynamic labels
            if (i == 0) {
                bool wifiUp = (WiFi.status() == WL_CONNECTED);
                snprintf(label, sizeof(label), "WiFi: %s", wifiUp ? WiFi.SSID().c_str() : "Connect");
                if (wifiUp) color = LOKI_GREEN;
            } else if (i == 1) {
                snprintf(label, sizeof(label), "Web UI: %s", LokiWeb::isRunning() ? "ON" : "OFF");
            } else if (i == 2) {
                snprintf(label, sizeof(label), "Theme: %s", LokiSprites::getThemeConfig().name);
            } else if (i == 3) {
                snprintf(label, sizeof(label), "Brightness: %d%%", brightnessLevels[brightnessIdx]);
            } else {
                strncpy(label, labels[i], sizeof(label));
            }
        }

        tft.setTextColor(color);
        tft.drawString(label, SCREEN_WIDTH / 2, y + itemH / 2);
        tft.drawLine(10, y + itemH - 1, SCREEN_WIDTH - 10, y + itemH - 1, LOKI_BG_ELEVATED);
    }

    tft.setTextFont(1);
    tft.setTextDatum(TL_DATUM);
}

void drawMenu() {
    inSettingsMenu = false;
    LokiPet::invalidateBackground();  // Menu overwrites bg, force redraw on return
    drawMenuItems(menuLabels, MENU_ITEMS, "LOKI MENU");
}

void drawSettingsMenu() {
    inSettingsMenu = true;
    drawMenuItems(settingsLabels, SETTINGS_ITEMS, "SETTINGS");
}

void handleMenuTouch(int x, int y) {
    int count = inSettingsMenu ? SETTINGS_ITEMS : MENU_ITEMS;
    int startY = 50;
    int itemH = (SCREEN_HEIGHT - startY - 10) / count;
    if (itemH > 60) itemH = 60;
    int selected = (y - startY) / itemH;

    if (selected < 0 || selected >= count) return;

    if (!inSettingsMenu) {
        // === MAIN MENU ===
        switch (selected) {
            case 0: // Auto — toggle
                if (LokiRecon::isRunning()) {
                    LokiRecon::stop();
                    autonomousMode = false;
                    LokiPet::setMood(MOOD_IDLE);
                    LokiPet::setStatus("Idle");
                } else if (WiFi.isConnected()) {
                    autonomousMode = true;
                    LokiPet::setStatus("Auto started");
                } else {
                    LokiPet::setStatus("Connect WiFi first");
                }
                // Draw pet screen BEFORE starting recon to avoid SD bus conflict
                currentScreen = SCREEN_PET;
                LokiPet::drawPetScreen();
                // Now safe to start recon (bg.bmp already read from SD)
                if (autonomousMode && !LokiRecon::isRunning()) {
                    LokiRecon::start();
                }
                break;

            case 1: // Manual
                if (WiFi.isConnected()) {
                    currentScreen = SCREEN_DEVICE_LIST;
                    LokiUI::setDetailDevice(-1);
                    LokiUI::drawDeviceList();
                } else {
                    LokiPet::setStatus("Connect WiFi first");
                    currentScreen = SCREEN_PET;
                    LokiPet::drawPetScreen();
                }
                break;

            case 2: // Hosts
                currentScreen = SCREEN_DEVICE_LIST;
                LokiUI::setDetailDevice(-1);
                LokiUI::drawDeviceList();
                break;

            case 3: // Credentials
                currentScreen = SCREEN_LOOT;
                LokiUI::drawLootView();
                break;

            case 4: // Attack Log
                currentScreen = SCREEN_KILL_FEED;
                attackLogScroll = max(0, LokiPet::getKillFeedCount() - 30);
                drawAttackLog();
                break;

            case 5: // Settings
                drawSettingsMenu();
                return;

            case 6: // Back
                currentScreen = SCREEN_PET;
                LokiPet::drawPetScreen();
                break;
        }
    } else {
        // === SETTINGS MENU ===
        switch (selected) {
            case 0: // WiFi
                currentScreen = SCREEN_WIFI_SCAN;
                LokiUI::drawWifiScan();
                break;

            case 1: // Web UI — toggle
                if (!WiFi.isConnected()) {
                    LokiPet::setStatus("Connect WiFi first");
                } else if (LokiWeb::isRunning()) {
                    LokiWeb::stop();
                    saveWebUISetting(false);
                } else {
                    LokiWeb::setup();
                    saveWebUISetting(true);
                }
                drawSettingsMenu();
                return;

            case 2: // Theme
                if (LokiSprites::getThemeCount() > 0) {
                    currentScreen = SCREEN_THEME_PICKER;
                    drawThemePicker();
                } else {
                    LokiPet::setStatus("No themes on SD");
                    drawSettingsMenu();
                }
                return;

            case 3: // Brightness
                brightnessIdx = (brightnessIdx + 1) % 4;
                setBrightness(brightnessLevels[brightnessIdx]);
                drawSettingsMenu();
                return;

            case 4: // Calibrate
                #ifdef CYD_35
                  clearTouchCalibration();
                  runTouchCalibration();
                #endif
                drawSettingsMenu();
                return;

            case 5: // Stats
                currentScreen = SCREEN_STATS;
                drawStatsScreen();
                break;

            case 6: // Clear Data
                LokiScoreManager::reset();
                LokiPet::setStatus("Data cleared");
                drawSettingsMenu();
                return;

            case 7: // Back to main menu
                drawMenu();
                return;
        }
    }
}

// =============================================================================
// WIFI INFO POPUP
// =============================================================================

void drawWifiInfo() {
    tft.fillScreen(LOKI_BG_DARK);

    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(LOKI_GREEN);
    tft.setTextSize(2);
    tft.drawString("WIFI INFO", SCREEN_WIDTH / 2, 20);
    tft.setTextDatum(TL_DATUM);
    tft.setTextSize(1);

    int y = 50;
    int lineH = 20;

    if (WiFi.status() == WL_CONNECTED) {
        tft.setTextColor(LOKI_GREEN);
        tft.setCursor(10, y); tft.print("Status: Connected"); y += lineH;

        tft.setTextColor(LOKI_TEXT);
        tft.setCursor(10, y); tft.printf("SSID: %s", WiFi.SSID().c_str()); y += lineH;
        tft.setCursor(10, y); tft.printf("IP: %s", WiFi.localIP().toString().c_str()); y += lineH;
        tft.setCursor(10, y); tft.printf("Gateway: %s", WiFi.gatewayIP().toString().c_str()); y += lineH;
        tft.setCursor(10, y); tft.printf("Subnet: %s", WiFi.subnetMask().toString().c_str()); y += lineH;
        tft.setCursor(10, y); tft.printf("DNS: %s", WiFi.dnsIP().toString().c_str()); y += lineH;
        tft.setCursor(10, y); tft.printf("MAC: %s", WiFi.macAddress().c_str()); y += lineH;
        tft.setCursor(10, y); tft.printf("RSSI: %d dBm", WiFi.RSSI()); y += lineH;
        tft.setCursor(10, y); tft.printf("Channel: %d", WiFi.channel()); y += lineH;
    } else {
        tft.setTextColor(LOKI_RED);
        tft.setCursor(10, y); tft.print("Status: Disconnected"); y += lineH;
        tft.setTextColor(LOKI_TEXT_DIM);
        tft.setCursor(10, y); tft.print("Use menu > WiFi to connect"); y += lineH;
    }

    // Tap to go back
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(LOKI_TEXT_DIM);
    tft.drawString("Tap to go back", SCREEN_WIDTH / 2, SCREEN_HEIGHT - 20);
    tft.setTextDatum(TL_DATUM);

    // Wait for tap then return to pet
    while (true) {
        int tx2, ty2;
        if (getTouchPoint(tx2, ty2)) {
            delay(200);  // Debounce
            break;
        }
        delay(50);
    }

    LokiPet::invalidateBackground();
    LokiPet::drawPetScreen();
}

// =============================================================================
// THEME PICKER
// =============================================================================

void drawThemePicker() {
    tft.fillScreen(LOKI_BG_DARK);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(LOKI_GREEN);
    tft.setTextFont(2);
    tft.setTextSize(2);
    tft.drawString("SELECT THEME", SCREEN_WIDTH / 2, 22);

    tft.setTextFont(2);
    tft.setTextSize(2);

    int count = LokiSprites::getThemeCount();
    int startY = 50;
    int itemH = 45;

    for (int i = 0; i < count && i < 10; i++) {
        int y = startY + i * itemH;
        bool isCurrent = (i == currentThemeIdx);

        if (isCurrent) {
            tft.fillRoundRect(10, y, SCREEN_WIDTH - 20, itemH - 4, 4, LOKI_BG_ELEVATED);
            tft.drawRoundRect(10, y, SCREEN_WIDTH - 20, itemH - 4, 4, LOKI_GREEN);
        } else {
            tft.fillRoundRect(10, y, SCREEN_WIDTH - 20, itemH - 4, 4, LOKI_BG_SURFACE);
            tft.drawRoundRect(10, y, SCREEN_WIDTH - 20, itemH - 4, 4, LOKI_GUNMETAL);
        }

        tft.setTextDatum(MC_DATUM);
        tft.setTextColor(isCurrent ? LOKI_GREEN : LOKI_TEXT);
        tft.drawString(LokiSprites::getThemeDisplayName(i), SCREEN_WIDTH / 2, y + itemH / 2 - 2);
    }

    // Back button
    int btnY = SCREEN_HEIGHT - 35;
    tft.fillRoundRect(SCREEN_WIDTH / 2 - 50, btnY, 100, 28, 3, LOKI_BG_SURFACE);
    tft.drawRoundRect(SCREEN_WIDTH / 2 - 50, btnY, 100, 28, 3, LOKI_RED);
    tft.setTextColor(LOKI_RED);
    tft.drawString("Back", SCREEN_WIDTH / 2, btnY + 14);
    tft.setTextFont(1);
    tft.setTextDatum(TL_DATUM);
}

void handleThemePickerTouch(int x, int y) {
    // Back button
    if (y >= SCREEN_HEIGHT - 35) {
        currentScreen = SCREEN_MENU;
        drawMenu();
        return;
    }

    // Theme selection
    int startY = 50;
    int itemH = 45;
    int count = LokiSprites::getThemeCount();
    int idx = (y - startY) / itemH;

    if (idx >= 0 && idx < count) {
        currentThemeIdx = idx;
        const char* themeName = LokiSprites::getThemeName(idx);
        Serial.printf("[THEME] Selected: %s\n", LokiSprites::getThemeDisplayName(idx));

        // Highlight selected theme immediately for visual feedback
        int iy = startY + idx * itemH;
        tft.fillRoundRect(10, iy, SCREEN_WIDTH - 20, itemH - 4, 4, LOKI_GREEN);
        tft.setTextDatum(MC_DATUM);
        tft.setTextFont(2);
        tft.setTextSize(2);
        tft.setTextColor(LOKI_BG_DARK);
        tft.drawString(LokiSprites::getThemeDisplayName(idx), SCREEN_WIDTH / 2, iy + itemH / 2 - 2);
        // Loading text above Back button
        int loadY = SCREEN_HEIGHT - 55;
        tft.setTextSize(1);
        tft.setTextColor(LOKI_GREEN);
        tft.drawString("Loading...", SCREEN_WIDTH / 2, loadY);
        tft.setTextFont(1);
        tft.setTextDatum(TL_DATUM);
        LokiSprites::loadTheme(themeName);
        // Save to NVS so it persists across reboots
        Preferences p;
        p.begin("loki", false);
        p.putString("theme", themeName);
        p.end();
        LokiPet::setup(false);
        currentScreen = SCREEN_PET;
        LokiPet::drawPetScreen();
    }
}

// =============================================================================
// ATTACK LOG VIEW — Full scrollable attack log
// =============================================================================

void drawAttackLog() {
    tft.fillScreen(LOKI_BG_DARK);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(LOKI_GREEN);
    tft.setTextSize(2);
    tft.drawString("ATTACK LOG", SCREEN_WIDTH / 2, 15);
    tft.setTextDatum(TL_DATUM);
    tft.setTextSize(1);

    int count = LokiPet::getKillFeedCount();

    if (count == 0) {
        tft.setTextColor(LOKI_TEXT_DIM);
        tft.setCursor(10, 45);
        tft.print("No attack activity yet.");
    } else {
        int lineH = 12;
        int startY = 35;
        attackLogMaxLines = (SCREEN_HEIGHT - 50 - startY) / lineH;

        // Clamp scroll
        int maxScroll = max(0, count - attackLogMaxLines);
        attackLogScroll = min(attackLogScroll, maxScroll);

        for (int i = 0; i < attackLogMaxLines && (i + attackLogScroll) < count; i++) {
            char text[52];
            uint16_t color;
            LokiPet::getKillFeedLine(i + attackLogScroll, text, sizeof(text), &color);
            tft.setTextColor(color);
            tft.setCursor(3, startY + i * lineH);
            tft.print(text);
        }

        // Scrollbar (right edge)
        if (count > attackLogMaxLines) {
            int sbX = SCREEN_WIDTH - 6;
            int sbY = startY;
            int sbH = attackLogMaxLines * lineH;
            int thumbH = max(10, sbH * attackLogMaxLines / count);
            int maxScrl = max(1, count - attackLogMaxLines);
            int thumbY = sbY + (sbH - thumbH) * attackLogScroll / maxScrl;
            tft.fillRect(sbX, sbY, 4, sbH, LOKI_BG_ELEVATED);
            tft.fillRect(sbX, thumbY, 4, thumbH, LOKI_GREEN);
        }
    }

    // Back button (drag to scroll)
    int btnY2 = SCREEN_HEIGHT - 26;
    tft.fillRoundRect(SCREEN_WIDTH / 2 - 50, btnY2, 100, 22, 3, LOKI_BG_SURFACE);
    tft.drawRoundRect(SCREEN_WIDTH / 2 - 50, btnY2, 100, 22, 3, LOKI_RED);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(LOKI_RED);
    tft.drawString("Back", SCREEN_WIDTH / 2, btnY2 + 11);
    tft.setTextDatum(TL_DATUM);
}

// =============================================================================
// STATS VIEW
// =============================================================================

void drawStatsScreen() {
    tft.fillScreen(LOKI_BG_DARK);
    LokiScore score = LokiScoreManager::get();

    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(LOKI_GREEN);
    tft.setTextSize(2);
    tft.drawString("STATS", SCREEN_WIDTH / 2, 20);

    tft.setTextSize(1);
    int y = 55;
    int lineH = 25;

    auto drawStat = [&](const char* label, uint32_t value, uint16_t color) {
        tft.setTextColor(LOKI_TEXT);
        tft.setTextDatum(TL_DATUM);
        tft.setCursor(15, y);
        tft.print(label);
        tft.setTextColor(color);
        tft.setTextDatum(TR_DATUM);
        char buf[12];
        snprintf(buf, sizeof(buf), "%lu", (unsigned long)value);
        tft.drawString(buf, SCREEN_WIDTH - 15, y);
        y += lineH;
    };

    drawStat("Hosts Found", score.hostsFound, LOKI_CYAN);
    drawStat("Ports Found", score.portsFound, LOKI_BRIGHT);
    drawStat("Cracked", score.servicesCracked, LOKI_HOTPINK);
    drawStat("Files Stolen", score.filesStolen, LOKI_MAGENTA);
    drawStat("Vulns Found", score.vulnsFound, LOKI_RED);
    drawStat("Attacks", score.totalAttacks, LOKI_TEXT);

    y += 10;
    tft.drawLine(15, y, SCREEN_WIDTH - 15, y, LOKI_BG_ELEVATED);
    y += 15;
    drawStat("XP", score.xp, LOKI_GOLD);

    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(LOKI_TEXT_DIM);
    tft.drawString("Tap to go back", SCREEN_WIDTH / 2, SCREEN_HEIGHT - 20);
    tft.setTextDatum(TL_DATUM);
}

// =============================================================================
// MAIN LOOP (Core 1)
// =============================================================================

void loop() {
    handleTouch();

    if (currentScreen == SCREEN_PET) {
        LokiPet::loop();
    }

    if (LokiWeb::isRunning()) {
        LokiWeb::loop();
    }

    {
        static bool reconFinalized = false;
        if (LokiRecon::isRunning()) {
            reconFinalized = false;
            LokiScore stats = LokiRecon::getStats();
            LokiPet::updateStats(stats);

            switch (LokiRecon::getPhase()) {
                case PHASE_DISCOVER: LokiPet::setMood(MOOD_SCANNING); break;
                case PHASE_IDENTIFY: LokiPet::setMood(MOOD_SCANNING); break;
                case PHASE_ATTACK:   LokiPet::setMood(MOOD_ATTACKING); break;
                default: break;
            }
        } else if (LokiRecon::isDone() && !reconFinalized) {
            reconFinalized = true;
            LokiScore stats = LokiRecon::getStats();
            LokiPet::updateStats(stats);
            LokiPet::setMood(MOOD_HAPPY);
            LokiScoreManager::save();
            LokiStorage::saveCredentials();
            LokiStorage::saveDevices();
            LokiStorage::saveAttackLog();
        }
    }

    delay(50);
}
