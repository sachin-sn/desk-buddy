#pragma once
// A small ring of text lines: the buddy's event log. Pure C++ (no Arduino), so it is unit-tested on a PC.
#include <stddef.h>
#include <stdint.h>

#define LOG_LINES 48     // how many lines are kept (oldest are overwritten)
#define LOG_WIDTH 112    // longest line including the terminating 0; longer messages are cut

struct LogEntry {
  uint32_t up;           // seconds since the buddy started
  uint32_t epoch;        // wall-clock time (Unix seconds) when known, else 0
  char     text[LOG_WIDTH];
};

struct LogRing {
  LogEntry line[LOG_LINES];
  uint32_t count = 0;    // lines ever added

  // Control characters and non-ASCII bytes become spaces, so a line can never break the log's layout.
  void push(uint32_t up, uint32_t epoch, const char* msg) {
    LogEntry& l = line[count % LOG_LINES];
    l.up = up;
    l.epoch = epoch;
    size_t i = 0;
    for (; msg && msg[i] && i < LOG_WIDTH - 1; i++) {
      unsigned char c = (unsigned char)msg[i];
      l.text[i] = (c < ' ' || c > '~') ? ' ' : (char)c;
    }
    l.text[i] = 0;
    count++;
  }

  int size() const { return count < LOG_LINES ? (int)count : LOG_LINES; }

  // i = 0 is the oldest line still held, size()-1 the newest.
  const LogEntry& at(int i) const {
    uint32_t first = count < LOG_LINES ? 0 : count - LOG_LINES;
    return line[(first + (uint32_t)i) % LOG_LINES];
  }
};
