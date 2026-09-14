#pragma once

// Call periodically from loop(). If WiFi is connected and there are
// queued scans, it POSTs each one to the Google Sheets webhook and
// removes only the ones that succeeded — anything that fails (no
// internet, Sheets down, etc.) stays queued and is retried next call.
// This file is completely separate from storage.cpp/main.cpp's RFID
// reading logic — it only ever reads/rewrites the pending queue file.
void sheets_syncPending();
