/*
ESP32-S3 Zero + MFRC522 RFID/NFC Attendance Reader + I2C LCD + Buzzer
----------------------------------------------------------------------
Production build: reading + storing a scan is completely decoupled from
uploading it, and member names are looked up from a locally cached list
that refreshes itself over WiFi — no reflashing needed to add a new member.

  main.cpp        -> reads cards, drives LCD/buzzer, calls storage_storeScan()
  storage.cpp     -> only responsibility: append/read the on-flash scan queue
  sheets_sync.cpp -> only responsibility: push queued scans to Google Sheets
  names_sync.cpp  -> only responsibility: cache the UID->Name list from Sheets

To add a member: add a row (UID, Name) to the "Names" tab in Google Sheets.
The device re-downloads that list every NAME_SYNC_INTERVAL_MS.

Wiring RC522 (SPI):
  RC522    ESP32-S3 Zero
  SDA/SS -> GPIO10
  SCK    -> GPIO12
  MOSI   -> GPIO11
  MISO   -> GPIO13
  RST    -> GPIO9
  GND    -> GND
  3.3V   -> 3.3V   (NOT 5V)

Wiring I2C LCD (16x2, PCF8574 backpack):
  LCD      ESP32-S3 Zero
  SDA    -> GPIO4
  SCL    -> GPIO5
  VCC    -> 5V
  GND    -> GND

Wiring Buzzer:
  Buzzer   ESP32-S3 Zero
  Signal -> GPIO6
  GND    -> GND

Libraries (see platformio.ini):
  miguelbalboa/MFRC522
  marcoschwartz/LiquidCrystal_I2C
  bblanchon/ArduinoJson
*/

#include <SPI.h>
#include <MFRC522.h>
#include <WiFi.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <time.h>

#include "config.h"
#include "storage.h"
#include "sheets_sync.h"
#include "names_sync.h"

#define BUZZER_PIN 6                     // move to config.h if you prefer
#define NAME_SYNC_INTERVAL_MS 300000UL   // refresh member list every 5 min

MFRC522 rfid(SS_PIN, RST_PIN);
LiquidCrystal_I2C lcd(LCD_ADDR, LCD_COLS, LCD_ROWS);

unsigned long lastScanTime = 0;
String lastUID = "";
unsigned long lastSyncAttempt = 0;
unsigned long lastNameSync = 0;
bool timeSynced = false;

// --- LCD state machine: IDLE shows a live clock, FEEDBACK shows scan result ---
enum LcdMode { LCD_IDLE, LCD_FEEDBACK };
LcdMode lcdMode = LCD_IDLE;
unsigned long feedbackUntil = 0;
int lastClockMinuteShown = -1;

void connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Connecting WiFi");
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 15000) {
    delay(300);
  }
  if (WiFi.status() == WL_CONNECTED) {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("WiFi Connected");
    configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET_SEC, NTP_SERVER);
  } else {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("WiFi offline");
  }
  delay(800);
}

// Returns "YYYY-MM-DD HH:MM:SS" if time is synced, otherwise falls back to
// an uptime-based placeholder so a scan is never dropped just because NTP
// hasn't synced yet.
String getTimestamp() {
  struct tm t;
  if (getLocalTime(&t, 200)) {
    timeSynced = true;
    char buf[25];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &t);
    return String(buf);
  }
  return "uptime_ms:" + String(millis());
}

String getUID() {
  String uid = "";
  for (byte i = 0; i < rfid.uid.size; i++) {
    if (rfid.uid.uidByte[i] < 0x10) uid += "0";
    uid += String(rfid.uid.uidByte[i], HEX);
  }
  uid.toUpperCase();
  return uid;
}

// Active buzzer (no pitch control) -> known/unknown are distinguished by
// beep COUNT: 1 beep = known member, 2 beeps = unknown card.
void beep(int times, int onMs = 100, int offMs = 100) {
  for (int i = 0; i < times; i++) {
    digitalWrite(BUZZER_PIN, HIGH);
    delay(onMs);
    digitalWrite(BUZZER_PIN, LOW);
    if (i < times - 1) delay(offMs);
  }
}

// Refreshes just the clock line, without a full lcd.clear() (avoids flicker).
// No-ops if time isn't synced yet, or if the minute hasn't changed.
void showIdleClock(bool force = false) {
  struct tm t;
  if (!getLocalTime(&t, 50)) return;
  if (!force && t.tm_min == lastClockMinuteShown) return;
  lastClockMinuteShown = t.tm_min;
  char buf[17];
  strftime(buf, sizeof(buf), "%I:%M %p", &t); // 12-hour with AM/PM, e.g. 09:57 PM
  lcd.setCursor(0, 1);
  lcd.print("                "); // clear the line first
  lcd.setCursor(0, 1);
  lcd.print(buf);
}

void enterIdle() {
  lcdMode = LCD_IDLE;
  lastClockMinuteShown = -1;
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Scan card...");
  showIdleClock(true);
}

void showFeedback(const String& line0, const String& line1, unsigned long durationMs) {
  lcdMode = LCD_FEEDBACK;
  feedbackUntil = millis() + durationMs;
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(line0);
  lcd.setCursor(0, 1);
  lcd.print(line1);
}

void setup() {
  SPI.begin(SCK_PIN, MISO_PIN, MOSI_PIN, SS_PIN);
  rfid.PCD_Init();

  Wire.begin(I2C_SDA, I2C_SCL);
  lcd.init();
  lcd.backlight();

  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);

  storage_begin();
  connectWiFi();
  names_syncFromSheet(); // pull the member list once at boot, if WiFi is up
  lastNameSync = millis();

  enterIdle();
}

void loop() {
  // --- Reconnect WiFi in background if it drops (non-blocking) ---
  if (WiFi.status() != WL_CONNECTED) {
    static unsigned long lastReconnectTry = 0;
    if (millis() - lastReconnectTry > 5000) {
      lastReconnectTry = millis();
      WiFi.disconnect();
      WiFi.begin(WIFI_SSID, WIFI_PASS);
    }
  }

  // --- Read + store: never blocked by network state ---
  if (rfid.PICC_IsNewCardPresent() && rfid.PICC_ReadCardSerial()) {
    String uid = getUID();
    unsigned long now = millis();

    if (!(uid == lastUID && (now - lastScanTime) < SCAN_COOLDOWN_MS)) {
      String name = names_lookup(uid);
      bool known = (name != "Unknown");

      String ts = getTimestamp();
      storage_storeScan(uid, name, ts); // <-- only this. No HTTP call here at all.

      beep(known ? 1 : 2);
      showFeedback(known ? "Welcome!" : "Unknown card", known ? name : uid, 1500);

      lastUID = uid;
      lastScanTime = now;
    }

    rfid.PICC_HaltA();
    rfid.PCD_StopCrypto1();
  }

  // --- Drop back to the idle clock once the feedback message has shown long enough ---
  if (lcdMode == LCD_FEEDBACK && millis() > feedbackUntil) {
    enterIdle();
  }

  // --- Keep the idle clock ticking (no-op while a feedback message is showing) ---
  if (lcdMode == LCD_IDLE) {
    showIdleClock();
  }

  // --- Sync scans: entirely separate concern, on its own timer ---
  if (millis() - lastSyncAttempt > SYNC_INTERVAL_MS) {
    lastSyncAttempt = millis();
    sheets_syncPending();
  }

  // --- Refresh the member-name cache periodically (new members show up
  //     automatically once added to the "Names" sheet — no reflash) ---
  if (millis() - lastNameSync > NAME_SYNC_INTERVAL_MS) {
    lastNameSync = millis();
    names_syncFromSheet();
  }
}