#ifndef LOKI_PET_H
#define LOKI_PET_H

#include "loki_config.h"
#include "loki_types.h"

// =============================================================================
// Loki CYD — Virtual Pet Display Module
// Tamagotchi-style UI with character animations, stats, and status
// =============================================================================

namespace LokiPet {

// Initialize the pet display (fullInit=true on first boot, false on theme switch)
void setup(bool fullInit = true);

// Main display loop (called from Core 1)
void loop();

// Set the current mood (controls character animation)
void setMood(LokiMood mood);

// Update stats on the display
void updateStats(const LokiScore& score);

// Update the status line (current action)
void setStatus(const char* main, const char* sub = nullptr);

// Add a line to the kill feed
void addKillLine(const char* text, uint16_t color);

// Show a commentary bubble
void setComment(const char* text);

// Draw the full pet screen
void drawPetScreen();

// Force bg.bmp redraw on next drawPetScreen (call after menu/overlay/theme switch)
void invalidateBackground();

// Get current mood
LokiMood getMood();

// Toggle status icon on/off
void setShowStatusIcon(bool show);
bool getShowStatusIcon();

// Kill feed / attack log access
int getKillFeedCount();
void getKillFeedLine(int idx, char* buf, int bufLen, uint16_t* color);
void clearKillFeed();

// Theme-aware kill feed colors (recon uses these instead of hardcoded constants)
uint16_t kfInfo();      // Info/status messages
uint16_t kfFound();     // Host/port discovered
uint16_t kfSuccess();   // Successful operations
uint16_t kfCracked();   // Credentials cracked
uint16_t kfDim();       // Blocked/locked/failed
uint16_t kfAttack();    // Attack start
uint16_t kfError();     // Errors
uint16_t kfXp();        // XP gains

}  // namespace LokiPet

#endif // LOKI_PET_H
