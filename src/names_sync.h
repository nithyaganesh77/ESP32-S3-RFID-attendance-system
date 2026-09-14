#pragma once
#include <Arduino.h>

// Downloads the "Names" sheet (UID, Name pairs) from the same Apps Script
// webhook used for scans, and caches it to LittleFS. Call periodically
// from loop(); it's a no-op if WiFi isn't connected, so it never blocks
// card reading. To add a new member: just add a row to the "Names" sheet
// in Google Sheets — no reflashing needed.
void names_syncFromSheet();

// Looks up a name for a scanned UID from the local cache.
// Returns "Unknown" if the UID isn't in the cache.
String names_lookup(const String& uid);
