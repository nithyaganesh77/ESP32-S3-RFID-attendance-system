/**
 * RFID Attendance Logger - Google Sheets receiver
 * Deploy as a Web App (Extensions > Apps Script).
 *
 * doPost: ESP32 (sheets_sync.cpp) sends one scan as JSON
 *   { "uid": "...", "name": "...", "timestamp": "..." }
 *   Writes to "Scans": Name | Date (dd.MM.yyyy) | In Time | Out Time | Duration
 *   F/G are hidden helper columns for epoch math — hide, don't delete.
 *   1st scan/day -> new row. 2nd scan/day -> fills Out Time + Duration.
 *   Repeat scans within 5 min are debounced (ignored).
 *
 * doGet: ESP32 (names_sync.cpp) polls this URL to refresh its UID->Name
 *   cache from the "Names" sheet. Add a row there to register a new
 *   member — no reflash needed.
 */

var DEBOUNCE_MS = 5 * 60 * 1000; // 5 minutes

function doPost(e) {
  var lock = LockService.getScriptLock();
  lock.waitLock(10000);

  try {
    var data = JSON.parse(e.postData.contents);
    var ss = SpreadsheetApp.getActiveSpreadsheet();
    var sheet = getOrCreateSheet(ss, "Scans",
      ["Name", "Date", "In Time", "Out Time", "Duration", "_InEpoch", "_OutEpoch"]);

    var now = new Date();
    var todayStr = Utilities.formatDate(now, "Asia/Kolkata", "dd.MM.yyyy");
    var name = data.name;

    var lastRow = sheet.getLastRow();
    var openRow = -1;
    var lastEventEpoch = null;

    if (lastRow > 1) {
      var range = sheet.getRange(2, 1, lastRow - 1, 7).getValues(); // A:G
      for (var i = range.length - 1; i >= 0; i--) { // search newest-first
        var rowName = range[i][0];
        var rowDate = range[i][1];
        var inEpoch = range[i][5];
        var outEpoch = range[i][6];

        // Sheets can silently convert date-looking text into a real Date
        // object on write, so compare as formatted strings, not ===.
        var rowDateStr = (rowDate instanceof Date)
          ? Utilities.formatDate(rowDate, "Asia/Kolkata", "dd.MM.yyyy")
          : String(rowDate);

        if (rowName !== name || rowDateStr !== todayStr) continue;

        lastEventEpoch = outEpoch ? outEpoch : inEpoch;
        if (!outEpoch) openRow = i + 2; // +2: header row + 0-index offset
        break; // most recent row today for this name
      }
    }

    // Debounce: ignore repeat taps within DEBOUNCE_MS of the last recorded event
    if (lastEventEpoch && (now.getTime() - lastEventEpoch) < DEBOUNCE_MS) {
      return jsonResponse({ status: "ok", note: "debounced" });
    }

    var timeStr = Utilities.formatDate(now, "Asia/Kolkata", "hh:mm a"); // e.g. 09:57 AM

    if (openRow > 0) {
      var inEpochStored = sheet.getRange(openRow, 6).getValue();
      var durationMs = now.getTime() - inEpochStored;
      var hours = Math.floor(durationMs / 3600000);
      var minutes = Math.floor((durationMs % 3600000) / 60000);

      sheet.getRange(openRow, 4).setNumberFormat("@");            // keep Out Time as plain text (preserves AM/PM)
      sheet.getRange(openRow, 4).setValue(timeStr);               // Out Time
      sheet.getRange(openRow, 5).setValue(hours + "h " + minutes + "m"); // Duration
      sheet.getRange(openRow, 7).setValue(now.getTime());         // _OutEpoch
      sheet.getRange(openRow, 4, 1, 2).setHorizontalAlignment("center"); // Out Time, Duration centered
    } else {
      var newRow = sheet.getLastRow() + 1;
      // Force columns B (Date), C (In Time) BEFORE writing, so Sheets can't
      // silently convert "31.07.2026" into a real Date object (broke same-day
      // matching above) or "11:06 AM" into a time serial (dropped AM/PM on display).
      sheet.getRange(newRow, 2).setNumberFormat("@");
      sheet.getRange(newRow, 3).setNumberFormat("@");
      sheet.getRange(newRow, 1, 1, 7).setValues([[name, todayStr, timeStr, "", "", now.getTime(), ""]]);
      sheet.getRange(newRow, 1).setHorizontalAlignment("left");        // Name left-aligned
      sheet.getRange(newRow, 2, 1, 4).setHorizontalAlignment("center"); // Date..Duration centered
    }
    SpreadsheetApp.flush(); // ensure the write is visible to the very next doPost call

    return jsonResponse({ status: "ok" });

  } catch (err) {
    return jsonResponse({ status: "error", message: err.toString() });
  } finally {
    lock.releaseLock();
  }
}

function doGet(e) {
  try {
    var ss = SpreadsheetApp.getActiveSpreadsheet();
    var sheet = getOrCreateSheet(ss, "Names", ["UID", "Name"]);
    var rows = sheet.getDataRange().getValues();
    var names = [];
    for (var i = 1; i < rows.length; i++) { // skip header row
      var uid = String(rows[i][0]).trim();
      var name = String(rows[i][1]).trim();
      if (uid.length > 0) names.push({ uid: uid, name: name });
    }
    return jsonResponse({ status: "ok", names: names });
  } catch (err) {
    return jsonResponse({ status: "error", message: err.toString() });
  }
}

function getOrCreateSheet(ss, name, headerRow) {
  var sheet = ss.getSheetByName(name);
  if (!sheet) {
    sheet = ss.insertSheet(name);
    sheet.appendRow(headerRow);
    sheet.setFrozenRows(1);
    sheet.getRange(1, 1, 1, headerRow.length).setHorizontalAlignment("center");
    sheet.getRange(1, 1).setHorizontalAlignment("left"); // "Name" header left-aligned
  }
  return sheet;
}

function jsonResponse(obj) {
  return ContentService
    .createTextOutput(JSON.stringify(obj))
    .setMimeType(ContentService.MimeType.JSON);
}