/* Bizli Tracker Prototype (Full Working Version)
   - Saves WiFi credentials + auto reconnect
   - Falls back to AP mode if WiFi fails
   - Saves devices + usage to flash
   - Live electricity consumption graph (instant W + total kWh)
*/

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
// ===== Last 60 Seconds =====
#define MAX_POINTS 60
float recentInstant[MAX_POINTS] = {0};
float recentTotal[MAX_POINTS] = {0};
int pointIndex = 0;


// ===== WiFi AP Settings =====
const char* apSSID = "Bizli Tracker Prototype";
const char* apPassword = "bizlitracker@2025";
ESP8266WebServer server(80);

bool wifiConnected = false;
String targetSSID = "";
String targetPASS = "";

// ===== File paths =====
const char *WIFI_PATH = "/wifi.json";
const char *DATA_PATH = "/devices.json";

// ===== Device Struct =====
struct Device {
  String name;
  int powerWatt;
  bool state;
  unsigned long startTime;
  unsigned long totalRuntime;
  int pin;
};

Device devices[3];
int deviceCount = 0;
int devicePins[3] = {14, 12, 13}; // D5, D6, D7

unsigned long lastSave = 0;

// ===== Helper: format runtime =====
String formatRuntimeMS(unsigned long ms) {
  unsigned long sec = ms / 1000;
  unsigned long h = sec / 3600;
  unsigned long m = (sec % 3600) / 60;
  unsigned long s = sec % 60;
  char buf[16];
  sprintf(buf, "%02lu:%02lu:%02lu", h, m, s);
  return String(buf);
}

// ===== Save/Load WiFi =====
void saveWiFiCreds(String ssid, String pass) {
  StaticJsonDocument<256> doc;
  doc["ssid"] = ssid;
  doc["pass"] = pass;
  File f = LittleFS.open(WIFI_PATH, "w");
  if (!f) return;
  serializeJson(doc, f);
  f.close();
}

bool loadWiFiCreds() {
  if (!LittleFS.exists(WIFI_PATH)) return false;
  File f = LittleFS.open(WIFI_PATH, "r");
  if (!f) return false;
  StaticJsonDocument<256> doc;
  if (deserializeJson(doc, f)) { f.close(); return false; }
  f.close();
  targetSSID = String(doc["ssid"] | "");
  targetPASS = String(doc["pass"] | "");
  return targetSSID.length() > 0;
}

// ===== Save/Load Devices =====
void saveData() {
  StaticJsonDocument<1024> doc;
  doc["deviceCount"] = deviceCount;
  JsonArray arr = doc.createNestedArray("devices");
  for (int i = 0; i < deviceCount; ++i) {
    JsonObject o = arr.createNestedObject();
    o["name"] = devices[i].name;
    o["powerWatt"] = devices[i].powerWatt;
    o["state"] = devices[i].state;
    o["totalRuntime"] = devices[i].totalRuntime + (devices[i].state ? (millis() - devices[i].startTime) : 0);
    o["pin"] = devices[i].pin;
  }
  File f = LittleFS.open(DATA_PATH, "w");
  if (!f) return;
  serializeJson(doc, f);
  f.close();
}

void loadData() {
  if (!LittleFS.exists(DATA_PATH)) return;
  File f = LittleFS.open(DATA_PATH, "r");
  if (!f) return;
  StaticJsonDocument<1024> doc;
  if (deserializeJson(doc, f)) { f.close(); return; }
  f.close();
  int savedCount = doc["deviceCount"] | 0;
  JsonArray arr = doc["devices"].as<JsonArray>();
  deviceCount = 0;
  for (int i = 0; i < savedCount && i < 3; i++) {
    JsonObject o = arr[i];
    devices[i].name = String(o["name"] | "");
    devices[i].powerWatt = o["powerWatt"] | 0;
    devices[i].state = o["state"] | false;
    devices[i].totalRuntime = (unsigned long)(o["totalRuntime"] | 0UL);
    devices[i].pin = devicePins[i];
    devices[i].startTime = devices[i].state ? millis() : 0;
    pinMode(devices[i].pin, OUTPUT);
    digitalWrite(devices[i].pin, devices[i].state ? HIGH : LOW);
    deviceCount++;
  }
}

// ===== Compute Usage =====
float computeTotalUsage_kWh() {
  float totalUsage = 0.0;
  for (int i = 0; i < deviceCount; i++) {
    unsigned long runtime = devices[i].totalRuntime;
    if (devices[i].state) runtime += millis() - devices[i].startTime;
    float hours = (float)runtime / 3600000.0;
    totalUsage += hours * (devices[i].powerWatt / 1000.0);
  }
  return totalUsage;
}

float computeInstantPower_W() {
  float total = 0;
  for (int i = 0; i < deviceCount; i++) {
    if (devices[i].state) total += devices[i].powerWatt;
  }
  return total;
}

// ===== HTML Pages =====
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

