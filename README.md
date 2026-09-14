# RFID Attendance Reader

An ESP32-S3 attendance terminal. Tap an RFID card and the device logs name, in-time, out-time and duration straight to a Google Sheet — no manual entry, no PC in the loop.

Scans are written to on-device flash the instant a card is read, so nothing is lost when Wi-Fi drops. A background task drains that queue to Google Sheets and retries until it succeeds.

```
RFID card  →  MFRC522  →  ESP32-S3  →  LittleFS queue  →  Google Sheets
                              ↓
                     16x2 LCD + buzzer
```

---

## What it does

## What it does
A standalone attendance device: people tap an RFID/NFC card, and the system
logs their name, in-time, out-time, and hours stayed to a Google Sheet —
automatically, with no manual entry.

## Hardware
- **ESP32-S3 Zero** (main controller)
- **MFRC522** RFID/NFC reader module
- **16x2 I2C LCD** (PCF8574 backpack)
- **Active buzzer**
- **TP4056 module** + single-cell battery (for portable/battery power)

### Wiring
**RC522 (SPI)**
| RC522 | ESP32-S3 Zero |
|---|---|
| SDA/SS | GPIO10 |
| SCK | GPIO12 |
| MOSI | GPIO11 |
| MISO | GPIO13 |
| RST | GPIO9 |
| 3.3V | 3.3V (not 5V) |
| GND | GND |

**LCD (I2C)**
| LCD | ESP32-S3 Zero |
|---|---|
| SDA | GPIO4 |
| SCL | GPIO5 |
| VCC | 5V |
| GND | GND |

**Buzzer:** signal → GPIO6, other leg → GND

**Battery (via TP4056):** battery → TP4056 B+/B−; TP4056 OUT+/OUT− → ESP32's
5V/GND pads. USB into the TP4056's own port charges the battery — separate
from the ESP32's own USB-C port.

## How it behaves
- Tap a **known** card → LCD shows "Welcome, [Name]", buzzer beeps **once**.
- Tap an **unknown** card → LCD shows "Unknown card [UID]", buzzer beeps **twice**.
- When idle, the LCD shows a live clock.
- A scan is saved to the device's flash storage immediately — it is never
  lost even if WiFi is down at that moment.
- A background task uploads queued scans to Google Sheets whenever WiFi is
  available, retrying automatically until it succeeds.
- Member names are looked up from a list cached on the device, which
  re-downloads from Google Sheets every 5 minutes automatically.

## Software architecture (ESP32 side)
| File | Responsibility |
|---|---|
| `main.cpp` | Reads cards, drives LCD/buzzer, calls into the other modules |
| `storage.cpp/.h` | Appends scans to an on-flash queue file; nothing else |
| `sheets_sync.cpp/.h` | Uploads queued scans to Google Sheets, one at a time per cycle |
| `names_sync.cpp/.h` | Downloads and caches the UID→Name list from Google Sheets |
| `config.h` | WiFi credentials, webhook URL, pin numbers, tunable settings (git-ignored) |
| `platformio.ini` | Board/library configuration |

## Google Sheets side (`Code_RFID.gs`)
Deployed as an Apps Script Web App with two entry points:
- **`doPost`** — receives each scan from the device, writes/updates a row in
  the **"Scans"** tab (Name, Date, In Time, Out Time, Duration).
- **`doGet`** — the device polls this to refresh its Names cache, reading
  from the **"Names"** tab (UID, Name).

### "Scans" tab logic
- 1st scan of the day for a person → new row, Date + In Time filled.
- 2nd scan that day → fills Out Time and computes Duration on that same row.
- 3rd scan → starts a new row (a second in/out pair that day), and so on.
- Any repeat scan within **5 minutes** of that person's last scan is ignored
  (treated as one tap, in case someone taps 2–3 times by accident).
- Two helper columns (`_InEpoch`, `_OutEpoch`) store raw timestamps used
  internally for duration math and the 5-minute check — safe to hide in the
  sheet UI, but don't delete them.

## To add a new member
No reflashing required — just add a row to the **"Names"** tab in the
Google Sheet: `UID, Name`. The device picks it up automatically within 5
minutes (or immediately on next power-up).

To find a card's UID: tap it once — it'll log to the Scans sheet as
"Unknown" under that UID, which you can then copy into the Names tab.

## Setup

**1. Clone and create your config**

```
git clone https://github.com/nithyaganesh77/esp32-rfid-attendance.git
cd esp32-rfid-attendance
cp include/config.h.example include/config.h
```

`include/config.h` is git-ignored — your credentials stay local.

**2. Google Sheet**

Create a spreadsheet with two tabs, `Scans` and `Names`. Go to Extensions →
Apps Script, paste in `google-apps-script/Code_RFID.gs`, then Deploy → New
deployment → Web app, executing as **Me** with access set to **Anyone**.
Copy the resulting `/exec` URL.

> Access "Anyone" means anyone holding that URL can write to your sheet.
> Treat the URL as a password: keep it in `config.h`, and redeploy to get a
> fresh URL if it ever leaks.

**3. Fill in config.h**

```c
#define WIFI_SSID  "YOUR_WIFI_SSID"
#define WIFI_PASS  "YOUR_WIFI_PASSWORD"
#define SHEETS_WEBHOOK_URL "https://script.google.com/macros/s/YOUR_DEPLOYMENT_ID/exec"
```

**4. Build, flash, and upload the filesystem**

```
pio run --target upload
pio run --target uploadfs
pio device monitor
```

`uploadfs` pushes `data/names.csv` to the device. The version in this repo
holds two sample rows — replace them with your own `UID,Name` lines before
uploading.

**5. Add a test row** to the Sheet's `Names` tab so there is something to
scan against.

> Any time you edit the Apps Script, redeploy it as a **new version**.
> Editing alone does not update the live URL.

## Known issues already solved (so nobody re-discovers them)
- Apps Script's redirect after a POST goes to a different host
  (`script.googleusercontent.com`) and can't just be "followed" normally —
  the device treats HTTP 302 as success and doesn't chase the redirect.
- Google Sheets silently converts date/time-looking text into real
  Date/Time values when written via script — this broke same-day row
  matching until the Date/In Time cells were forced to plain-text format.

## Privacy note

`data/names.csv` maps card UIDs to real people's names, and the Google Sheet
holds their attendance history. The copy in this repo is sample data only.
If you fork this for real use, keep your own names file out of git and be
deliberate about who can see the sheet.

## License

MIT — see [LICENSE](LICENSE).
