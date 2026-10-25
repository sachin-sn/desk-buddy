#pragma once
// Calendar events as the device sees them, plus the small pure helpers around them.
// No Arduino dependencies (host-tested).
#include <stdint.h>
#include <stdio.h>
#include <stddef.h>
#include <string.h>

#define AGENDA_MAX  8
#define TITLE_MAX   28

struct Event {
  char     title[TITLE_MAX];
  uint32_t start;   // unix seconds
  uint32_t end;
};

struct Agenda {
  Event    ev[AGENDA_MAX];
  uint8_t  n;
  uint32_t fetched;   // unix seconds of the last successful fetch, 0 = never
  bool     valid;
};

// The display fonts only have ASCII: control bytes are dropped and every multi-byte
// character (accents, emoji) becomes a single '?'.
inline void agendaSanitize(const char* in, char* out, size_t n) {
  size_t o = 0;
  if (n == 0) return;
  for (const unsigned char* p = (const unsigned char*)in; *p && o + 1 < n; p++) {
    if (*p >= 0x20 && *p < 0x7F) out[o++] = (char)*p;
    else if (*p >= 0xC0) out[o++] = '?';       // lead byte; its continuation bytes (0x80-0xBF) are skipped
  }
  out[o] = 0;
}

inline void agendaSort(Agenda& a) {
  for (int i = 1; i < a.n; i++) {                 // insertion sort by start, n <= 8
    Event e = a.ev[i];
    int j = i - 1;
    while (j >= 0 && a.ev[j].start > e.start) { a.ev[j + 1] = a.ev[j]; j--; }
    a.ev[j + 1] = e;
  }
}

// First event that has not finished yet (ongoing or upcoming), or -1.
inline int agendaNext(const Agenda& a, uint32_t now) {
  for (int i = 0; i < a.n; i++) if (a.ev[i].end > now) return i;
  return -1;
}

// "now", "in 12m", "in 1h 05m", "in 2h", "in 3d". `delta` = seconds until the start.
inline void fmtUntil(char* out, size_t n, int32_t delta) {
  if (delta <= 0)            { snprintf(out, n, "now"); return; }
  if (delta < 3600)          { snprintf(out, n, "in %dm", (int)((delta + 59) / 60)); return; }
  if (delta < 86400) {
    int h = delta / 3600, m = (delta % 3600) / 60;
    if (m) snprintf(out, n, "in %dh %02dm", h, m); else snprintf(out, n, "in %dh", h);
    return;
  }
  snprintf(out, n, "in %dd", (int)(delta / 86400));
}

// "12m left" for something that is happening now. `delta` = seconds until the end.
inline void fmtLeft(char* out, size_t n, int32_t delta) {
  if (delta < 60) snprintf(out, n, "ending");
  else if (delta < 3600) snprintf(out, n, "%dm left", (int)((delta + 59) / 60));
  else snprintf(out, n, "%dh %02dm left", (int)(delta / 3600), (int)((delta % 3600) / 60));
}

// ---- alerts: one "soon" and one "starting" per event, never repeated ----
enum AlertKind : uint8_t { AL_NONE = 0, AL_SOON, AL_START };

struct AlertTracker {
  uint32_t soonFor = 0;    // start time of the event we already warned about
  uint32_t startFor = 0;   // start time of the event we already announced
};

// Returns an alert to raise now (at most one per call). `leadSec` = how early the warning comes.
// An event that began more than a minute ago never alerts (e.g. after a reboot mid-meeting).
inline AlertKind agendaAlert(const Agenda& a, uint32_t now, uint32_t leadSec, AlertTracker& tr, int* idxOut = nullptr) {
  for (int i = 0; i < a.n; i++) {
    const Event& e = a.ev[i];
    if (now >= e.start && now < e.start + 60 && tr.startFor != e.start) {
      tr.startFor = e.start; tr.soonFor = e.start;
      if (idxOut) *idxOut = i;
      return AL_START;
    }
    if (now + leadSec >= e.start && now < e.start && tr.soonFor != e.start) {
      tr.soonFor = e.start;
      if (idxOut) *idxOut = i;
      return AL_SOON;
    }
  }
  return AL_NONE;
}
