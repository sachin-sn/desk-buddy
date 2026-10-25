#pragma once
// Event log shown in the web editor (Logs tab) and echoed to the serial port.
// Safe to call from any task. Keep lines short, and never log passwords, tokens or typed text.
#include "logring.h"

void logEvent(const char* fmt, ...) __attribute__((format(printf, 1, 2)));

// Copies the held lines, oldest first, into out[]; returns how many (at most max).
int logRead(LogEntry* out, int max);

// Plain-words reason for the last restart: "power-on", "CRASH (panic)", "BROWNOUT (power dip)", ...
const char* logResetReason();
