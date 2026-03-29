#ifndef LOKI_RECON_H
#define LOKI_RECON_H

#include "loki_config.h"
#include "loki_types.h"

// =============================================================================
// Loki CYD — Autonomous Recon Engine
// Network scanning, brute force, and attack orchestration
// Runs on Core 0, reports to pet UI on Core 1
// =============================================================================

namespace LokiRecon {

// Initialize the recon engine
void setup();

// Start autonomous scanning (launches Core 0 task)
void start();

// Stop scanning
void stop();

// Is the recon engine currently running?
bool isRunning();

// Is the scan complete?
bool isDone();

// Get current scan phase
LokiScanPhase getPhase();

// Get device list
LokiDevice* getDevices();
int getDeviceCount();

// Get credential log (all cracked creds, not just last per host)
LokiCredEntry* getCredLog();
int getCredLogCount();

// Clear data
void clearDevices();
void clearCredLog();

// Get live stats
LokiScore getStats();

// Set WiFi credentials for connection
void setWiFi(const char* ssid, const char* pass);

// Cleanup
void cleanup();

}  // namespace LokiRecon

#endif // LOKI_RECON_H
