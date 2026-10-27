#pragma once
// Cleans text for a notification before it is drawn: the screen fonts only have plain ASCII, so emoji and
// accented letters would show as empty boxes. Pure (no Arduino) so it is unit-tested on a PC.
#include <stddef.h>
#include <stdint.h>

// Copies `in` into `out` (capacity `cap`, including the final 0): keeps printable ASCII, drops everything else
// (including every byte of a multi-byte UTF-8 character), turns runs of spaces/tabs/newlines into one space,
// trims both ends and ends with "..." when it had to cut. Returns the length written.
inline size_t noteClean(const char* in, char* out, size_t cap) {
  if (!out || cap == 0) return 0;
  out[0] = 0;
  if (!in) return 0;
  size_t n = 0;
  bool pendingSpace = false, cut = false;
  for (const unsigned char* p = (const unsigned char*)in; *p; p++) {
    unsigned char c = *p;
    if (c == ' ' || c == '\t' || c == '\n' || c == '\r') { pendingSpace = n > 0; continue; }
    if (c < 0x21 || c > 0x7E) continue;
    size_t need = (pendingSpace ? 1 : 0) + 1;
    if (n + need >= cap) { cut = true; break; }
    if (pendingSpace) { out[n++] = ' '; pendingSpace = false; }
    out[n++] = (char)c;
  }
  out[n] = 0;
  if (cut && cap > 4) {
    while (n > cap - 4) n--;
    while (n > 0 && out[n - 1] == ' ') n--;
    out[n++] = '.'; out[n++] = '.'; out[n++] = '.'; out[n] = 0;
  }
  return n;
}