String getDevicesTable() {
  String html = "<table border='1'><tr><th>Name</th><th>Power(W)</th><th>State</th><th>Runtime</th><th>Usage(kWh)</th></tr>";
  for (int i = 0; i < deviceCount; i++) {
   html += "<tr><td>" + devices[i].name + "</td><td>" + String(devices[i].powerWatt) + "</td><td>"
        "<a href='/toggle?id=" + String(i) + "'>"
        "<button class='" + (devices[i].state ? "on" : "off") + "'>"
        + (devices[i].state ? "ON" : "OFF") + "</button></a></td>"
        "<td>" + formatRuntimeMS(devices[i].totalRuntime + (devices[i].state ? (millis() - devices[i].startTime) : 0)) + "</td>"
        "<td>" + String((devices[i].powerWatt / 1000.0) * ((devices[i].totalRuntime + (devices[i].state ? millis() - devices[i].startTime : 0)) / 3600000.0), 3) + " kWh</td></tr>";

  }
  html += "</table><a href='/'><button class='back'>Back</button></a>";
  return html;
}


String getViewDevicesPage() {
  String html = "<!DOCTYPE html><html><head>"
                "<meta name='viewport' content='width=device-width, initial-scale=1'>"
                "<title>View Devices</title>"
                "<style>"
                "body{background:#000;color:#00FF00;font-family:Arial;text-align:center;}"
                "h2{color:#00FF00;}"
                "table{margin:auto;border-collapse:collapse;width:95%;}"
                "th,td{border:1px solid #00FF00;padding:8px;text-align:center;}"
                "th{background:#004400;color:#00FF00;}"
                "button{padding:8px 18px;margin:5px;font-size:15px;border:none;border-radius:5px;cursor:pointer;}"
                ".on{background:#00FF00;color:black;}.off{background:#FF6000;color:white;}"
                ".back{background:#007700;color:white;margin-top:15px;}.back:hover{background:#004400;}"
                "</style>"
                "<script>"
                "function refreshDevices(){"
                " fetch('/viewData').then(r=>r.text()).then(html=>{ document.getElementById('devicesTable').innerHTML = html; });"
                "}"
                "setInterval(refreshDevices,1000); window.onload = refreshDevices;"
                "</script></head><body>";
  html += "<h2>Added Devices</h2><div id='devicesTable'>" + getDevicesTable() + "</div>";
  html += "</body></html>";
  return html;
}


String getConsumptionPage() {
  String html = "<!DOCTYPE html><html><head>"
                "<meta name='viewport' content='width=device-width, initial-scale=1'>"
                "<title>Electricity Consumption</title>"
                "<style>"
                "body{background:black;color:#00FF00;font-family:Arial;text-align:center;}"
                "canvas{background:#001900;margin:auto;display:block;}"
                "button{padding:10px 20px;margin:10px;font-size:16px;border:none;border-radius:5px;"
                "background:#00FF00;color:black;cursor:pointer;} button:hover{background:#00AA00;}"
                "</style>"
                "<script src='https://cdn.jsdelivr.net/npm/chart.js'></script>"
                "<script>"
                "let ctx, chart;"
                "function initChart(){"
                " ctx = document.getElementById('usageChart').getContext('2d');"
                " chart = new Chart(ctx,{"
                " type:'line',"
                " data:{labels:Array(60).fill(''),datasets:["
                "  {label:'Instant Power (W)',borderColor:'lime',fill:false,data:[]},"
                "  {label:'Total Usage (kWh)',borderColor:'orange',fill:false,data:[]}"
                " ]},"
                " options:{"
                "  animation:false,"
                "  scales:{"
                "   x:{title:{display:true,text:\"Last 60 seconds\"},ticks:{display:false}},"
                "   y:{beginAtZero:true}"
                "  }"
                " }"
                "});"
                "}"



                "function updateChart(){"
                " fetch('/consumptionJSON').then(r=>r.json()).then(d=>{"
                "  chart.data.labels = Array(d.instant.length).fill('');"
                "   chart.data.datasets[0].data = d.instant;"
                "   chart.data.datasets[1].data = d.total;"
                "  chart.update();"
                " });"
                "}"



                "setInterval(updateChart,1000); window.onload = ()=>{initChart(); updateChart();};"
                "</script>"
                "</head><body>"
                "<h2>Electricity Consumption</h2>"
                "<canvas id='usageChart' width='400' height='200'></canvas>"
                "<br><a href='/'><button>Back</button></a>"
                "</body></html>";
  return html;
}



// ===== Server Handlers =====
void handleRoot() {
  if (!wifiConnected) server.send(200,"text/html",getWiFiListPage());
  else server.send(200,"text/html",getControlPage());
}

void handleConnect() {
  if (server.hasArg("ssid")) {
    targetSSID = server.arg("ssid");
    server.send(200,"text/html",getWiFiPasswordPage(targetSSID));
  } else server.send(400,"text/plain","Missing ssid");
}

