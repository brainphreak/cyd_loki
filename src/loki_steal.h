#ifndef LOKI_STEAL_H
#define LOKI_STEAL_H

#include "loki_types.h"

// =============================================================================
// Loki CYD — File Steal Module
// Exfiltrates target files from cracked hosts via FTP, SSH, or Telnet.
// Saves stolen files to SD card at /loki/stolen/<ip>/
// All functions require SD card — they no-op without one.
// =============================================================================

namespace LokiSteal {

// Steal files from a cracked FTP host. Returns number of files stolen.
int stealFTP(uint8_t* ip, const char* user, const char* pass);

// Steal files from a cracked SSH host. Returns number of files stolen.
int stealSSH(uint8_t* ip, const char* user, const char* pass);

// Steal files from a cracked Telnet host. Returns number of files stolen.
int stealTelnet(uint8_t* ip, const char* user, const char* pass);

// Run all applicable steal attacks on a device (checks crackedPorts).
// Only runs on hosts with STATUS_CRACKED. Returns total files stolen.
int stealFromDevice(LokiDevice& dev);

}  // namespace LokiSteal

#endif // LOKI_STEAL_H
