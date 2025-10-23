#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>

// ===== WiFi Access Point (Hotspot) =====
const char* apSSID = "Bizli Tracker Prototype";
const char* apPassword = "bizlitracker@2025";

ESP8266WebServer server(80);
bool wifiConnected = false;
String targetSSID = "";

// ===== Device Struct =====
struct Device {
  String name;
  int powerWatt;              // Device power in Watts
  bool state;                 // ON/OFF
  unsigned long startTime;    // Timestamp when turned ON
  unsigned long totalRuntime; // Total ON time in ms (accumulated)
  int pin;                    // GPIO pin number
};

// Max 3 devices allowed
Device devices[3];
int deviceCount = 0;
int devicePins[3] = {14, 12, 13}; // D5, D6, D7

// ----------------- Helper: format runtime ms -> "HH:MM:SS" -----------------
String formatRuntimeMS(unsigned long ms) {
  unsigned long sec = ms / 1000;
  unsigned long h = sec / 3600;
  unsigned long m = (sec % 3600) / 60;
  unsigned long s = sec % 60;
  char buf[16];
  sprintf(buf, "%02lu:%02lu:%02lu", h, m, s);
  return String(buf);
}

// ================= HTML Pages =================

// --- WiFi List Page (sorted by RSSI) ---
String getWiFiListPage() {
  int n = WiFi.scanNetworks();  // Scan available WiFi

  String page = "<!DOCTYPE html><html><head>"
                "<meta name='viewport' content='width=device-width, initial-scale=1'>"
                "<title>Select WiFi</title>"
                "<style>"
                "body{background:black;color:#00FF00;font-family:Arial;text-align:center;}"
                "h2{color:#00FF00;}"
                "table{margin:auto;border-collapse:collapse;width:90%;}"
                "th,td{border:1px solid #00FF00;padding:10px;}"
                "td.ssid{text-align:left;}"
                "button{padding:8px 20px;background:#00FF00;color:black;border:none;border-radius:5px;cursor:pointer;}"
                "button:hover{background:#008800;color:white;}"
                "</style></head><body>";

  page += "<h2>Available WiFi Networks</h2>";

  if (n <= 0) {
    page += "<p>No networks found. <a href='/'><button>Refresh</button></a></p>";
    page += "</body></html>";
    return page;
  }

  // Store SSID + RSSI in arrays for sorting (dynamic allocation)
  String* ssids = new String[n];
  int* rssis = new int[n];
  for (int i = 0; i < n; i++) {
    ssids[i] = WiFi.SSID(i);
    rssis[i] = WiFi.RSSI(i);
  }

  // Sort by strongest signal (simple bubble)
  for (int i = 0; i < n - 1; i++) {
    for (int j = i + 1; j < n; j++) {
      if (rssis[j] > rssis[i]) {
        int tmpR = rssis[i]; rssis[i] = rssis[j]; rssis[j] = tmpR;
        String tmpS = ssids[i]; ssids[i] = ssids[j]; ssids[j] = tmpS;
      }
    }
  }

  page += "<table><tr><th>SSID</th><th>Signal</th><th>Action</th></tr>";
  for (int i = 0; i < n; i++) {
    page += "<tr><td class='ssid'>" + ssids[i] + "</td><td>" + String(rssis[i]) + " dBm</td>"
            "<td><a href='/connect?ssid=" + ssids[i] + "'><button>Connect</button></a></td></tr>";
  }
  page += "</table>";
  page += "<br><a href='/'><button>Refresh</button></a>";

  // cleanup
  delete[] ssids;
  delete[] rssis;

  page += "</body></html>";
  return page;
}

// --- WiFi Password Input Page ---
String getWiFiPasswordPage(String ssid) {
  String page = "<!DOCTYPE html><html><head>"
                "<meta name='viewport' content='width=device-width, initial-scale=1'>"
                "<title>Enter Password</title>"
                "<style>"
                "body{background:black;color:#00FF00;font-family:Arial;text-align:center;}"
                "h3{color:#00FF00;}"
                "input, button{padding:10px;margin:5px;font-size:16px;border:none;border-radius:5px;}"
                "button{background:#00FF00;color:black;cursor:pointer;}"
                "button:hover{background:#00AA00;}"
                "</style></head><body>";

  page += "<h3>Enter password for " + ssid + "</h3>";
  page += "<form action='/doconnect' method='POST'>";
  page += "<input type='hidden' name='ssid' value='" + ssid + "'>";
  page += "Password: <input type='password' name='pass'><br>";
  page += "<button type='submit'>Connect</button>";
  page += "</form></body></html>";
  return page;
}

// --- Main Control Page ---
String getControlPage() {
  String page = "<!DOCTYPE html><html><head>"
                "<meta name='viewport' content='width=device-width, initial-scale=1'>"
                "<title>Bizli Tracker Control</title>"
                "<style>"
                "body{background:#001900;color:#00FF00;font-family:Arial;text-align:center;}"
                "h2{color:#00FF00;}"
                "button{padding:12px 25px;margin:10px;font-size:16px;border:none;border-radius:5px;"
                "background:#007700;color:white;cursor:pointer;transition:0.3s;}"
                "button:hover{background:#004400;}"
                "</style></head><body>";

  page += "<h2>Bizli Tracker Control Panel</h2>";
  page += "<a href='/add'><button>Add New Device</button></a>";
  page += "<a href='/view'><button>View Devices</button></a>";
  page += "<a href='/consumption'><button>Electricity Consumption</button></a>";
  page += "</body></html>";
  return page;
}

