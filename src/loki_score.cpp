// =============================================================================
// Loki CYD — Scoring & Persistence System
// Saves scores to NVS flash so they survive reboots
// =============================================================================

#include "loki_score.h"
#include "loki_config.h"
#include "loki_types.h"
#include <Preferences.h>

namespace LokiScoreManager {

static LokiScore score = {0};
static Preferences prefs;

void load() {
    prefs.begin("loki", true);  // read-only
    score.hostsFound      = prefs.getUInt("hosts", 0);
    score.portsFound      = prefs.getUInt("ports", 0);
    score.servicesCracked = prefs.getUInt("cracked", 0);
    score.filesStolen     = prefs.getUInt("files", 0);
    score.vulnsFound      = prefs.getUInt("vulns", 0);
    score.totalScans      = prefs.getUInt("scans", 0);
    score.xp              = prefs.getUInt("xp", 0);
    prefs.end();
}

void save() {
    prefs.begin("loki", false);  // read-write
    prefs.putUInt("hosts",   score.hostsFound);
    prefs.putUInt("ports",   score.portsFound);
    prefs.putUInt("cracked", score.servicesCracked);
    prefs.putUInt("files",   score.filesStolen);
    prefs.putUInt("vulns",   score.vulnsFound);
    prefs.putUInt("scans",   score.totalScans);
    prefs.putUInt("xp",      score.xp);
    prefs.end();
}

void reset() {
    memset(&score, 0, sizeof(LokiScore));
    prefs.begin("loki", false);
    prefs.clear();
    prefs.end();
}

LokiScore get() { return score; }

void addHostFound()       { score.hostsFound++;      score.xp += 10;  }
void addPortFound()       { score.portsFound++;      score.xp += 2;   }
void addServiceCracked()  { score.servicesCracked++; score.xp += 100; }
void addFileStolen()      { score.filesStolen++;     score.xp += 50;  }
void addVulnFound()       { score.vulnsFound++;      score.xp += 25;  }
void addScanCompleted()   { score.totalScans++;      score.xp += 5;   }

uint32_t calculateXP(const LokiScore& s) {
    return s.hostsFound * 10 + s.portsFound * 2 + s.servicesCracked * 100
         + s.filesStolen * 50 + s.vulnsFound * 25 + s.totalScans * 5;
}

LokiMood suggestMood() {
    if (score.servicesCracked > 0) return MOOD_HAPPY;
    if (score.hostsFound > 0) return MOOD_SCANNING;
    return MOOD_IDLE;
}

}  // namespace LokiScoreManager
