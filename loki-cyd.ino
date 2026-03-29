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
static const unsigned long TOUCH_DEBOUNCE_MS = 200;

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

    LokiScoreManager::load();
    LokiStorage::setup();
    LokiStorage::loadCredentials();
    LokiUI::setup();

    drawSplash();
    delay(2000);

    LokiPet::setup();
    LokiPet::drawPetScreen();

    // Auto-reconnect WiFi if saved credentials exist (don't auto-scan)
    char savedSSID[33], savedPass[65];
    if (loadWifiCreds(savedSSID, sizeof(savedSSID), savedPass, sizeof(savedPass))) {
        Serial.printf("[LOKI] Auto-connecting WiFi to '%s'...\n", savedSSID);
        LokiPet::setStatus("Connecting WiFi...");
        WiFi.mode(WIFI_STA);
        WiFi.begin(savedSSID, savedPass);

        // Wait up to 10 seconds for connection
        unsigned long t0 = millis();
        while (WiFi.status() != WL_CONNECTED && millis() - t0 < 10000) {
            delay(250);
        }

        if (WiFi.status() == WL_CONNECTED) {
            Serial.printf("[LOKI] WiFi connected: %s\n", WiFi.localIP().toString().c_str());
            LokiPet::setStatus("Idle");
            LokiRecon::setWiFi(savedSSID, savedPass);

            // Auto-start Web UI if it was enabled
            if (loadWebUISetting()) {
                LokiWeb::setup();
                Serial.printf("[LOKI] Web UI auto-started on http://%s/\n", WiFi.localIP().toString().c_str());
            }
        } else {
            Serial.println("[LOKI] WiFi auto-connect failed");
            LokiPet::setStatus("WiFi failed - tap menu");
        }
    }

    Serial.println("[LOKI] Setup complete. Tap screen to interact.");
}

// =============================================================================
// SPLASH SCREEN
// =============================================================================

