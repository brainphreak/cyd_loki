// =============================================================================
// Loki CYD — UI Screens
// WiFi picker, on-screen keyboard, device list, loot viewer, attack menu
// =============================================================================

#include "loki_ui.h"
#include "loki_config.h"
#include "loki_types.h"
#include "loki_recon.h"
#include "loki_pet.h"
#include <TFT_eSPI.h>
#include <WiFi.h>

extern TFT_eSPI tft;

namespace LokiUI {

// =============================================================================
// STATE
// =============================================================================

// WiFi scan results
#define MAX_NETWORKS 20
struct WifiNet {
    char ssid[33];
    int rssi;
    int encType;
    uint8_t channel;
};
static WifiNet networks[MAX_NETWORKS];
static int networkCount = 0;
static int networkScroll = 0;

// Keyboard
static String kbInput = "";
static uint8_t kbMode = 0; // 0=lower, 1=upper, 2=symbols
static const char* kbLower[]  = {"1234567890", "qwertyuiop", "asdfghjkl^", "zxcvbnm._<"};
static const char* kbUpper[]  = {"1234567890", "QWERTYUIOP", "ASDFGHJKL^", "ZXCVBNM._<"};
static const char* kbSymbol[] = {"!@#$%&*()-", "~`+=[]{}|^", ";:'\",.<>?/", "\\-!@#$%_<"};
static const char** kbLayout = kbLower;

// Selection
static char selectedSSID[33] = {0};
static char selectedPass[65] = {0};
static bool wifiSelected = false;
static bool showPassword = true;  // Show password by default
static int detailDevIdx = -1;

// Keyboard entry target: 0=SSID, 1=password, 2=manual IP
static int kbTarget = 0;

// Device list scroll
static int devScroll = 0;

// =============================================================================
// SETUP
// =============================================================================

void setup() {
    networkCount = 0;
    wifiSelected = false;
}

// =============================================================================
// WIFI SCAN SCREEN
// =============================================================================

void drawWifiScan() {
    wifiSelected = false;  // Reset selection state on entry
    tft.fillScreen(LOKI_BG_DARK);

    // Title
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(LOKI_GREEN);
    tft.setTextSize(2);
    tft.drawString("SELECT WIFI", SCREEN_WIDTH / 2, 18);
    tft.setTextDatum(TL_DATUM);

    // Scan networks
    tft.setTextColor(LOKI_TEXT_DIM);
    tft.setTextSize(1);
    tft.setCursor(5, 38);
    tft.print("Scanning...");

    // Scan without disconnecting — ESP32 can scan while connected
    WiFi.mode(WIFI_STA);
    int n = WiFi.scanNetworks(false, false);
    networkCount = 0;
    for (int i = 0; i < n && networkCount < MAX_NETWORKS; i++) {
        String ssid = WiFi.SSID(i);
        if (ssid.length() == 0) continue;
        strncpy(networks[networkCount].ssid, ssid.c_str(), 32);
        networks[networkCount].ssid[32] = '\0';
        networks[networkCount].rssi = WiFi.RSSI(i);
        networks[networkCount].encType = WiFi.encryptionType(i);
        networks[networkCount].channel = WiFi.channel(i);
        networkCount++;
    }
    WiFi.scanDelete();

    // Clear and redraw with results
    tft.fillRect(0, 35, SCREEN_WIDTH, SCREEN_HEIGHT - 75, LOKI_BG_DARK);

    int listY = 40;
    int lineH = SCALE_H(22);
    int maxVisible = (SCREEN_HEIGHT - 80 - listY) / lineH;

    for (int i = networkScroll; i < networkCount && (i - networkScroll) < maxVisible; i++) {
        int y = listY + (i - networkScroll) * lineH;
        bool isOpen = (networks[i].encType == WIFI_AUTH_OPEN);

        // Signal bars
        int bars = 0;
        if (networks[i].rssi > -50) bars = 4;
        else if (networks[i].rssi > -60) bars = 3;
        else if (networks[i].rssi > -70) bars = 2;
        else bars = 1;

        for (int b = 0; b < 4; b++) {
            uint16_t barColor = (b < bars) ? LOKI_GREEN : LOKI_GUNMETAL;
            tft.fillRect(5 + b * 4, y + (12 - (b + 1) * 3), 3, (b + 1) * 3, barColor);
        }

        // Lock icon
        tft.setTextColor(isOpen ? LOKI_GREEN : LOKI_DIM);
        tft.setCursor(22, y + 2);
        tft.print(isOpen ? "*" : "L");

        // SSID
        tft.setTextColor(LOKI_TEXT);
        tft.setCursor(32, y + 2);
        String ssidDisp = String(networks[i].ssid);
        if (ssidDisp.length() > 26) ssidDisp = ssidDisp.substring(0, 26);
        tft.print(ssidDisp);

        // Channel
        tft.setTextColor(LOKI_TEXT_DIM);
        tft.setCursor(SCREEN_WIDTH - 25, y + 2);
        tft.printf("ch%d", networks[i].channel);
    }

    // Scrollbar (right edge)
    if (networkCount > maxVisible) {
        int sbX = SCREEN_WIDTH - 6;
        int sbY = listY;
        int sbH = maxVisible * lineH;
        int thumbH = max(10, sbH * maxVisible / networkCount);
        int maxScrl = max(1, networkCount - maxVisible);
        int thumbY = sbY + (sbH - thumbH) * networkScroll / maxScrl;
        tft.fillRect(sbX, sbY, 4, sbH, LOKI_BG_ELEVATED);
        tft.fillRect(sbX, thumbY, 4, thumbH, LOKI_GREEN);
    }

    // Bottom buttons
    int btnY = SCREEN_HEIGHT - 30;
    int btnW = SCREEN_WIDTH / 3 - 6;
    int btnH = 24;

    // Rescan
    tft.fillRoundRect(4, btnY, btnW, btnH, 3, LOKI_BG_SURFACE);
    tft.drawRoundRect(4, btnY, btnW, btnH, 3, LOKI_GREEN);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(LOKI_GREEN);
    tft.drawString("Rescan", 4 + btnW / 2, btnY + btnH / 2);

    // Manual
    int manX = SCREEN_WIDTH / 3 + 2;
    tft.fillRoundRect(manX, btnY, btnW, btnH, 3, LOKI_BG_SURFACE);
    tft.drawRoundRect(manX, btnY, btnW, btnH, 3, LOKI_BRIGHT);
    tft.setTextColor(LOKI_BRIGHT);
    tft.drawString("Manual", manX + btnW / 2, btnY + btnH / 2);

    // Back
    int backX = SCREEN_WIDTH * 2 / 3 + 2;
    tft.fillRoundRect(backX, btnY, btnW, btnH, 3, LOKI_BG_SURFACE);
    tft.drawRoundRect(backX, btnY, btnW, btnH, 3, LOKI_RED);
    tft.setTextColor(LOKI_RED);
    tft.drawString("Back", backX + btnW / 2, btnY + btnH / 2);

    tft.setTextDatum(TL_DATUM);
}

LokiScreen handleWifiScanTouch(int x, int y) {
    int btnY = SCREEN_HEIGHT - 30;

    // Rescan button
    if (y >= btnY && x < SCREEN_WIDTH / 3) {
        networkScroll = 0;
        drawWifiScan();
        return SCREEN_WIFI_SCAN;
    }

    // Manual button
    if (y >= btnY && x >= SCREEN_WIDTH / 3 && x < SCREEN_WIDTH * 2 / 3) {
        kbTarget = 0;
        kbInput = "";
        drawKeyboard("Enter SSID:", 32);
        return SCREEN_WIFI_KEYBOARD;
    }

    // Back button
    if (y >= btnY && x >= SCREEN_WIDTH * 2 / 3) {
        return SCREEN_MENU;
    }

    // Network selection
    int listY = 40;
    int lineH = SCALE_H(22);

    int idx = (y - listY) / lineH + networkScroll;
    if (idx >= 0 && idx < networkCount) {
        strncpy(selectedSSID, networks[idx].ssid, 32);
        selectedSSID[32] = '\0';

        if (networks[idx].encType == WIFI_AUTH_OPEN) {
            selectedPass[0] = '\0';
            wifiSelected = true;
            return SCREEN_PET;  // Caller will start scan
        } else {
            kbTarget = 1;
            kbInput = "";
            drawKeyboard("Password:", 64);
            return SCREEN_WIFI_KEYBOARD;
        }
    }

    return SCREEN_WIFI_SCAN;
}

// =============================================================================
// ON-SCREEN KEYBOARD
// =============================================================================

void drawKeyboard(const char* prompt, int maxLen) {
    tft.fillScreen(LOKI_BG_DARK);

    // Prompt
    tft.setTextColor(LOKI_GREEN);
    tft.setTextSize(1);
    tft.setCursor(5, 8);
    tft.print(prompt);

    // Input box
    int boxY = 22;
    tft.fillRect(5, boxY, SCREEN_WIDTH - 10, 20, LOKI_BG_SURFACE);
    tft.drawRect(5, boxY, SCREEN_WIDTH - 10, 20, LOKI_GREEN);
    tft.setTextColor(LOKI_BRIGHT);
    tft.setCursor(10, boxY + 5);
    if (kbTarget == 1 && !showPassword) {
        for (int i = 0; i < (int)kbInput.length() && i < 30; i++) tft.print('*');
    } else {
        tft.print(kbInput);
    }

    // Keyboard grid
    int kbKeyW = SCALE_W(22);
    int kbKeyH = SCALE_H(18);
    int kbKeySp = 2;
    int yOffset = 50;

    for (int row = 0; row < 4; row++) {
        int xOffset = 3;
        for (int col = 0; col < (int)strlen(kbLayout[row]); col++) {
            tft.fillRect(xOffset, yOffset, kbKeyW, kbKeyH, LOKI_BG_SURFACE);
            tft.drawRect(xOffset, yOffset, kbKeyW, kbKeyH, LOKI_GUNMETAL);
            tft.setTextColor(LOKI_TEXT);
            tft.setCursor(xOffset + kbKeyW / 3, yOffset + kbKeyH / 4);
            tft.print(kbLayout[row][col]);
            xOffset += kbKeyW + kbKeySp;
        }
        yOffset += kbKeyH + kbKeySp;
    }

    // Bottom buttons: Space, Clear, OK, Back
    int btnY = yOffset + 5;
    int btnW = SCREEN_WIDTH / 4 - 4;
    int btnH = 22;

    tft.fillRoundRect(4, btnY, btnW, btnH, 3, LOKI_BG_SURFACE);
    tft.drawRoundRect(4, btnY, btnW, btnH, 3, LOKI_TEXT_DIM);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(LOKI_TEXT_DIM);
    tft.drawString("Space", 4 + btnW / 2, btnY + btnH / 2);

    int clrX = SCREEN_WIDTH / 4 + 2;
    tft.fillRoundRect(clrX, btnY, btnW, btnH, 3, LOKI_BG_SURFACE);
    tft.drawRoundRect(clrX, btnY, btnW, btnH, 3, LOKI_MAGENTA);
    tft.setTextColor(LOKI_MAGENTA);
    tft.drawString("Clear", clrX + btnW / 2, btnY + btnH / 2);

    int okX = SCREEN_WIDTH / 2 + 2;
    tft.fillRoundRect(okX, btnY, btnW, btnH, 3, LOKI_BG_SURFACE);
    tft.drawRoundRect(okX, btnY, btnW, btnH, 3, LOKI_GREEN);
    tft.setTextColor(LOKI_GREEN);
    tft.drawString("OK", okX + btnW / 2, btnY + btnH / 2);

    int backX = SCREEN_WIDTH * 3 / 4 + 2;
    tft.fillRoundRect(backX, btnY, btnW, btnH, 3, LOKI_BG_SURFACE);
    tft.drawRoundRect(backX, btnY, btnW, btnH, 3, LOKI_RED);
    tft.setTextColor(LOKI_RED);
    tft.drawString("Back", backX + btnW / 2, btnY + btnH / 2);

    tft.setTextDatum(TL_DATUM);

    // Show/Hide toggle for password
    if (kbTarget == 1) {
        int eyeX = SCREEN_WIDTH - 50;
        int eyeY2 = 22;  // Same Y as input box
        tft.fillRoundRect(eyeX, eyeY2, 45, 20, 3, LOKI_BG_SURFACE);
        tft.drawRoundRect(eyeX, eyeY2, 45, 20, 3, LOKI_TEXT_DIM);
        tft.setTextDatum(MC_DATUM);
        tft.setTextColor(LOKI_TEXT_DIM);
        tft.drawString(showPassword ? "Hide" : "Show", eyeX + 22, eyeY2 + 10);
        tft.setTextDatum(TL_DATUM);
    }

    // Help text
    tft.setTextColor(LOKI_TEXT_DIM);
    tft.setCursor(5, btnY + btnH + 5);
    tft.print("^ = Aa/!@#   < = Bksp   _ = Space");
}

LokiScreen handleKeyboardTouch(int x, int y, int maxLen) {
    // Show/Hide password toggle
    if (kbTarget == 1 && x >= SCREEN_WIDTH - 50 && y >= 22 && y <= 42) {
        showPassword = !showPassword;
        drawKeyboard("Password:", maxLen);
        return SCREEN_WIFI_KEYBOARD;
    }

    int kbKeyW = SCALE_W(22);
    int kbKeyH = SCALE_H(18);
    int kbKeySp = 2;

    // Check keyboard keys
    int yOffset = 50;
    for (int row = 0; row < 4; row++) {
        int xOffset = 3;
        for (int col = 0; col < (int)strlen(kbLayout[row]); col++) {
            if (x >= xOffset && x <= xOffset + kbKeyW &&
                y >= yOffset && y <= yOffset + kbKeyH) {
                char c = kbLayout[row][col];

                if (c == '<') {
                    // Backspace
                    if (kbInput.length() > 0) kbInput.remove(kbInput.length() - 1);
                } else if (c == '^') {
                    // Toggle case/symbols
                    kbMode = (kbMode + 1) % 3;
                    if (kbMode == 0) kbLayout = kbLower;
                    else if (kbMode == 1) kbLayout = kbUpper;
                    else kbLayout = kbSymbol;
                } else if (c == '_') {
                    // Space
                    if ((int)kbInput.length() < maxLen) kbInput += ' ';
                } else {
                    if ((int)kbInput.length() < maxLen) kbInput += c;
                }

                drawKeyboard(kbTarget == 1 ? "Password:" : "Enter SSID:", maxLen);
                return SCREEN_WIFI_KEYBOARD;
            }
            xOffset += kbKeyW + kbKeySp;
        }
        yOffset += kbKeyH + kbKeySp;
    }

    // Bottom buttons
    int btnY2 = yOffset + 5;

    if (y >= btnY2 && y <= btnY2 + 30) {
        if (x < SCREEN_WIDTH / 4) {
            // Space
            if ((int)kbInput.length() < maxLen) kbInput += ' ';
            drawKeyboard(kbTarget == 1 ? "Password:" : "Enter SSID:", maxLen);
        } else if (x < SCREEN_WIDTH / 2) {
            // Clear
            kbInput = "";
            drawKeyboard(kbTarget == 1 ? "Password:" : "Enter SSID:", maxLen);
        } else if (x < SCREEN_WIDTH * 3 / 4) {
            // OK
            if (kbTarget == 0) {
                strncpy(selectedSSID, kbInput.c_str(), 32);
                selectedSSID[32] = '\0';
                kbInput = "";
                kbTarget = 1;
                drawKeyboard("Password:", 64);
                return SCREEN_WIFI_KEYBOARD;
            } else if (kbTarget == 1) {
                strncpy(selectedPass, kbInput.c_str(), 64);
                selectedPass[64] = '\0';
                wifiSelected = true;
                kbInput = "";
                return SCREEN_PET;  // Signal that WiFi is selected
            }
        } else {
            // Back
            kbInput = "";
            wifiSelected = false;
            return SCREEN_MENU;
        }
    }
    return SCREEN_WIFI_KEYBOARD;
}

String getKeyboardInput() { return kbInput; }
void clearKeyboardInput() { kbInput = ""; }
const char* getSelectedSSID() { return selectedSSID; }
const char* getSelectedPass() { return selectedPass; }
bool hasWifiSelection() { return wifiSelected; }

// =============================================================================
// DEVICE LIST
// =============================================================================

void drawDeviceList() {
    tft.fillScreen(LOKI_BG_DARK);

    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(LOKI_GREEN);
    tft.setTextSize(2);
    tft.drawString("HOSTS", SCREEN_WIDTH / 2, 18);
    tft.setTextDatum(TL_DATUM);

    int devCount = LokiRecon::getDeviceCount();
    LokiDevice* devs = LokiRecon::getDevices();

    int listY = 40;
    int lineH = SCALE_H(20);
    int maxVisible = (SCREEN_HEIGHT - 70 - listY) / lineH;

    if (devCount == 0) {
        tft.setTextColor(LOKI_TEXT_DIM);
        tft.setTextSize(1);
        tft.setCursor(5, 45);
        tft.print("No hosts discovered yet.");
        tft.setCursor(5, 60);
        tft.print("Run a scan first.");
    } else {

        // Clamp scroll
        int maxScroll = max(0, devCount - maxVisible);
        devScroll = min(devScroll, maxScroll);

        // Scroll indicator
        if (devCount > maxVisible) {
            tft.setTextColor(LOKI_TEXT_DIM);
            tft.setTextDatum(MR_DATUM);
            char scrollInfo[16];
            snprintf(scrollInfo, sizeof(scrollInfo), "%d-%d/%d",
                     devScroll + 1, min(devScroll + maxVisible, devCount), devCount);
            tft.drawString(scrollInfo, SCREEN_WIDTH - 5, 25);
            tft.setTextDatum(TL_DATUM);
        }

        for (int i = devScroll; i < devCount && (i - devScroll) < maxVisible; i++) {
            int y = listY + (i - devScroll) * lineH;
            LokiDevice& dev = devs[i];

            char ipStr[16];
            snprintf(ipStr, sizeof(ipStr), "%d.%d.%d.%d", dev.ip[0], dev.ip[1], dev.ip[2], dev.ip[3]);

            // Status color
            uint16_t statusColor = LOKI_TEXT_DIM;
            const char* statusStr = "?";
            switch (dev.status) {
                case STATUS_CRACKED: statusColor = LOKI_HOTPINK; statusStr = "!"; break;
                case STATUS_OPEN:    statusColor = LOKI_GREEN;   statusStr = "O"; break;
                case STATUS_LOCKED:  statusColor = LOKI_RED;     statusStr = "X"; break;
                case STATUS_TESTING: statusColor = LOKI_GOLD;    statusStr = "~"; break;
                default:             statusColor = LOKI_CYAN;    statusStr = "+"; break;
            }

            // Row background
            tft.fillRect(3, y, SCREEN_WIDTH - 6, lineH - 2, LOKI_BG_SURFACE);

            // Status indicator
            tft.setTextColor(statusColor);
            tft.setCursor(6, y + lineH / 2 - 4);
            tft.print(statusStr);

            // IP + vendor
            tft.setTextColor(LOKI_TEXT);
            tft.setCursor(18, y + lineH / 2 - 4);
            if (dev.vendor[0]) {
                tft.printf("%s (%s)", ipStr, dev.vendor);
            } else {
                tft.print(ipStr);
            }
        }
    }

    // Scrollbar (right edge)
    if (devCount > 0) {
        int maxVisible = (SCREEN_HEIGHT - 70 - listY) / lineH;
        if (devCount > maxVisible) {
            int sbX = SCREEN_WIDTH - 6;
            int sbY = listY;
            int sbH = SCREEN_HEIGHT - 70 - listY;
            int thumbH = max(10, sbH * maxVisible / devCount);
            int thumbY = sbY + (sbH - thumbH) * devScroll / max(1, devCount - maxVisible);
            tft.fillRect(sbX, sbY, 4, sbH, LOKI_BG_ELEVATED);
            tft.fillRect(sbX, thumbY, 4, thumbH, LOKI_GREEN);
        }
    }

    // Back button (drag to scroll)
    int btnY = SCREEN_HEIGHT - 30;
    tft.fillRoundRect(SCREEN_WIDTH / 2 - 50, btnY, 100, 24, 3, LOKI_BG_SURFACE);
    tft.drawRoundRect(SCREEN_WIDTH / 2 - 50, btnY, 100, 24, 3, LOKI_RED);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(LOKI_RED);
    tft.drawString("Back", SCREEN_WIDTH / 2, btnY + 12);
    tft.setTextDatum(TL_DATUM);
}

void handleDeviceListTouch(int x, int y) {
    // Back button (center bottom)
    if (y >= SCREEN_HEIGHT - 30) {
        // Handled by caller as "back"
        return;
    }

    // Tap on a device — select it for detail view
    int listY = 40;
    int lineH = SCALE_H(20);
    int idx = (y - listY) / lineH + devScroll;
    if (idx >= 0 && idx < LokiRecon::getDeviceCount()) {
        detailDevIdx = idx;
    }
}

// =============================================================================
// DEVICE DETAIL
// =============================================================================

void drawDeviceDetail(int deviceIdx) {
    if (deviceIdx < 0 || deviceIdx >= LokiRecon::getDeviceCount()) return;

    tft.fillScreen(LOKI_BG_DARK);
    LokiDevice& dev = LokiRecon::getDevices()[deviceIdx];
    detailDevIdx = deviceIdx;

    char ipStr[16];
    snprintf(ipStr, sizeof(ipStr), "%d.%d.%d.%d", dev.ip[0], dev.ip[1], dev.ip[2], dev.ip[3]);

    // Header
    tft.setTextColor(LOKI_GREEN);
    tft.setTextSize(2);
    tft.setCursor(5, 8);
    tft.print(ipStr);

    tft.setTextSize(1);
    int y = 35;

    // Type (from vendor + port classification)
    const char* typeStr = "Unknown";
    switch (dev.type) {
        case DEV_PHONE:    typeStr = "Phone/Mobile"; break;
        case DEV_LAPTOP:   typeStr = "Laptop/PC"; break;
        case DEV_ROUTER:   typeStr = "Router"; break;
        case DEV_CAMERA:   typeStr = "Camera"; break;
        case DEV_NAS:      typeStr = "NAS"; break;
        case DEV_PRINTER:  typeStr = "Printer"; break;
        case DEV_TV_MEDIA: typeStr = "TV/Media"; break;
        case DEV_IOT:      typeStr = "IoT"; break;
        case DEV_SPEAKER:  typeStr = "Smart Speaker"; break;
        case DEV_GAMING:   typeStr = "Gaming Console"; break;
        case DEV_SERVER:   typeStr = "Server"; break;
        case DEV_VM:       typeStr = "VM"; break;
        case DEV_OTHER:    typeStr = "Device"; break;
        default: break;
    }
    tft.setTextColor(LOKI_BRIGHT);
    tft.setCursor(5, y); tft.printf("Type: %s", typeStr); y += 15;

    // Vendor
    if (dev.vendor[0]) {
        tft.setTextColor(LOKI_TEXT);
        tft.setCursor(5, y); tft.printf("Vendor: %s", dev.vendor); y += 15;
    }

    // MAC
    tft.setTextColor(LOKI_TEXT_DIM);
    tft.setCursor(5, y);
    tft.printf("MAC: %02X:%02X:%02X:%02X:%02X:%02X",
               dev.mac[0], dev.mac[1], dev.mac[2], dev.mac[3], dev.mac[4], dev.mac[5]);
    y += 15;

    // Banner
    tft.setTextColor(LOKI_CYAN);
    tft.setCursor(5, y); tft.printf("Banner: %s", dev.banner); y += 15;

    // Status
    const char* statusStr = "Found";
    uint16_t statusColor = LOKI_TEXT;
    switch (dev.status) {
        case STATUS_CRACKED: statusStr = "CRACKED"; statusColor = LOKI_HOTPINK; break;
        case STATUS_OPEN:    statusStr = "OPEN";    statusColor = LOKI_GREEN; break;
        case STATUS_LOCKED:  statusStr = "LOCKED";  statusColor = LOKI_RED; break;
        case STATUS_TESTING: statusStr = "TESTING"; statusColor = LOKI_GOLD; break;
        default: break;
    }
    tft.setTextColor(statusColor);
    tft.setCursor(5, y); tft.printf("Status: %s", statusStr); y += 15;

    // Ports
    static const uint16_t scanPorts[] = LOKI_SCAN_PORTS;
    static const char* portNames[] = {"FTP","SSH","Telnet","HTTP","HTTPS","SMB","MySQL","RDP","HTTP"};
    tft.setTextColor(LOKI_TEXT_DIM);
    tft.setCursor(5, y); tft.print("Ports:"); y += 12;
    for (int p = 0; p < LOKI_MAX_PORTS; p++) {
        if (dev.openPorts & (1 << p)) {
            tft.setCursor(10, y);
            tft.setTextColor(LOKI_CYAN);
            tft.printf("  %d (%s)", scanPorts[p], portNames[p]);
            y += 12;
        }
    }

    y += 5;

    // Credentials
    if (dev.status == STATUS_CRACKED) {
        tft.setTextColor(LOKI_HOTPINK);
        tft.setCursor(5, y); tft.printf("User: %s", dev.credUser); y += 15;
        tft.setCursor(5, y); tft.printf("Pass: %s", dev.credPass); y += 20;

        // Access URLs
        tft.setTextColor(LOKI_GREEN);
        if (dev.openPorts & PORT_FTP) { tft.setCursor(5, y); tft.printf("ftp://%s/", ipStr); y += 12; }
        if (dev.openPorts & PORT_SSH) { tft.setCursor(5, y); tft.printf("ssh %s@%s", dev.credUser, ipStr); y += 12; }
        if (dev.openPorts & PORT_TELNET) { tft.setCursor(5, y); tft.printf("telnet %s", ipStr); y += 12; }
        if (dev.openPorts & PORT_HTTP) { tft.setCursor(5, y); tft.printf("http://%s/", ipStr); y += 12; }
        if (dev.openPorts & PORT_HTTPS) { tft.setCursor(5, y); tft.printf("https://%s/", ipStr); y += 12; }
        if (dev.openPorts & PORT_SMB) { tft.setCursor(5, y); tft.printf("smb://%s/", ipStr); y += 12; }
        if (dev.openPorts & PORT_MYSQL) { tft.setCursor(5, y); tft.printf("mysql://%s/", ipStr); y += 12; }
        if (dev.openPorts & PORT_RDP) { tft.setCursor(5, y); tft.printf("rdp://%s/", ipStr); y += 12; }
    }

    // Buttons: Attack, Back
    int btnY = SCREEN_HEIGHT - 30;
    int btnW = SCREEN_WIDTH / 2 - 6;

    tft.fillRoundRect(4, btnY, btnW, 24, 3, LOKI_BG_SURFACE);
    tft.drawRoundRect(4, btnY, btnW, 24, 3, LOKI_MAGENTA);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(LOKI_MAGENTA);
    tft.drawString("Attack", 4 + btnW / 2, btnY + 12);

    tft.fillRoundRect(SCREEN_WIDTH / 2 + 2, btnY, btnW, 24, 3, LOKI_BG_SURFACE);
    tft.drawRoundRect(SCREEN_WIDTH / 2 + 2, btnY, btnW, 24, 3, LOKI_RED);
    tft.setTextColor(LOKI_RED);
    tft.drawString("Back", SCREEN_WIDTH / 2 + 2 + btnW / 2, btnY + 12);

    tft.setTextDatum(TL_DATUM);
}

void handleDeviceDetailTouch(int x, int y) {
    if (y >= SCREEN_HEIGHT - 30) {
        if (x < SCREEN_WIDTH / 2) {
            // Attack button — show attack menu
            drawAttackMenu(detailDevIdx);
        }
        // Back handled by caller
    }
}

// =============================================================================
// ATTACK MENU — Manual attack selection for a device
// =============================================================================

void drawAttackMenu(int deviceIdx) {
    if (deviceIdx < 0 || deviceIdx >= LokiRecon::getDeviceCount()) return;

    tft.fillScreen(LOKI_BG_DARK);
    LokiDevice& dev = LokiRecon::getDevices()[deviceIdx];
    detailDevIdx = deviceIdx;

    char ipStr[16];
    snprintf(ipStr, sizeof(ipStr), "%d.%d.%d.%d", dev.ip[0], dev.ip[1], dev.ip[2], dev.ip[3]);

    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(LOKI_MAGENTA);
    tft.setTextSize(2);
    tft.drawString("ATTACK", SCREEN_WIDTH / 2, 15);
    tft.setTextSize(1);
    tft.setTextColor(LOKI_TEXT);
    tft.drawString(ipStr, SCREEN_WIDTH / 2, 35);
    tft.setTextDatum(TL_DATUM);

    // Available attacks based on open ports
    int y = 55;
    int btnH = 28;
    int attackNum = 0;

    struct AttackOption {
        const char* label;
        uint16_t color;
        bool available;
    };

    AttackOption attacks[] = {
        {"Brute FTP",    LOKI_CYAN,    (bool)(dev.openPorts & PORT_FTP)},
        {"Brute SSH",    LOKI_CYAN,    (bool)(dev.openPorts & PORT_SSH)},
        {"Brute Telnet", LOKI_CYAN,    (bool)(dev.openPorts & PORT_TELNET)},
        {"Brute HTTP",   LOKI_CYAN,    (bool)(dev.openPorts & (PORT_HTTP | PORT_HTTP2))},
        {"Brute SMB",    LOKI_CYAN,    (bool)(dev.openPorts & PORT_SMB)},
        {"Brute MySQL",  LOKI_CYAN,    (bool)(dev.openPorts & PORT_MYSQL)},
        {"Brute RDP",    LOKI_CYAN,    (bool)(dev.openPorts & PORT_RDP)},
    };

    for (int i = 0; i < 7; i++) {
        if (!attacks[i].available) continue;

        tft.fillRoundRect(10, y, SCREEN_WIDTH - 20, btnH - 2, 3, LOKI_BG_SURFACE);
        tft.drawRoundRect(10, y, SCREEN_WIDTH - 20, btnH - 2, 3, attacks[i].color);
        tft.setTextDatum(MC_DATUM);
        tft.setTextColor(attacks[i].color);
        tft.drawString(attacks[i].label, SCREEN_WIDTH / 2, y + btnH / 2 - 1);
        y += btnH + 2;
        attackNum++;
    }

    if (attackNum == 0) {
        tft.setTextColor(LOKI_TEXT_DIM);
        tft.setTextDatum(MC_DATUM);
        tft.drawString("No attacks available", SCREEN_WIDTH / 2, 80);
    }

    // Back button
    int btnY = SCREEN_HEIGHT - 30;
    tft.fillRoundRect(SCREEN_WIDTH / 2 - 40, btnY, 80, 24, 3, LOKI_BG_SURFACE);
    tft.drawRoundRect(SCREEN_WIDTH / 2 - 40, btnY, 80, 24, 3, LOKI_RED);
    tft.setTextColor(LOKI_RED);
    tft.drawString("Back", SCREEN_WIDTH / 2, btnY + 12);

    tft.setTextDatum(TL_DATUM);
}

void handleAttackMenuTouch(int x, int y) {
    // Back button at bottom
    if (y >= SCREEN_HEIGHT - 30) return; // Caller handles

    // TODO: Match touch to attack buttons and trigger individual attacks
    // For now, attacks run through the autonomous pipeline
}

// =============================================================================
// LOOT VIEWER — Cracked credentials
// =============================================================================

void drawLootView() {
    tft.fillScreen(LOKI_BG_DARK);

    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(LOKI_HOTPINK);
    tft.setTextSize(2);
    tft.drawString("CREDENTIALS", SCREEN_WIDTH / 2, 18);
    tft.setTextDatum(TL_DATUM);
    tft.setTextSize(1);

    int count = LokiRecon::getCredLogCount();
    LokiCredEntry* creds = LokiRecon::getCredLog();

    if (count == 0) {
        tft.setTextColor(LOKI_TEXT_DIM);
        tft.setCursor(5, 50);
        tft.print("No credentials cracked yet.");
    } else {
        int y = 42;
        int lineH = 14;

        for (int i = 0; i < count; i++) {
            if (y > SCREEN_HEIGHT - 50) break;

            char ipStr[16];
            snprintf(ipStr, sizeof(ipStr), "%d.%d.%d.%d",
                     creds[i].ip[0], creds[i].ip[1], creds[i].ip[2], creds[i].ip[3]);

            // IP + port
            tft.setTextColor(LOKI_CYAN);
            tft.setCursor(5, y);
            tft.printf("%s:%d", ipStr, creds[i].port);
            y += lineH;

            // Credentials
            tft.setTextColor(LOKI_HOTPINK);
            tft.setCursor(15, y);
            tft.printf("%s : %s", creds[i].user, creds[i].pass);
            y += lineH + 4;
        }
    }

    // Clear + Back buttons
    int btnY = SCREEN_HEIGHT - 30;
    int btnW = SCREEN_WIDTH / 2 - 6;

    tft.fillRoundRect(4, btnY, btnW, 24, 3, LOKI_BG_SURFACE);
    tft.drawRoundRect(4, btnY, btnW, 24, 3, LOKI_MAGENTA);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(LOKI_MAGENTA);
    tft.drawString("Clear Creds", 4 + btnW / 2, btnY + 12);

    tft.fillRoundRect(SCREEN_WIDTH / 2 + 2, btnY, btnW, 24, 3, LOKI_BG_SURFACE);
    tft.drawRoundRect(SCREEN_WIDTH / 2 + 2, btnY, btnW, 24, 3, LOKI_RED);
    tft.setTextColor(LOKI_RED);
    tft.drawString("Back", SCREEN_WIDTH / 2 + 2 + btnW / 2, btnY + 12);
    tft.setTextDatum(TL_DATUM);
}

void handleLootTouch(int x, int y) {
    if (y >= SCREEN_HEIGHT - 30) {
        if (x < SCREEN_WIDTH / 2) {
            // Clear credentials
            LokiRecon::clearCredLog();
            drawLootView();
        }
        // Back handled by caller
    }
}

int getDetailDevice() { return detailDevIdx; }
void setDetailDevice(int idx) { detailDevIdx = idx; }

int getDevScroll() { return devScroll; }
void scrollDevices(int scroll) { devScroll = scroll; }

}  // namespace LokiUI