// --- Add Device Page ---
String getAddDevicePage(int deviceNumber) {
  String page = "<!DOCTYPE html><html><head>"
                "<meta name='viewport' content='width=device-width, initial-scale=1'>"
                "<title>Add Device</title>"
                "<style>"
                "body{background:black;color:#00FF00;font-family:Arial;text-align:center;}"
                "h3{color:#00FF00;}"
                "input, button{padding:10px;margin:5px;font-size:16px;border:none;border-radius:5px;}"
                "button{background:#00FF00;color:black;cursor:pointer;}"
                "button:hover{background:#00AA00;}"
                "</style></head><body>";

  page += "<h3>Add Device " + String(deviceNumber) + "</h3>";
  page += "<form action='/add' method='GET'>";
  page += "Device Name: <input type='text' name='name' required><br>";
  page += "Power (Watt): <input type='number' name='power' required><br>";
  page += "<button type='submit'>Add Device</button>";
  page += "</form>";
  page += "<br><a href='/'><button>Back</button></a>";
  page += "</body></html>";
  return page;
}

// --- View Devices Page (AJAX updated table, Back button included) ---
String getViewDevicesPage() {
  // The page contains a div (#devicesTable) which is replaced every second by JS calling /viewData.
  String page = "<!DOCTYPE html><html><head>"
                "<meta name='viewport' content='width=device-width, initial-scale=1'>"
                "<title>View Devices</title>"
                "<style>"
                "body{background:#000;color:#00FF00;font-family:Arial;text-align:center;}"
                "h2{color:#00FF00;}"
                "table{margin:auto;border-collapse:collapse;width:95%;}"
                "th,td{border:1px solid #00FF00;padding:8px;text-align:center;}"
                "th{background:#004400;color:#00FF00;}"
                "button{padding:8px 18px;margin:5px;font-size:15px;border:none;border-radius:5px;cursor:pointer;}"
                ".on{background:#00FF00;color:black;}"
                ".off{background:#FF6000;color:white;}"
                ".back{background:#007700;color:white;margin-top:15px;}"
                ".back:hover{background:#004400;}"
                "</style>"
                "<script>"
                "function refreshDevices(){"
                " fetch('/viewData').then(r=>r.text()).then(html=>{ document.getElementById('devicesTable').innerHTML = html; });"
                "}"
                "setInterval(refreshDevices,1000);"
                // call once immediately so user doesn't wait 1s
                "window.onload = refreshDevices;"
                "</script></head><body>";

  page += "<h2>Added Devices</h2>";
  page += "<div id='devicesTable'>";             // initial content will be replaced by AJAX
  page += getDevicesTable();                     // include initial table server-side to avoid blank
  page += "</div>";
  page += "<br><a href='/'><button class='back'>Back</button></a>";
  page += "</body></html>";
  return page;
}

// --- Builds full devices HTML table (used by AJAX /viewData and initial page) ---
String getDevicesTable() {
  String table = "<table><tr><th>Device Name</th><th>Switch</th><th>Power (W)</th><th>Runtime</th><th>Usage (kWh)</th></tr>";

  for (int i = 0; i < deviceCount; i++) {
    unsigned long runtime = devices[i].totalRuntime;
    if (devices[i].state) runtime += millis() - devices[i].startTime;

    float usage = (runtime / 3600000.0) * (devices[i].powerWatt / 1000.0);

    table += "<tr>";
    table += "<td style='text-align:left;'>" + String(i + 1) + ". " + devices[i].name + "</td>";

    // Toggle link uses `id` parameter (match server handler)
    table += "<td><a href='/toggle?id=" + String(i) + "'>"
             "<button class='" + String(devices[i].state ? "on" : "off") + "'>"
             + String(devices[i].state ? "ON" : "OFF") +
             "</button></a></td>";

    table += "<td>" + String(devices[i].powerWatt) + "</td>";
    table += "<td>" + formatRuntimeMS(runtime) + "</td>";
    table += "<td>" + String(usage, 3) + "</td>";
    table += "</tr>";
  }
  table += "</table>";
  return table;
}

