#pragma once
#include <Arduino.h>

// Mount the local filesystem used to queue scans. Call once from setup().
bool storage_begin();

// Append one scan record (uid, resolved name, ISO timestamp) to the
// on-flash queue. This is the ONLY thing card-reading code needs to
// call — it does not touch WiFi/HTTP at all, so a scan is never lost
// even if the network is down or the sync task is busy.
void storage_storeScan(const String& uid, const String& name, const String& isoTimestamp);

// Returns true if there is at least one queued record waiting to be synced.
bool storage_hasPending();