void handleDoConnect() {
  if (server.hasArg("ssid") && server.hasArg("pass")) {
    targetSSID = server.arg("ssid");
    targetPASS = server.arg("pass");
    WiFi.begin(targetSSID.c_str(), targetPASS.c_str());
    int tries = 0;
    while (WiFi.status() != WL_CONNECTED && tries < 20) { delay(500); tries++; }
    if (WiFi.status() == WL_CONNECTED) {
      wifiConnected = true;
      saveWiFiCreds(targetSSID, targetPASS);
      server.sendHeader("Location","/",true);
      server.send(302,"text/plain","Redirecting...");
    } else {
      wifiConnected = false;
      server.send(200,"text/html","<h3>Failed to Connect. Try Again.</h3><a href='/'><button>Back</button></a>");
    }
  } else server.send(400,"text/plain","Missing ssid or pass");
}

void handleAdd() {
  if (deviceCount>=3) { server.send(200,"text/html","<h3>Max 3 devices allowed</h3><a href='/'>Back</a>"); return; }
  if (server.hasArg("name") && server.hasArg("power")) {
    devices[deviceCount].name = server.arg("name");
    devices[deviceCount].powerWatt = server.arg("power").toInt();
    devices[deviceCount].state = false;
    devices[deviceCount].totalRuntime = 0;
    devices[deviceCount].startTime = 0;
    devices[deviceCount].pin = devicePins[deviceCount];
    pinMode(devices[deviceCount].pin,OUTPUT);
    digitalWrite(devices[deviceCount].pin,LOW);
    deviceCount++;
    server.sendHeader("Location","/",true);
    server.send(302,"text/plain","Redirecting...");
  } else server.send(200,"text/html",getAddDevicePage(deviceCount+1));
}

void handleView() { server.send(200,"text/html",getViewDevicesPage()); }
void handleViewData() { server.send(200,"text/html",getDevicesTable()); }

void handleToggle() {
  if (server.hasArg("id")) {
    int id = server.arg("id").toInt();
    if (id>=0 && id<deviceCount) {
      if (devices[id].state) {
        devices[id].totalRuntime += millis()-devices[id].startTime;
        devices[id].state = false;
        digitalWrite(devices[id].pin,LOW);
      } else {
        devices[id].startTime = millis();
        devices[id].state = true;
        digitalWrite(devices[id].pin,HIGH);
      }
    }
  }
  server.sendHeader("Location","/view",true);
  server.send(302,"text/plain","Redirecting...");
}

void handleConsumption() { server.send(200,"text/html",getConsumptionPage()); }

void handleConsumptionJSON() {
  StaticJsonDocument<512> doc;
  JsonArray inst = doc.createNestedArray("instant");
  JsonArray tot  = doc.createNestedArray("total");
  
  for (int i = 0; i < MAX_POINTS; i++) {
    int idx = (pointIndex + i) % MAX_POINTS; // oldest first
    inst.add(recentInstant[idx]);
    tot.add(recentTotal[idx]);
  }

  String payload;
  serializeJson(doc, payload);
  server.send(200, "application/json", payload);
}



// ===== Setup =====
void setup() {
  Serial.begin(115200);
  LittleFS.begin();
loadData();

// Always start AP
WiFi.mode(WIFI_AP_STA);          // Dual mode: AP + STA
WiFi.softAP(apSSID, apPassword); // AP will always be visible
Serial.println("AP started: " + String(apSSID));

// Try connecting to saved WiFi
if (loadWiFiCreds()) {
  WiFi.begin(targetSSID.c_str(), targetPASS.c_str());
  Serial.println("Connecting to saved WiFi: " + targetSSID);
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 10000) {
    delay(500);
    Serial.print(".");
  }
  if (WiFi.status() == WL_CONNECTED) {
    wifiConnected = true;
    Serial.println("\nConnected to " + targetSSID + " IP: " + WiFi.localIP().toString());
  } else {
    wifiConnected = false;
    Serial.println("\nFailed to connect to saved WiFi, AP mode still active.");
  }
} else {
  wifiConnected = false;
  Serial.println("No saved WiFi, AP mode active.");
}


  // Register routes
  server.on("/", handleRoot);
  server.on("/connect", handleConnect);
  server.on("/doconnect", HTTP_POST, handleDoConnect);
  server.on("/add", handleAdd);
  server.on("/view", handleView);
  server.on("/viewData", handleViewData);
  server.on("/toggle", handleToggle);
  server.on("/consumption", handleConsumption);
  server.on("/consumptionJSON", handleConsumptionJSON);

  server.begin();
  Serial.println("HTTP server started");
}

// ===== Loop =====
void loop() {
  server.handleClient();
  if (millis() - lastSave > 1000) { // every 1 second
    saveData();
    // Update recent arrays
    recentInstant[pointIndex] = computeInstantPower_W();
    recentTotal[pointIndex] = computeTotalUsage_kWh();
    pointIndex = (pointIndex + 1) % MAX_POINTS; // wrap around
    lastSave = millis();
  }

}
