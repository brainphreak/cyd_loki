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

// Add a credential to the log (used by storage to restore on boot)
void restoreCredential(uint8_t* ip, uint16_t port, const char* user, const char* pass);

// Clear data
void clearDevices();
void clearCredLog();

// Get live stats
LokiScore getStats();

// Set WiFi credentials for connection
void setWiFi(const char* ssid, const char* pass);

// Cleanup
void cleanup();

// =========================================================================
// Modular attack functions — callable individually or via auto-mode
// =========================================================================

// Connect to WiFi using stored ssid/pass, return success
bool connectWiFi();

// ARP scan the subnet, populate devices array, return count
int discoverHosts();

// Port scan a specific device, return open port count
int scanHostPorts(int deviceIdx);

// Banner grab + fingerprint a device
void identifyServices(int deviceIdx);

// Individual protocol attacks — return true if cracked
bool attackSSH(int deviceIdx);
bool attackFTP(int deviceIdx);
bool attackTelnet(int deviceIdx);
bool attackHTTP(int deviceIdx);
bool attackMySQL(int deviceIdx);
bool attackSMB(int deviceIdx);
bool attackRDP(int deviceIdx);

// Run all relevant attacks on a device's open ports
void attackAllPorts(int deviceIdx);

// File steal attacks — run after brute force on cracked hosts (requires SD card)
int stealFiles(int deviceIdx);

// Full autonomous pipeline calling the above in sequence
void runAutoMode();

}  // namespace LokiRecon

#endif // LOKI_RECON_H
