#include "photostore.h"
#include <LittleFS.h>
#include <esp_heap_caps.h>
#include "log.h"

const char* const PHOTO_PATH = "/photo.bin";
static const char* PHOTO_TMP = "/photo.tmp";

static uint8_t*    buf[2] = {nullptr, nullptr};
static int         active = -1;               // which buffer the UI should draw, -1 = no photo
static uint32_t    ver = 0, seen = 0;
static size_t      fill = 0;
static bool        uploading = false, overflow = false;
static int         target = 0;                // buffer being filled by an upload
static portMUX_TYPE pm = portMUX_INITIALIZER_UNLOCKED;

// PSRAM only: 115 KB of internal RAM would starve WiFi and TLS.
static bool ensure(int i) {
  if (buf[i]) return true;
  buf[i] = (uint8_t*)heap_caps_malloc(PHOTO_FILE_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  return buf[i] != nullptr;
}

static void publish(int which) {            // which = -1 removes the photo
  portENTER_CRITICAL(&pm);
  active = which;
  ver++;
  portEXIT_CRITICAL(&pm);
}

PhotoRef photoCurrent() {
  PhotoRef r;
  portENTER_CRITICAL(&pm);
  r.px = active >= 0 ? (const uint16_t*)(buf[active] + PHOTO_HDR) : nullptr;
  r.ver = ver;
  portEXIT_CRITICAL(&pm);
  return r;
}

bool photoPresent() { return active >= 0; }
void photoSeen(uint32_t v) { seen = v; }

void photoInit() {
  if (!LittleFS.exists(PHOTO_PATH)) return;
  File f = LittleFS.open(PHOTO_PATH, "r");
  if (!f) return;
  size_t n = f.size();
  if (n != PHOTO_FILE_BYTES || !ensure(0)) {
    logEvent("photo: the saved photo cannot be used (%u bytes) - upload it again", (unsigned)n);
    f.close();
    return;
  }
  size_t got = f.read(buf[0], PHOTO_FILE_BYTES);
  f.close();
  if (!photoHeaderOk(buf[0], got)) { logEvent("photo: the saved file is damaged - upload it again"); return; }
  publish(0);
  logEvent("photo: loaded");
}

bool photoUploadBegin(String& err) {
  target = active < 0 ? 0 : 1 - active;
  // the UI may still be drawing the older buffer for a moment after a previous upload
  for (int waited = 0; active >= 0 && seen != ver && waited < 500; waited += 10) delay(10);
  if (!ensure(target)) { err = "not enough memory for the photo"; return false; }
  fill = 0; overflow = false; uploading = true;
  return true;
}

void photoUploadWrite(const uint8_t* data, size_t n) {
  if (!uploading || overflow) return;
  if (fill + n > PHOTO_FILE_BYTES) { overflow = true; return; }
  memcpy(buf[target] + fill, data, n);
  fill += n;
}

bool photoUploadEnd(String& err) {
  if (!uploading) { err = "no upload in progress"; return false; }
  uploading = false;
  if (overflow || !photoHeaderOk(buf[target], fill)) { err = "that is not a 240x240 photo from the editor"; return false; }
  size_t freeBytes = LittleFS.totalBytes() - LittleFS.usedBytes();
  if (freeBytes < PHOTO_FILE_BYTES + 8192) { err = "not enough storage left on the buddy"; return false; }
  File f = LittleFS.open(PHOTO_TMP, "w");
  if (!f) { err = "could not write the photo"; return false; }
  size_t w = f.write(buf[target], PHOTO_FILE_BYTES);
  f.close();
  if (w != PHOTO_FILE_BYTES) { LittleFS.remove(PHOTO_TMP); err = "could not write the photo"; return false; }
  LittleFS.remove(PHOTO_PATH);
  if (!LittleFS.rename(PHOTO_TMP, PHOTO_PATH)) { LittleFS.remove(PHOTO_TMP); err = "could not save the photo"; return false; }
  publish(target);
  logEvent("photo: saved");
  return true;
}

void photoUploadAbort() { uploading = false; }

bool photoRemove() {
  bool had = LittleFS.exists(PHOTO_PATH);
  if (had) LittleFS.remove(PHOTO_PATH);
  if (active >= 0) publish(-1);
  if (had) logEvent("photo: removed");
  return had;
}
