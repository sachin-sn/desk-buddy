#pragma once
#include <Arduino.h>
#include "weather.h"

// Fetches the current conditions for the saved location from Open-Meteo.
// Core 0 only: this does a TLS handshake and can take a few seconds.
bool weatherFetch(Weather& out, String& err);
