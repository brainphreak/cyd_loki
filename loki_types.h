#ifndef LOKI_TYPES_H
#define LOKI_TYPES_H

#include <Arduino.h>
#include "loki_config.h"

// =============================================================================
// Loki CYD — Shared Type Definitions
// =============================================================================

// --- Device types found on the network ---
enum LokiDeviceType : uint8_t {
    DEV_UNKNOWN = 0,
    DEV_PHONE,          // Apple, Samsung, etc.
    DEV_LAPTOP,         // Intel, Dell, Lenovo, etc.
    DEV_ROUTER,         // Cisco, Netgear, TP-Link, etc.
    DEV_CAMERA,         // Hikvision, Dahua, etc.
    DEV_NAS,            // Synology, QNAP, etc.
    DEV_PRINTER,        // Brother, Canon, etc.
    DEV_TV_MEDIA,       // Roku, LG, etc.
    DEV_IOT,            // Espressif, Shelly, Tuya, etc.
    DEV_SPEAKER,        // Sonos, Amazon Echo, etc.
    DEV_GAMING,         // Nintendo, PlayStation, etc.
    DEV_SERVER,         // Linux/Windows server
    DEV_VM,             // VMware, etc.
    DEV_OTHER           // Identified vendor but unknown category
};

// --- Device attack status ---
enum LokiDeviceStatus : uint8_t {
    STATUS_FOUND = 0,
    STATUS_SCANNING,
    STATUS_TESTING,
    STATUS_CRACKED,
    STATUS_OPEN,
    STATUS_LOCKED
};

// --- Scan pipeline phases ---
enum LokiScanPhase : uint8_t {
    PHASE_IDLE = 0,
    PHASE_WIFI_CONNECT,
    PHASE_DISCOVER,
    PHASE_IDENTIFY,
    PHASE_ATTACK,
    PHASE_DONE
};

// --- Pet mood / emotional state ---
enum LokiMood : uint8_t {
    MOOD_IDLE = 0,       // No targets, waiting
    MOOD_SCANNING,       // Actively scanning network
    MOOD_ATTACKING,      // Brute forcing / exploiting
    MOOD_CRACKED,        // Just cracked something! (brief celebration)
    MOOD_STEALING,       // Exfiltrating files
    MOOD_SLEEPING,       // Idle timeout, low activity
    MOOD_HAPPY,          // High score, lots of cracks
    MOOD_BORED           // No new targets found
};

// --- Discovered device record ---
struct LokiDevice {
    uint8_t  ip[4];
    uint8_t  mac[6];                             // MAC address from ARP
    uint16_t openPorts;                          // Bitmask (9 ports)
    LokiDeviceType  type;
    LokiDeviceStatus status;
    char     banner[LOKI_MAX_BANNER_LEN];
    char     vendor[16];                         // Vendor name from OUI lookup
    char     credUser[LOKI_MAX_CRED_USER];       // Last cracked cred (for display)
    char     credPass[LOKI_MAX_CRED_PASS];
    uint8_t  crackedPorts;                       // Bitmask of which ports were cracked
};

// --- Credential log entry (stores ALL cracked creds) ---
#define LOKI_MAX_CREDS 64
struct LokiCredEntry {
    uint8_t  ip[4];
    uint16_t port;
    char     user[LOKI_MAX_CRED_USER];
    char     pass[LOKI_MAX_CRED_PASS];
};

// --- Persistent score (saved to NVS) ---
struct LokiScore {
    uint32_t hostsFound;
    uint32_t portsFound;
    uint32_t servicesCracked;
    uint32_t filesStolen;
    uint32_t vulnsFound;
    uint32_t totalScans;
    uint32_t xp;                // Cumulative experience points
};

// --- Kill feed line ---
struct LokiKillLine {
    char text[52];
    uint16_t color;
};

// --- UI screen states ---
enum LokiScreen : uint8_t {
    SCREEN_PET = 0,          // Main pet view (default)
    SCREEN_WIFI_SCAN,        // WiFi network picker
    SCREEN_WIFI_KEYBOARD,    // Password entry
    SCREEN_WIFI_CONNECTING,  // Connection progress
    SCREEN_MENU,             // Pause / settings menu
    SCREEN_KILL_FEED,        // Scrollable attack log
    SCREEN_DEVICE_LIST,      // Discovered hosts
    SCREEN_DEVICE_DETAIL,    // Single device info
    SCREEN_STATS,            // Score / stats view
    SCREEN_LOOT              // Cracked credentials
};

#endif // LOKI_TYPES_H