// --- Electricity Consumption Page ---
String getConsumptionPage() {
  String page = "<!DOCTYPE html><html><head>"
                "<meta name='viewport' content='width=device-width, initial-scale=1'>"
                "<title>Electricity Consumption</title>"
                "<style>"
                "body{background:black;color:#00FF00;font-family:Arial;text-align:center;}"
                "button{padding:10px 20px;margin:10px;font-size:16px;border:none;border-radius:5px;"
                "background:#00FF00;color:black;cursor:pointer;}"
                "button:hover{background:#00AA00;}"
                "</style>"
                "<script>"
                "function refreshUsage(){"
                " fetch('/consumptionData').then(r=>r.text()).then(txt=>{ document.getElementById('usageSpan').innerText = txt; });"
                "}"
                "setInterval(refreshUsage,1000);"
                "window.onload = refreshUsage;"
                "</script></head><body>";

  page += "<h2>Total Electricity Consumption</h2>";
  page += "<h3><span id='usageSpan'>0 kWh</span></h3>";
  page += "<br><a href='/'><button>Back</button></a>";
  page += "</body></html>";
  return page;
}

// ================= Server Handlers =================

void handleRoot() {
  if (!wifiConnected) {
    server.send(200, "text/html", getWiFiListPage());
  } else {
    server.send(200, "text/html", getControlPage());
  }
}

void handleConnect() {
  if (server.hasArg("ssid")) {
    targetSSID = server.arg("ssid");
    server.send(200, "text/html", getWiFiPasswordPage(targetSSID));
  } else {
    server.send(400, "text/plain", "Missing ssid");
  }
}

void handleDoConnect() {
  if (server.hasArg("ssid") && server.hasArg("pass")) {
    String ssid = server.arg("ssid");
    String pass = server.arg("pass");

    WiFi.begin(ssid.c_str(), pass.c_str());
    int tries = 0;
    while (WiFi.status() != WL_CONNECTED && tries < 20) {
      delay(500);
      tries++;
    }

    if (WiFi.status() == WL_CONNECTED) {
      wifiConnected = true;
      server.sendHeader("Location", "/", true);
      server.send(302, "text/plain", "Redirecting...");
    } else {
      wifiConnected = false;
      server.send(200, "text/html", "<h3>Failed to Connect. Try Again.</h3><a href='/'>Back</a>");
    }
  } else {
    server.send(400, "text/plain", "Missing ssid or pass");
  }
}

void handleAdd() {
  if (deviceCount >= 3) {
    server.send(200, "text/html", "<h3>Max 3 devices allowed</h3><a href='/'>Back</a>");
    return;
  }

  if (server.hasArg("name") && server.hasArg("power")) {
    devices[deviceCount].name = server.arg("name");
    devices[deviceCount].powerWatt = server.arg("power").toInt();
    devices[deviceCount].state = false;
    devices[deviceCount].totalRuntime = 0;
    devices[deviceCount].startTime = 0;
    devices[deviceCount].pin = devicePins[deviceCount];
    pinMode(devices[deviceCount].pin, OUTPUT);
    digitalWrite(devices[deviceCount].pin, LOW);
    deviceCount++;
    server.sendHeader("Location", "/", true);
    server.send(302, "text/plain", "Redirecting...");
  } else {
    server.send(200, "text/html", getAddDevicePage(deviceCount + 1));
  }
}

void handleView() {
  server.send(200, "text/html", getViewDevicesPage());
}

// AJAX endpoint that returns the current devices table (used by /view page JS)
void handleViewData() {
  server.send(200, "text/html", getDevicesTable());
}

void handleToggle() {
  // toggle expects "id" parameter
  if (server.hasArg("id")) {
    int id = server.arg("id").toInt();
    if (id >= 0 && id < deviceCount) {
      if (devices[id].state) {
        // turning OFF: add elapsed time to totalRuntime
        devices[id].totalRuntime += millis() - devices[id].startTime;
        devices[id].state = false;
        digitalWrite(devices[id].pin, LOW);
      } else {
        // turning ON: record start time
        devices[id].startTime = millis();
        devices[id].state = true;
        digitalWrite(devices[id].pin, HIGH);
      }
    }
  }
  // redirect back to view page
  server.sendHeader("Location", "/view", true);
  server.send(302, "text/plain", "Redirecting...");
}

void handleConsumption() {
  server.send(200, "text/html", getConsumptionPage());
}

void handleConsumptionData() {
  float totalUsage = 0.0;
  for (int i = 0; i < deviceCount; i++) {
    unsigned long runtime = devices[i].totalRuntime;
    if (devices[i].state) runtime += millis() - devices[i].startTime;
    totalUsage += (runtime / 3600000.0) * (devices[i].powerWatt / 1000.0);
  }
  server.send(200, "text/plain", String(totalUsage, 3) + " kWh");
}

// ================= Setup =================
void setup() {
  Serial.begin(115200);

  // Start AP Mode (Hotspot)
  WiFi.softAP(apSSID, apPassword);
  Serial.println("AP started: " + String(apSSID));

  // Register routes
  server.on("/", handleRoot);
  server.on("/connect", handleConnect);
  server.on("/doconnect", HTTP_POST, handleDoConnect);
  server.on("/add", handleAdd);
  server.on("/view", handleView);
  server.on("/viewData", handleViewData); // AJAX refresh endpoint
  server.on("/toggle", handleToggle);
  server.on("/consumption", handleConsumption);
  server.on("/consumptionData", handleConsumptionData);

  server.begin();
  Serial.println("HTTP Server started");
}

// ================= Main Loop =================
void loop() {
  server.handleClient();
}
