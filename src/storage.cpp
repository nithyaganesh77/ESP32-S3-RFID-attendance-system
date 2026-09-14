#include "storage.h"
#include <LittleFS.h>
#include "config.h"

bool storage_begin() {
  return LittleFS.begin(true);
}

void storage_storeScan(const String& uid, const String& name, const String& isoTimestamp) {
  File f = LittleFS.open(PENDING_FILE, FILE_APPEND);
  if (!f) return;
  // one JSON line per scan
  String line = "{\"uid\":\"" + uid + "\",\"name\":\"" + name + "\",\"timestamp\":\"" + isoTimestamp + "\"}";
  f.println(line);
  f.close();
}

bool storage_hasPending() {
  if (!LittleFS.exists(PENDING_FILE)) return false;
  File f = LittleFS.open(PENDING_FILE, FILE_READ);
  bool has = f && f.size() > 0;
  if (f) f.close();
  return has;
}