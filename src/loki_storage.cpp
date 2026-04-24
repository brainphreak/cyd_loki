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
#include "loki_sprites.h"
#include "loki_pet.h"
#include <SPIFFS.h>
#include <SD.h>
#include <SPI.h>
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

static void writeCredsToFile(File& f) {
    int count = LokiRecon::getCredLogCount();
    LokiCredEntry* creds = LokiRecon::getCredLog();
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
}

void saveCredentials() {
    int count = LokiRecon::getCredLogCount();

    // Save to SD card if available
    if (LokiSprites::sdAvailable()) {
        if (LokiSprites::sdMount()) {
            File f = SD.open("/loki/loot/credentials.json", FILE_WRITE);
            if (f) { writeCredsToFile(f); f.close(); }
            LokiSprites::sdUnmount();
            Serial.printf("[STORAGE] Saved %d creds to SD\n", count);
        }
    }

    // Also save to SPIFFS as backup
    if (spiffsReady) {
        File f = SPIFFS.open("/credentials.json", FILE_WRITE);
        if (f) { writeCredsToFile(f); f.close(); }
    }
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

static bool restoreCredsFromJson(const String& content) {
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, content);
    if (err) {
        Serial.printf("[STORAGE] JSON parse error: %s\n", err.c_str());
        return false;
    }

    JsonArray arr = doc.as<JsonArray>();
    int loaded = 0;
    for (JsonObject obj : arr) {
        const char* ip = obj["ip"];
        int port = obj["port"];
        const char* user = obj["user"];
        const char* pass = obj["pass"];

        if (ip && user && pass) {
            uint8_t ipBytes[4];
            sscanf(ip, "%hhu.%hhu.%hhu.%hhu",
                   &ipBytes[0], &ipBytes[1], &ipBytes[2], &ipBytes[3]);
            LokiRecon::restoreCredential(ipBytes, port, user, pass);
            loaded++;
        }
    }
    if (loaded > 0) Serial.printf("[STORAGE] Restored %d credentials\n", loaded);
    return loaded > 0;
}

void loadCredentials() {
    // Try SD card first
    if (LokiSprites::sdAvailable()) {
        String content;
        bool loaded = false;
        if (LokiSprites::sdMount()) {
            File f = SD.open("/loki/loot/credentials.json", FILE_READ);
            if (f) {
                content = f.readString();
                f.close();
                loaded = true;
            }
            LokiSprites::sdUnmount();
        }
        if (loaded && restoreCredsFromJson(content)) return;
    }

    // Fall back to SPIFFS
    if (!spiffsReady) return;
    File f = SPIFFS.open("/credentials.json", FILE_READ);
    if (!f) return;
    String content = f.readString();
    f.close();
    restoreCredsFromJson(content);
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
