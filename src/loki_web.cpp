// =============================================================================
// Loki CYD — Web UI Server (stub)
// Will serve a lightweight dashboard for monitoring and control
// =============================================================================

#include "loki_web.h"
#include "loki_config.h"
#include "loki_types.h"
#include "loki_recon.h"
#include "loki_score.h"
#include "loki_pet.h"
#include "loki_storage.h"
#include <SPIFFS.h>
#include <WiFi.h>
#include <WebServer.h>
#include <TFT_eSPI.h>

extern TFT_eSPI tft;

namespace LokiWeb {

static WebServer* server = nullptr;
static bool webRunning = false;

// =============================================================================
// ROUTES
// =============================================================================

static void handleRoot() {
    LokiScore score = LokiScoreManager::get();
    int devCount = LokiRecon::getDeviceCount();

    String html = "<!DOCTYPE html><html><head>";
    html += "<meta name='viewport' content='width=device-width,initial-scale=1'>";
    html += "<title>LOKI CYD</title>";
    html += "<style>";
    html += "body{background:#0a120a;color:#d4e8d4;font-family:monospace;margin:20px;}";
    html += "h1{color:#5ebd45;text-align:center;}";
    html += ".stat{display:flex;justify-content:space-between;padding:8px;border-bottom:1px solid #263a26;}";
    html += ".val{color:#7ed860;font-weight:bold;}";
    html += ".xp{color:#ffc800;font-size:1.5em;text-align:center;padding:15px;}";
    html += "a{color:#5ebd45;}";
    html += "button{background:#3a8a28;color:#d4e8d4;border:none;padding:10px 20px;margin:5px;cursor:pointer;font-family:monospace;}";
    html += "button:hover{background:#5ebd45;}";
    html += "</style></head><body>";
    html += "<h1>LOKI</h1>";
    html += "<div class='xp'>XP: " + String(score.xp) + "</div>";
    html += "<div class='stat'><span>Hosts Found</span><span class='val'>" + String(score.hostsFound) + "</span></div>";
    html += "<div class='stat'><span>Ports Found</span><span class='val'>" + String(score.portsFound) + "</span></div>";
    html += "<div class='stat'><span>Cracked</span><span class='val'>" + String(score.servicesCracked) + "</span></div>";
    html += "<div class='stat'><span>Files Stolen</span><span class='val'>" + String(score.filesStolen) + "</span></div>";
    html += "<div class='stat'><span>Vulns Found</span><span class='val'>" + String(score.vulnsFound) + "</span></div>";
    html += "<div class='stat'><span>Devices</span><span class='val'>" + String(devCount) + "</span></div>";
    html += "<div style='text-align:center;margin-top:20px;'>";

    if (LokiRecon::isRunning()) {
        html += "<button onclick=\"fetch('/stop').then(()=>location.reload())\">Stop Scan</button>";
    } else {
        html += "<button onclick=\"fetch('/start').then(()=>location.reload())\">Start Scan</button>";
    }

    html += "</div>";
    html += "<div style='text-align:center;margin-top:20px;'>";
    html += "<h3 style='color:#5ebd45;'>Screen</h3>";
    html += "<img src='/screenshot' style='max-width:100%;border:1px solid #263a26;' id='scr'>";
    html += "<br><button onclick=\"document.getElementById('scr').src='/screenshot?t='+Date.now()\" style='margin-top:10px;'>Refresh</button>";
    html += "</div>";
    html += "<p style='text-align:center;color:#566b56;margin-top:30px;'>Loki CYD " LOKI_VERSION "</p>";
    html += "</body></html>";

    server->send(200, "text/html", html);
}

static void handleStart() {
    if (!LokiRecon::isRunning()) {
        LokiRecon::start();
    }
    server->send(200, "text/plain", "OK");
}

static void handleStop() {
    LokiRecon::stop();
    server->send(200, "text/plain", "OK");
}

static void handleStats() {
    LokiScore score = LokiScoreManager::get();
    String json = "{";
    json += "\"hosts\":" + String(score.hostsFound) + ",";
    json += "\"ports\":" + String(score.portsFound) + ",";
    json += "\"cracked\":" + String(score.servicesCracked) + ",";
    json += "\"files\":" + String(score.filesStolen) + ",";
    json += "\"vulns\":" + String(score.vulnsFound) + ",";
    json += "\"xp\":" + String(score.xp) + ",";
    json += "\"running\":" + String(LokiRecon::isRunning() ? "true" : "false");
    json += "}";
    server->send(200, "application/json", json);
}

static void handleCreds() {
    int count = LokiRecon::getCredLogCount();
    LokiCredEntry* creds = LokiRecon::getCredLog();

    String json = "[";
    for (int i = 0; i < count; i++) {
        if (i > 0) json += ",";
        char ip[16];
        snprintf(ip, sizeof(ip), "%d.%d.%d.%d", creds[i].ip[0], creds[i].ip[1], creds[i].ip[2], creds[i].ip[3]);
        json += "{\"ip\":\"" + String(ip) + "\",\"port\":" + String(creds[i].port) +
                ",\"user\":\"" + String(creds[i].user) + "\",\"pass\":\"" + String(creds[i].pass) + "\"}";
    }
    json += "]";
    server->send(200, "application/json", json);
}

static void handleLog() {
    int count = LokiPet::getKillFeedCount();
    String text = "";
    for (int i = 0; i < count; i++) {
        char line[52];
        uint16_t color;
        LokiPet::getKillFeedLine(i, line, sizeof(line), &color);
        text += String(line) + "\n";
    }
    server->send(200, "text/plain", text);
}

static void handleFiles() {
    server->send(200, "application/json", LokiStorage::listFiles());
}

static void handleDownload() {
    if (!server->hasArg("file")) {
        server->send(400, "text/plain", "Missing ?file= parameter");
        return;
    }
    String filename = server->arg("file");
    if (!filename.startsWith("/")) filename = "/" + filename;

    if (!SPIFFS.exists(filename)) {
        server->send(404, "text/plain", "File not found");
        return;
    }

    File f = SPIFFS.open(filename, FILE_READ);
    if (!f) {
        server->send(500, "text/plain", "Failed to open file");
        return;
    }

    String contentType = "application/octet-stream";
    if (filename.endsWith(".json")) contentType = "application/json";
    else if (filename.endsWith(".txt")) contentType = "text/plain";

    server->streamFile(f, contentType);
    f.close();
}

static void handleScreenshot() {
    int w = SCREEN_WIDTH;
    int h = SCREEN_HEIGHT;

    // BMP header: 14 + 40 + 12 (BI_BITFIELDS) = 66 bytes
    int rowSize = ((w * 2 + 3) / 4) * 4;
    int dataSize = rowSize * h;
    int fileSize = 66 + dataSize;

    // Send BMP header
    uint8_t header[66] = {0};
    header[0] = 'B'; header[1] = 'M';
    *(uint32_t*)&header[2] = fileSize;
    *(uint32_t*)&header[10] = 66;  // data offset
    *(uint32_t*)&header[14] = 40;  // DIB header size
    *(int32_t*)&header[18] = w;
    *(int32_t*)&header[22] = -h;   // top-down
    *(uint16_t*)&header[26] = 1;   // planes
    *(uint16_t*)&header[28] = 16;  // bpp
    *(uint32_t*)&header[30] = 3;   // BI_BITFIELDS
    *(uint32_t*)&header[34] = dataSize;
    // Color masks
    *(uint32_t*)&header[54] = 0xF800; // R
    *(uint32_t*)&header[58] = 0x07E0; // G
    *(uint32_t*)&header[62] = 0x001F; // B

    server->setContentLength(fileSize);
    server->send(200, "image/bmp", "");

    // Send header
    server->client().write(header, 66);

    // Read screen row by row, swap bytes for BMP format, and send
    uint16_t rowBuf[w];
    for (int y = 0; y < h; y++) {
        tft.readRect(0, y, w, 1, rowBuf);
        // Swap bytes — TFT returns big-endian, BMP needs little-endian
        for (int x = 0; x < w; x++) {
            rowBuf[x] = (rowBuf[x] >> 8) | (rowBuf[x] << 8);
        }
        server->client().write((uint8_t*)rowBuf, rowSize);
    }
}

// =============================================================================
// LIFECYCLE
// =============================================================================

void setup() {
    if (server) return;

    server = new WebServer(80);
    server->on("/", handleRoot);
    server->on("/start", handleStart);
    server->on("/stop", handleStop);
    server->on("/stats", handleStats);
    server->on("/screenshot", handleScreenshot);
    server->on("/creds", handleCreds);
    server->on("/log", handleLog);
    server->on("/files", handleFiles);
    server->on("/download", handleDownload);
    server->begin();
    webRunning = true;

    Serial.printf("[WEB] Server started on http://%s/\n", WiFi.localIP().toString().c_str());
}

void loop() {
    if (server && webRunning) {
        server->handleClient();
    }
}

void stop() {
    if (server) {
        server->stop();
        delete server;
        server = nullptr;
    }
    webRunning = false;
}

bool isRunning() { return webRunning; }

}  // namespace LokiWeb
