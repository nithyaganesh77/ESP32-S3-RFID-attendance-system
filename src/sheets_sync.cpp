#include "sheets_sync.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <LittleFS.h>
#include <vector>
#include "config.h"

static bool postLine(const String& jsonLine) {
  if (WiFi.status() != WL_CONNECTED) return false;

  WiFiClientSecure client;
  client.setInsecure(); // simplest option for an internal tool; see README for cert-pinning alternative

  HTTPClient https;
  https.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS); // don't chase the echo redirect — see note below
  if (!https.begin(client, SHEETS_WEBHOOK_URL)) return false;

  // Kept short on purpose: sheets_syncPending() runs inline in the main
  // loop(), which also handles card reads/LCD/buzzer. A stuck/slow request
  // here would freeze all of those for however long it blocks — so we'd
  // rather fail fast and retry next cycle than risk the device looking
  // "stuck" for 10-15s+ per record.
  https.setConnectTimeout(4000);
  https.setTimeout(5000);
  https.addHeader("Content-Type", "application/json");
  int code = https.POST(jsonLine);
  https.end();

  // Apps Script runs doPost() and writes the row BEFORE it replies with the
  // 302 to script.googleusercontent.com. That redirect only exists to hand
  // back the response text, which we don't need — so 302 already means the
  // row was written. Trying to follow it (via GET or POST) reliably gets a
  // 405 from Google's front end, so we simply don't bother.
  return (code == 200 || code == 302);
}

// Sends AT MOST ONE queued record per call. This runs inline in the main
// loop() (same loop that reads cards and drives the LCD), so draining an
// entire backlog of N queued scans in a single call means N sequential
// blocking HTTP round-trips before loop() can service anything else —
// that's what caused the device to "freeze" after a handful of scans piled
// up while offline/slow. Sending one record per SYNC_INTERVAL_MS tick keeps
// each call's worst-case block bounded to a single request's timeout, and
// a backlog just drains a bit slower instead of freezing the device.
void sheets_syncPending() {
  if (WiFi.status() != WL_CONNECTED) return;
  if (!LittleFS.exists(PENDING_FILE)) return;

  File f = LittleFS.open(PENDING_FILE, FILE_READ);
  if (!f) return;

  String firstLine;
  std::vector<String> rest;
  while (f.available()) {
    String line = f.readStringUntil('\n');
    line.trim();
    if (line.length() == 0) continue;
    if (firstLine.length() == 0) {
      firstLine = line;
    } else {
      rest.push_back(line);
    }
  }
  f.close();

  if (firstLine.length() == 0) return; // nothing to send

  bool sent = postLine(firstLine);
  if (!sent) rest.insert(rest.begin(), firstLine); // keep at front for retry next cycle

  // Rewrite the queue file with whatever's still left
  LittleFS.remove(PENDING_FILE);
  if (!rest.empty()) {
    File out = LittleFS.open(PENDING_FILE, FILE_WRITE);
    for (auto& l : rest) out.println(l);
    out.close();
  }
}