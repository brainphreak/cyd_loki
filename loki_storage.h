#ifndef LOKI_STORAGE_H
#define LOKI_STORAGE_H

#include "loki_types.h"

// =============================================================================
// Loki CYD — SPIFFS Storage
// Persists scan results, credentials, and logs to flash filesystem.
// Files are served via web UI for download.
// =============================================================================

namespace LokiStorage {

// Initialize SPIFFS filesystem
void setup();

// Save scan results to SPIFFS
void saveCredentials();
void saveDevices();
void saveAttackLog();

// Load from SPIFFS on boot
void loadCredentials();

// Get SPIFFS file list for web UI
String listFiles();

// Is SPIFFS available?
bool available();

}  // namespace LokiStorage

#endif // LOKI_STORAGE_H
