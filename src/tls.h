#pragma once
// HTTPS settings shared by the calendar and weather fetches.
#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

#ifndef BUDDY_TLS_INSECURE
// Certificate bundle embedded in the Arduino-ESP32 core (Mozilla roots). If your build lacks it,
// the link step fails on this symbol: add -DBUDDY_TLS_INSECURE to build_flags (see README).
extern const uint8_t rootca_crt_bundle_start[] asm("_binary_x509_crt_bundle_start");
#endif

inline void tlsConfigure(WiFiClientSecure& client) {
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
