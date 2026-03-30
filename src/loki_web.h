#ifndef LOKI_WEB_H
#define LOKI_WEB_H

// =============================================================================
// Loki CYD — Web UI Server
// Lightweight dashboard for monitoring and manual control
// =============================================================================

namespace LokiWeb {

// Start web server on port 80
void setup();

// Handle client requests (call from main loop)
void loop();

// Stop web server
void stop();

// Is web server running?
bool isRunning();

}  // namespace LokiWeb

#endif // LOKI_WEB_H
