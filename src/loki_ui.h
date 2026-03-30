#ifndef LOKI_UI_H
#define LOKI_UI_H

#include "loki_config.h"
#include "loki_types.h"

// =============================================================================
// Loki CYD — UI Screens (WiFi picker, keyboard, device list, loot, attacks)
// =============================================================================

namespace LokiUI {

// Initialize UI system
void setup();

// WiFi picker — returns requested screen change (or current if none)
void drawWifiScan();
LokiScreen handleWifiScanTouch(int x, int y);

// On-screen keyboard — returns requested screen change
void drawKeyboard(const char* prompt, int maxLen);
LokiScreen handleKeyboardTouch(int x, int y, int maxLen);
String getKeyboardInput();
void clearKeyboardInput();

// Device list
void drawDeviceList();
void handleDeviceListTouch(int x, int y);

// Device detail
void drawDeviceDetail(int deviceIdx);
void handleDeviceDetailTouch(int x, int y);

// Loot viewer (cracked credentials)
void drawLootView();
void handleLootTouch(int x, int y);

// Manual attack menu for a specific device
void drawAttackMenu(int deviceIdx);
void handleAttackMenuTouch(int x, int y);

// Get selected WiFi SSID and password
const char* getSelectedSSID();
const char* getSelectedPass();
bool hasWifiSelection();

// Current detail device index
int getDetailDevice();
void setDetailDevice(int idx);

}  // namespace LokiUI

#endif // LOKI_UI_H
