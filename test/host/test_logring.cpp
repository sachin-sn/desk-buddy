#include "check.h"
#include "logring.h"

int main() {
  static LogRing r;   // static: the real one is too, and it starts zeroed

  // empty
  CHECK_EQ(r.size(), 0);

  // order and metadata
  r.push(5, 0, "first");
  r.push(9, 1800000000, "second");
  CHECK_EQ(r.size(), 2);
  CHECK_STR(r.at(0).text, "first");
  CHECK_STR(r.at(1).text, "second");
  CHECK_EQ(r.at(0).up, 5);
  CHECK_EQ(r.at(0).epoch, 0);
  CHECK_EQ(r.at(1).epoch, 1800000000);

  // wrapping: only the newest LOG_LINES stay, oldest first
  for (int i = 0; i < LOG_LINES + 10; i++) {
    char b[24]; snprintf(b, sizeof b, "line %d", i);
    r.push((uint32_t)i, 0, b);
  }
  CHECK_EQ(r.size(), LOG_LINES);
  CHECK_STR(r.at(0).text, "line 10");                      // 2 earlier lines + 10 dropped by the wrap
  char last[24]; snprintf(last, sizeof last, "line %d", LOG_LINES + 9);
  CHECK_STR(r.at(LOG_LINES - 1).text, last);

  // long messages are cut, never overflow, and stay terminated
  LogRing r2;
  char longMsg[400]; memset(longMsg, 'x', sizeof longMsg - 1); longMsg[sizeof longMsg - 1] = 0;
  r2.push(0, 0, longMsg);
  CHECK_EQ((int)strlen(r2.at(0).text), LOG_WIDTH - 1);

  // control characters and non-ASCII cannot break the layout
  r2.push(0, 0, "a\nb\r\tc\x01 \xC3\xA9 end");
  CHECK_STR(r2.at(1).text, "a b  c     end");        // \n \r \t \x01 and the two bytes of "é" each became one space
  for (const char* p = r2.at(1).text; *p; p++) CHECK((unsigned char)*p >= ' ' && (unsigned char)*p <= '~');

  // null and empty messages are safe
  r2.push(0, 0, nullptr);
  CHECK_STR(r2.at(2).text, "");
  r2.push(0, 0, "");
  CHECK_STR(r2.at(3).text, "");

  return finish();
}
