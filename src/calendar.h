#pragma once
#include <Arduino.h>
#include "agenda.h"

// Fetches the next events from the Google Apps Script web app (google/calendar_proxy.gs).
// Core 0 only: this does a TLS handshake and can take a few seconds.
bool calendarFetch(Agenda& out, String& err);
