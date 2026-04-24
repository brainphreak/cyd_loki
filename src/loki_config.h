#ifndef LOKI_CONFIG_H
#define LOKI_CONFIG_H

// =============================================================================
// Loki CYD — Master Configuration
// Supports: ESP32-2432S028 (2.8" 320x240), QDtech E32R35T (3.5" 320x480)
// =============================================================================

// Board detection
#ifdef CYD_E32R35T
  #ifndef CYD_35
    #define CYD_35
  #endif
#endif

#if !defined(CYD_28) && !defined(CYD_35)
  #define CYD_28
#endif

// =============================================================================
// SCREEN DIMENSIONS
// =============================================================================

#ifdef CYD_28
  #define SCREEN_WIDTH   240
  #define SCREEN_HEIGHT  320
  #define TFT_BL_PIN     21
#endif

#ifdef CYD_35
  #define SCREEN_WIDTH   320
  #define SCREEN_HEIGHT  480
  #define TFT_BL_PIN     27
#endif

// Scaling macros (base: 240x320)
#define SCALE_X(x)  ((x) * SCREEN_WIDTH / 240)
#define SCALE_Y(y)  ((y) * SCREEN_HEIGHT / 320)
#define SCALE_W(w)  ((w) * SCREEN_WIDTH / 240)
#define SCALE_H(h)  ((h) * SCREEN_HEIGHT / 320)

// =============================================================================
// TOUCH CONTROLLER (XPT2046)
// =============================================================================

#define TOUCH_CS    33
#define TOUCH_IRQ   36

#ifdef CYD_28
  #define TOUCH_MOSI  32
  #define TOUCH_MISO  39
  #define TOUCH_CLK   25
#endif

// =============================================================================
// SD CARD (VSPI)
// =============================================================================

#define SD_CS        5
#define SD_SCK      18
#define SD_MOSI     23
#define SD_MISO     19

// =============================================================================
// LOKI COLOR PALETTE — Nordic/Green theme
// =============================================================================

// Primary theme colors
#define LOKI_GREEN      0x3DE9  // #3CBE28 — Primary accent
#define LOKI_BRIGHT     0x7EF6  // #7ED860 — Highlights
#define LOKI_DIM        0x3464  // #3A8A28 — Dimmed accent
#define LOKI_GOLD       0xFE60  // #FFC800 — Score/XP highlights
#define LOKI_CYAN       0x07FF  // #00FFFF — Info text
#define LOKI_MAGENTA    0xF81F  // #FF00FF — Cracked/alerts
#define LOKI_RED        0xF800  // #FF0000 — Errors/locked
#define LOKI_HOTPINK    0xFB56  // #FF6AB0 — Kill feed highlights

// Background colors
#define LOKI_BG_DARK    0x0861  // #0A120A — Main background
#define LOKI_BG_SURFACE 0x0A43  // #121A12 — Panels
#define LOKI_BG_ELEVATED 0x1264 // #1A251A — Elevated surfaces
#define LOKI_BLACK      0x0000
#define LOKI_WHITE      0xFFFF

// Text colors
#define LOKI_TEXT       0xD6B4  // #D4E8D4 — Primary text
#define LOKI_TEXT_DIM   0x4CC9  // #4E6B4E — Muted text
#define LOKI_GUNMETAL   0x18E3  // #1C1C1C

// =============================================================================
// RECON SETTINGS
// =============================================================================

#define LOKI_MAX_DEVICES     48
#define LOKI_MAX_PORTS       9
#define LOKI_MAX_BANNER_LEN  48
#define LOKI_MAX_CRED_USER   16
#define LOKI_MAX_CRED_PASS   16
#define LOKI_MAX_KILL_LINES  200

// Scan ports and bit masks
#define LOKI_SCAN_PORTS { 21, 22, 23, 80, 443, 445, 3306, 3389, 8080 }

#define PORT_FTP     (1 << 0)   // 21
#define PORT_SSH     (1 << 1)   // 22
#define PORT_TELNET  (1 << 2)   // 23
#define PORT_HTTP    (1 << 3)   // 80
#define PORT_HTTPS   (1 << 4)   // 443
#define PORT_SMB     (1 << 5)   // 445
#define PORT_MYSQL   (1 << 6)   // 3306
#define PORT_RDP     (1 << 7)   // 3389
#define PORT_HTTP2   (1 << 8)   // 8080

// =============================================================================
// FILE STEAL SETTINGS
// =============================================================================

#define STEAL_MAX_DEPTH     3
#define STEAL_MAX_FILES   100
#define STEAL_MAX_FILE_SIZE 65536
#define STEAL_TIMEOUT_MS  120000

// =============================================================================
// PET SETTINGS
// =============================================================================

// Animation frame timing — set these to match your theme's image_display_delay values
// Loki theme: min=1.5s, max=2.0s
#ifndef PET_ANIM_INTERVAL_MIN
  #define PET_ANIM_INTERVAL_MIN  1500
#endif
#ifndef PET_ANIM_INTERVAL_MAX
  #define PET_ANIM_INTERVAL_MAX  2000
#endif
#define PET_COMMENT_MIN_MS     15000 // Min time between comments
#define PET_COMMENT_MAX_MS     30000 // Max time between comments
#define PET_IDLE_TIMEOUT_MS    60000 // Go to sleep after 60s idle

// Character sprite size (will be centered in character area)
#define PET_SPRITE_W     80
#define PET_SPRITE_H     80

#endif // LOKI_CONFIG_H
