

```javascript
function doGet(e) {
  var ss = SpreadsheetApp.getActiveSpreadsheet();
  var sheet = ss.getSheetByName("Attendance");

  if (!sheet) {
    sheet = ss.insertSheet("Attendance");
    sheet.appendRow(["Name", "Status", "Date", "Time", "Device"]);
  }

  var name = e.parameter.name || "Unknown";
  var status = e.parameter.status || "Present";
  var device = e.parameter.device || "ESP32-CAM";

  var now = new Date();
  var date = Utilities.formatDate(now, "Asia/Karachi", "yyyy-MM-dd");
  var time = Utilities.formatDate(now, "Asia/Karachi", "HH:mm:ss");

  sheet.appendRow([name, status, date, time, device]);

  return ContentService
    .createTextOutput("Attendance marked for " + name)
    .setMimeType(ContentService.MimeType.TEXT);
}