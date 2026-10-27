#include <stdio.h>
#include <string.h>
#include "../../src/notetext.h"
static int fails = 0, total = 0;
#define CHECK(c) do { total++; if (!(c)) { fails++; printf("FAIL line %d: %s\n", __LINE__, #c); } } while (0)
static bool is(const char* in, size_t cap, const char* want) { char b[64]; noteClean(in, b, cap); if (strcmp(b, want)) printf("  got '%s' want '%s'\n", b, want); return !strcmp(b, want); }
int main() {
  CHECK(is("Hello", 32, "Hello"));
  CHECK(is("  Hello   world \n", 32, "Hello world"));
  CHECK(is("Caf\xC3\xA9 \xF0\x9F\x98\x80 ok", 32, "Caf ok"));              // accents and emoji removed, no double space
  CHECK(is("\xF0\x9F\x98\x80", 32, ""));
  CHECK(is("", 32, ""));
  CHECK(is("a\tb", 32, "a b"));
  CHECK(is("abcdefghijklmnop", 10, "abcdef..."));                           // cut with an ellipsis, fits the buffer
  CHECK(is("abcdefghi", 10, "abcdefghi"));                                  // exactly fits, no cut
  CHECK(is("abcdefghij", 10, "abcdef..."));
  CHECK(is("ab cd ef gh ij kl", 10, "ab cd..."));                          // no dangling space before the dots
  char b[4]; CHECK(noteClean("abcdef", b, 4) == 3 && !strcmp(b, "abc"));    // tiny buffer: plain cut
  CHECK(noteClean(nullptr, b, 4) == 0 && b[0] == 0);
  CHECK(noteClean("x", nullptr, 4) == 0);
  CHECK(noteClean("x", b, 0) == 0);
  printf("%d checks, %d failed\n", total, fails);
  return fails ? 1 : 0;
}