void drawSplash() {
    tft.fillScreen(LOKI_BG_DARK);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(LOKI_GREEN);
    tft.setTextSize(3);
    tft.drawString("LOKI", SCREEN_WIDTH / 2, SCREEN_HEIGHT / 3);
    tft.setTextSize(1);
    tft.setTextColor(LOKI_TEXT_DIM);
    tft.drawString("LAN Orchestrated", SCREEN_WIDTH / 2, SCREEN_HEIGHT / 3 + 35);
    tft.drawString("Key Infiltrator", SCREEN_WIDTH / 2, SCREEN_HEIGHT / 3 + 50);
    tft.setTextColor(LOKI_DIM);
    tft.drawString(LOKI_VERSION, SCREEN_WIDTH / 2, SCREEN_HEIGHT / 3 + 80);
    tft.setTextColor(LOKI_TEXT);
    tft.drawString("Tap to begin", SCREEN_WIDTH / 2, SCREEN_HEIGHT * 3 / 4);
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

void handleTouch() {
    int tx, ty;
    if (!getTouchPoint(tx, ty)) return;

    unsigned long now = millis();
    if (now - lastTouchTime < TOUCH_DEBOUNCE_MS) return;
    lastTouchTime = now;

    Serial.printf("[TOUCH] screen=%d x=%d y=%d\n", currentScreen, tx, ty);

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
            if (ty >= SCREEN_HEIGHT - 30) {
                if (tx < SCREEN_WIDTH / 2) {
                    // Clear log
                    LokiPet::clearKillFeed();
                    attackLogScroll = 0;
                    drawAttackLog();
                } else {
                    // Back button
                    currentScreen = SCREEN_PET;
                    LokiPet::drawPetScreen();
                }
            } else if (ty < SCREEN_HEIGHT / 2) {
                // Scroll up
                attackLogScroll = max(0, attackLogScroll - 5);
                drawAttackLog();
            } else {
                // Scroll down
                int maxScroll = max(0, LokiPet::getKillFeedCount() - attackLogMaxLines);
                attackLogScroll = min(attackLogScroll + 5, maxScroll);
                drawAttackLog();
            }
            break;

        case SCREEN_STATS:
            currentScreen = SCREEN_PET;
            LokiPet::drawPetScreen();
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
            LokiUI::handleDeviceListTouch(tx, ty);
            if (LokiUI::getDetailDevice() >= 0) {
                currentScreen = SCREEN_DEVICE_DETAIL;
                LokiUI::drawDeviceDetail(LokiUI::getDetailDevice());
            }
            if (ty >= SCREEN_HEIGHT - 30 && tx >= SCREEN_WIDTH / 2) {
                currentScreen = SCREEN_MENU;
                drawMenu();
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

#define MENU_ITEMS 11
static const char* menuLabels[MENU_ITEMS] = {
    "WiFi",
    "Auto",
    "Web UI",
    "Manual",
    "Hosts",
    "Credentials",
    "Attack Log",
    "Stats",
    "Theme",
    "Brightness",
    "Back"
};

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

void drawMenu() {
    tft.fillScreen(LOKI_BG_DARK);

    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(LOKI_GREEN);
    tft.setTextSize(2);
    tft.drawString("LOKI MENU", SCREEN_WIDTH / 2, 20);

    tft.setTextSize(1);
    int startY = 45;
    int itemH = (SCREEN_HEIGHT - startY - 10) / MENU_ITEMS;
    if (itemH > SCALE_H(35)) itemH = SCALE_H(35);

    for (int i = 0; i < MENU_ITEMS; i++) {
        int y = startY + i * itemH;
        uint16_t color = LOKI_TEXT;

        tft.setTextColor(color);

        char label[36];
        if (i == 0) {
            bool wifiUp = (WiFi.status() == WL_CONNECTED);
            snprintf(label, sizeof(label), "WiFi: %s", wifiUp ? WiFi.SSID().c_str() : "Connect");
            if (wifiUp) color = LOKI_GREEN;
        } else if (i == 1) {
            snprintf(label, sizeof(label), "Auto: %s", LokiRecon::isRunning() ? "STOP" : "START");
            if (!WiFi.isConnected()) color = LOKI_TEXT_DIM;
        } else if (i == 2) {
            snprintf(label, sizeof(label), "Web UI: %s", LokiWeb::isRunning() ? "ON" : "OFF");
            if (!WiFi.isConnected()) color = LOKI_TEXT_DIM;
        } else if (i == 3) {
            strncpy(label, "Manual", sizeof(label));
            if (!WiFi.isConnected()) color = LOKI_TEXT_DIM;
        } else if (i == 8 && LokiSprites::getThemeCount() > 0) {
            snprintf(label, sizeof(label), "Theme: %s", LokiSprites::getThemeConfig().name);
        } else if (i == 9) {
            snprintf(label, sizeof(label), "Brightness: %d%%", brightnessLevels[brightnessIdx]);
        } else {
            strncpy(label, menuLabels[i], sizeof(label));
        }

        tft.setTextColor(color);
        tft.drawString(label, SCREEN_WIDTH / 2, y + itemH / 2);

        tft.drawLine(10, y + itemH - 1, SCREEN_WIDTH - 10, y + itemH - 1, LOKI_BG_ELEVATED);
    }

    tft.setTextDatum(TL_DATUM);
}

void handleMenuTouch(int x, int y) {
    int startY = 45;
    int itemH = (SCREEN_HEIGHT - startY - 10) / MENU_ITEMS;
    if (itemH > SCALE_H(35)) itemH = SCALE_H(35);
    int selected = (y - startY) / itemH;

    if (selected < 0 || selected >= MENU_ITEMS) return;

    switch (selected) {
        case 0: // WiFi
            currentScreen = SCREEN_WIFI_SCAN;
            LokiUI::drawWifiScan();
            break;

        case 1: // Auto — toggle start/stop
            if (LokiRecon::isRunning()) {
                LokiRecon::stop();
                autonomousMode = false;
                LokiPet::setMood(MOOD_IDLE);
                LokiPet::setStatus("Idle");
            } else if (WiFi.isConnected()) {
                autonomousMode = true;
                LokiRecon::start();
                LokiPet::setStatus("Auto started");
            } else {
                LokiPet::setStatus("Connect WiFi first");
            }
            currentScreen = SCREEN_PET;
            LokiPet::drawPetScreen();
            break;

        case 2: // Web UI — toggle
            if (!WiFi.isConnected()) {
                LokiPet::setStatus("Connect WiFi first");
            } else if (LokiWeb::isRunning()) {
                LokiWeb::stop();
                saveWebUISetting(false);
                LokiPet::setStatus("Idle");
            } else {
                LokiWeb::setup();
                saveWebUISetting(true);
                char msg[40];
                snprintf(msg, sizeof(msg), "Web: %s", WiFi.localIP().toString().c_str());
                LokiPet::setStatus(msg);
            }
            drawMenu();
            return;

        case 3: // Manual — show hosts for manual attack selection
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

        case 4: // Hosts
            currentScreen = SCREEN_DEVICE_LIST;
            LokiUI::setDetailDevice(-1);
            LokiUI::drawDeviceList();
            break;

        case 5: // Loot
            currentScreen = SCREEN_LOOT;
            LokiUI::drawLootView();
            break;

        case 6: // Attack Log
            currentScreen = SCREEN_KILL_FEED;
            attackLogScroll = max(0, LokiPet::getKillFeedCount() - 30); // Start near bottom
            drawAttackLog();
            break;

        case 7: // Stats
            currentScreen = SCREEN_STATS;
            drawStatsScreen();
            break;

        case 8: // Theme
            if (LokiSprites::getThemeCount() > 0) {
                currentThemeIdx = (currentThemeIdx + 1) % LokiSprites::getThemeCount();
                LokiSprites::loadTheme(LokiSprites::getThemeName(currentThemeIdx));
                LokiPet::setup();
            }
            currentScreen = SCREEN_PET;
            LokiPet::drawPetScreen();
            break;

        case 9: // Brightness — cycle through levels
            brightnessIdx = (brightnessIdx + 1) % 4;
            setBrightness(brightnessLevels[brightnessIdx]);
            drawMenu();  // Redraw to show new level
            return;

        case 10: // Back
            currentScreen = SCREEN_PET;
            LokiPet::drawPetScreen();
            break;
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

    LokiPet::drawPetScreen();
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

        // Scroll indicator
        tft.setTextColor(LOKI_TEXT_DIM);
        tft.setTextDatum(MC_DATUM);
        char scrollInfo[24];
        snprintf(scrollInfo, sizeof(scrollInfo), "%d-%d / %d",
                 attackLogScroll + 1, min(attackLogScroll + attackLogMaxLines, count), count);
        tft.drawString(scrollInfo, SCREEN_WIDTH / 2, SCREEN_HEIGHT - 40);
        tft.drawString("^ Tap upper half to scroll up ^", SCREEN_WIDTH / 2, SCREEN_HEIGHT - 28);
        tft.setTextDatum(TL_DATUM);
    }

    // Clear + Back buttons
    int btnY2 = SCREEN_HEIGHT - 22;
    int btnW2 = SCREEN_WIDTH / 2 - 6;
    tft.fillRoundRect(4, btnY2, btnW2, 20, 3, LOKI_BG_SURFACE);
    tft.drawRoundRect(4, btnY2, btnW2, 20, 3, LOKI_MAGENTA);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(LOKI_MAGENTA);
    tft.drawString("Clear Log", 4 + btnW2 / 2, btnY2 + 10);

    tft.fillRoundRect(SCREEN_WIDTH / 2 + 2, btnY2, btnW2, 20, 3, LOKI_BG_SURFACE);
    tft.drawRoundRect(SCREEN_WIDTH / 2 + 2, btnY2, btnW2, 20, 3, LOKI_RED);
    tft.setTextColor(LOKI_RED);
    tft.drawString("Back", SCREEN_WIDTH / 2 + 2 + btnW2 / 2, btnY2 + 10);
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
    drawStat("Total Scans", score.totalScans, LOKI_TEXT);

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

    if (LokiRecon::isRunning()) {
        LokiScore stats = LokiRecon::getStats();
        LokiPet::updateStats(stats);

        switch (LokiRecon::getPhase()) {
            case PHASE_DISCOVER: LokiPet::setMood(MOOD_SCANNING); break;
            case PHASE_IDENTIFY: LokiPet::setMood(MOOD_SCANNING); break;
            case PHASE_ATTACK:   LokiPet::setMood(MOOD_ATTACKING); break;
            case PHASE_DONE:
                LokiPet::setMood(MOOD_HAPPY);
                LokiScoreManager::save();
                LokiStorage::saveCredentials();
                LokiStorage::saveDevices();
                LokiStorage::saveAttackLog();
                break;
            default: break;
        }
    }

    delay(50);
}
