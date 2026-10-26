#pragma once
// The single photo for the Photo clock face (format: photo.h). The file lives in flash (LittleFS) and a
// decoded-free copy sits in PSRAM, which the UI draws straight from.
//
// Two buffers, so a new upload never changes the picture the UI is drawing: the upload fills the spare
// one, then publishes it by swapping. The UI says which version it now draws (photoSeen) and the next
// upload waits for that before it reuses the old buffer.
#include <Arduino.h>
#include "photo.h"

struct PhotoRef {
  const uint16_t* px;     // 240*240 RGB565 pixels, nullptr when there is no photo
  uint32_t        ver;    // changes whenever the picture changes (including removal)
};

// --- any core
PhotoRef photoCurrent();
bool     photoPresent();
void     photoSeen(uint32_t ver);        // UI core: "I now draw this version, the older buffer is free"

// --- core 0 only (files)
void photoInit();                                    // load the saved photo, if any
bool photoUploadBegin(String& err);
void photoUploadWrite(const uint8_t* data, size_t n);
bool photoUploadEnd(String& err);                    // checks the file, saves it, shows it
void photoUploadAbort();
bool photoRemove();
extern const char* const PHOTO_PATH;
