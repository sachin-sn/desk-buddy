#pragma once
// Tiny assertion helper so the tests have no dependencies.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_checks = 0, g_failed = 0;

#define CHECK(cond) do { g_checks++; if (!(cond)) { g_failed++; \
  printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); } } while (0)
#define CHECK_EQ(a, b) do { g_checks++; long long _a = (long long)(a), _b = (long long)(b); \
  if (_a != _b) { g_failed++; printf("  FAIL %s:%d  %s == %s  (%lld vs %lld)\n", __FILE__, __LINE__, #a, #b, _a, _b); } } while (0)
#define CHECK_STR(a, b) do { g_checks++; if (strcmp((a), (b)) != 0) { g_failed++; \
  printf("  FAIL %s:%d  \"%s\" != \"%s\"\n", __FILE__, __LINE__, (a), (b)); } } while (0)

static int finish() {
  printf("  %d checks, %d failed\n", g_checks, g_failed);
  return g_failed ? 1 : 0;
}
