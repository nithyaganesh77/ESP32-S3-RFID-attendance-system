#include "names_sync.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include "config.h"

void names_syncFromSheet() {
  if (WiFi.status() != WL_CONNECTED) return;

  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient https;
  // doGet responses are plain JSON with no POST body involved, so the
  // normal automatic redirect-follow works fine here (unlike doPost).
  https.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  if (!https.begin(client, SHEETS_WEBHOOK_URL)) return;

  https.setConnectTimeout(10000);
  https.setTimeout(15000);
  int code = https.GET();
  if (code != 200) {
    https.end();
    return; // keep whatever cache we already have
  }
  String body = https.getString();
  https.end();

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, body);
  if (err) return;

  File f = LittleFS.open(NAMES_FILE, FILE_WRITE); // overwrite with fresh list
  if (!f) return;

  JsonArray names = doc["names"].as<JsonArray>();
  for (JsonObject entry : names) {
    String uid = entry["uid"].as<String>();
    String name = entry["name"].as<String>();
    uid.toUpperCase();
    f.println(uid + "," + name);
  }
  f.close();
}

String names_lookup(const String& uid) {
  if (!LittleFS.exists(NAMES_FILE)) return "Unknown";
  File f = LittleFS.open(NAMES_FILE, FILE_READ);
  if (!f) return "Unknown";

  String result = "Unknown";
  while (f.available()) {
    String line = f.readStringUntil('\n');
    line.trim();
    if (line.length() == 0) continue;
    int comma = line.indexOf(',');
    if (comma < 0) continue;
    if (line.substring(0, comma) == uid) {
      result = line.substring(comma + 1);
      break;
    }
  }
  f.close();
  return result;
}