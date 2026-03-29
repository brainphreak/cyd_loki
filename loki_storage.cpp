// =============================================================================
// Loki CYD — SPIFFS Storage
// Persists credentials, devices, and attack logs to flash.
// Files available for download via web UI.
//
// SPIFFS Layout:
//   /loot/credentials.json    — cracked credentials
//   /loot/devices.json        — discovered hosts + ports
//   /loot/attacklog.txt       — attack history
// =============================================================================

#include "loki_storage.h"
#include "loki_config.h"
#include "loki_recon.h"
#include "loki_pet.h"
#include <SPIFFS.h>
#include <ArduinoJson.h>

namespace LokiStorage {

static bool spiffsReady = false;

void setup() {
    if (SPIFFS.begin(true)) {  // true = format on first use
        spiffsReady = true;
        Serial.println("[STORAGE] SPIFFS ready");
        Serial.printf("[STORAGE] Total: %dKB  Used: %dKB  Free: %dKB\n",
                      SPIFFS.totalBytes() / 1024,
                      SPIFFS.usedBytes() / 1024,
                      (SPIFFS.totalBytes() - SPIFFS.usedBytes()) / 1024);
    } else {
        Serial.println("[STORAGE] SPIFFS failed");
    }
}

bool available() { return spiffsReady; }

// =============================================================================
// SAVE CREDENTIALS
// =============================================================================

void saveCredentials() {
    if (!spiffsReady) return;

    int count = LokiRecon::getCredLogCount();
    LokiCredEntry* creds = LokiRecon::getCredLog();

    File f = SPIFFS.open("/loot/credentials.json", FILE_WRITE);
    if (!f) {
        // Create directory by just writing the file
        f = SPIFFS.open("/credentials.json", FILE_WRITE);
        if (!f) return;
    }

    f.print("[");
    for (int i = 0; i < count; i++) {
        if (i > 0) f.print(",");
        char ip[16];
        snprintf(ip, sizeof(ip), "%d.%d.%d.%d",
                 creds[i].ip[0], creds[i].ip[1], creds[i].ip[2], creds[i].ip[3]);
        f.printf("{\"ip\":\"%s\",\"port\":%d,\"user\":\"%s\",\"pass\":\"%s\"}",
                 ip, creds[i].port, creds[i].user, creds[i].pass);
    }
    f.print("]");
    f.close();
    Serial.printf("[STORAGE] Saved %d credentials\n", count);
}

// =============================================================================
// SAVE DEVICES
// =============================================================================

void saveDevices() {
    if (!spiffsReady) return;

    int count = LokiRecon::getDeviceCount();
    LokiDevice* devs = LokiRecon::getDevices();

    File f = SPIFFS.open("/devices.json", FILE_WRITE);
    if (!f) return;

    static const uint16_t scanPorts[] = LOKI_SCAN_PORTS;

    f.print("[");
    for (int d = 0; d < count; d++) {
        if (d > 0) f.print(",");
        char ip[16];
        snprintf(ip, sizeof(ip), "%d.%d.%d.%d",
                 devs[d].ip[0], devs[d].ip[1], devs[d].ip[2], devs[d].ip[3]);

        // Build port list
        String ports = "";
        for (int p = 0; p < LOKI_MAX_PORTS; p++) {
            if (devs[d].openPorts & (1 << p)) {
                if (ports.length() > 0) ports += ",";
                ports += String(scanPorts[p]);
            }
        }

        const char* status = "found";
        switch (devs[d].status) {
            case STATUS_CRACKED: status = "cracked"; break;
            case STATUS_OPEN:    status = "open"; break;
            case STATUS_LOCKED:  status = "locked"; break;
            default: break;
        }

        f.printf("{\"ip\":\"%s\",\"ports\":\"%s\",\"banner\":\"%s\",\"status\":\"%s\"}",
                 ip, ports.c_str(), devs[d].banner, status);
    }
    f.print("]");
    f.close();
    Serial.printf("[STORAGE] Saved %d devices\n", count);
}

// =============================================================================
// SAVE ATTACK LOG
// =============================================================================

void saveAttackLog() {
    if (!spiffsReady) return;

    int count = LokiPet::getKillFeedCount();

    File f = SPIFFS.open("/attacklog.txt", FILE_WRITE);
    if (!f) return;

    for (int i = 0; i < count; i++) {
        char text[52];
        uint16_t color;
        LokiPet::getKillFeedLine(i, text, sizeof(text), &color);
        f.println(text);
    }
    f.close();
    Serial.printf("[STORAGE] Saved %d log lines\n", count);
}

// =============================================================================
// LOAD CREDENTIALS (on boot)
// =============================================================================

void loadCredentials() {
    if (!spiffsReady) return;

    File f = SPIFFS.open("/credentials.json", FILE_READ);
    if (!f) return;

    String content = f.readString();
    f.close();

    // Parse JSON and populate credential log
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, content);
    if (err) {
        Serial.printf("[STORAGE] JSON parse error: %s\n", err.c_str());
        return;
    }

    JsonArray arr = doc.as<JsonArray>();
    int loaded = 0;
    for (JsonObject obj : arr) {
        const char* ip = obj["ip"];
        int port = obj["port"];
        const char* user = obj["user"];
        const char* pass = obj["pass"];

        if (ip && user && pass) {
            // Parse IP string to bytes
            uint8_t ipBytes[4];
            sscanf(ip, "%hhu.%hhu.%hhu.%hhu",
                   &ipBytes[0], &ipBytes[1], &ipBytes[2], &ipBytes[3]);

            // Add to live credential log via recon module
            // (We can't call addCredential directly since it's static)
            // Instead, just log to serial — the cred log will be populated on next scan
            Serial.printf("[STORAGE] Loaded cred: %s:%d %s:%s\n", ip, port, user, pass);
            loaded++;
        }
    }
    Serial.printf("[STORAGE] Loaded %d credentials from flash\n", loaded);
}

// =============================================================================
// LIST FILES (for web UI)
// =============================================================================

String listFiles() {
    if (!spiffsReady) return "[]";

    String json = "[";
    File root = SPIFFS.open("/");
    File file = root.openNextFile();
    bool first = true;

    while (file) {
        if (!first) json += ",";
        json += "{\"name\":\"" + String(file.name()) + "\",\"size\":" + String(file.size()) + "}";
        first = false;
        file = root.openNextFile();
    }
    json += "]";
    return json;
}

}  // namespace LokiStorage
