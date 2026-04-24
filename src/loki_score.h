#ifndef LOKI_SCORE_H
#define LOKI_SCORE_H

#include "loki_types.h"

// =============================================================================
// Loki CYD — Scoring & Persistence System
// Tracks XP, stats, and pet mood. Persists to NVS flash.
// =============================================================================

namespace LokiScoreManager {

// Load scores from NVS
void load();

// Save scores to NVS
void save();

// Reset all scores
void reset();

// Get current scores
LokiScore get();

// Add points for events
void addHostFound();
void addPortFound();
void addServiceCracked();
void addFileStolen();
void addVulnFound();
void addAttackCompleted();

// Calculate XP from score components
uint32_t calculateXP(const LokiScore& score);

// Get pet mood based on recent activity
LokiMood suggestMood();

}  // namespace LokiScoreManager

#endif // LOKI_SCORE_H
