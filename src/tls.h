#pragma once
// HTTPS settings shared by the calendar and weather fetches.
#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <esp_heap_caps.h>
#include <mbedtls/platform.h>
#include "log.h"

#ifndef BUDDY_TLS_INSECURE
// Certificate bundle embedded in the Arduino-ESP32 core (Mozilla roots). If your build lacks it,
// the link step fails on this symbol: add -DBUDDY_TLS_INSECURE to build_flags (see README).
extern const uint8_t rootca_crt_bundle_start[] asm("_binary_x509_crt_bundle_start");
#endif

// The Arduino-ESP32 core is built so that mbedTLS takes ALL its memory from internal RAM: two 16 KB
// record buffers plus the handshake state, each needing one contiguous block. With WiFi, Bluetooth
// and the UI running, the largest free internal block can drop below that ("tls SSL - Memory
// allocation failed"). The core's library cannot be rebuilt from here, but mbedTLS lets us swap its
// allocator at run time, so the big handshake buffers come from the 8 MB PSRAM instead. If PSRAM is
// full or absent, it falls back to internal RAM. Add -DBUDDY_TLS_INTERNAL_RAM to turn this off.
#ifndef BUDDY_TLS_INTERNAL_RAM
inline void* tlsCalloc(size_t n, size_t size) {
  void* p = heap_caps_calloc(n, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!p) p = heap_caps_calloc(n, size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  return p;
}
inline void tlsFree(void* p) { heap_caps_free(p); }   // frees PSRAM and internal blocks alike
#endif

inline void tlsUsePsram() {
#ifndef BUDDY_TLS_INTERNAL_RAM
  static bool done = false;
  if (done) return;
  done = true;
  if (heap_caps_get_total_size(MALLOC_CAP_SPIRAM) == 0) { logEvent("tls: no PSRAM found, using internal RAM"); return; }
  mbedtls_platform_set_calloc_free(tlsCalloc, tlsFree);
  logEvent("tls: handshake memory comes from PSRAM");
#endif
}

inline void tlsConfigure(WiFiClientSecure& client) {
  tlsUsePsram();
#ifdef BUDDY_TLS_INSECURE
  client.setInsecure();
#else
  client.setCACertBundle(rootca_crt_bundle_start);
#endif
}

// HTTPClient reports -1 for every connect failure. The TLS layer knows whether it was the certificate,
// memory or the network, and the largest free block shows whether the handshake could even be allocated.
inline String connectFailText(int code, WiFiClientSecure& client) {
  char tls[48] = "";
  int le = client.lastError(tls, sizeof tls);
  String s = String("connect failed: ") + HTTPClient::errorToString(code);
  if (le) s += String(" / tls ") + tls;
  s += String(" / free ") + (ESP.getMaxAllocHeap() / 1024) + "K";
  return s;
}
