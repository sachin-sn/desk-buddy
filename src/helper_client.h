#pragma once
#include <Arduino.h>

// Talks to the Mac helper (helper/buddy-helper.js) over the local network. Core 0 only.
// The device only ever sends an action *id*; what that id does is defined on the Mac
// in actions.json, so a leaked token cannot run arbitrary commands.
bool helperConfigured();
bool helperAction(const char* id);                       // POST /action
bool helperNotify(const char* title, const char* body);  // POST /notify
bool helperLastOk();                                     // result of the most recent call
