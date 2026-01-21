// Hunter Irrigation Controller with OTA Updates and Schedule
// ESP8266-based web server for Hunter X-Core irrigation systems

#include "ESP8266WiFi.h"
#include "ESPAsyncWebServer.h"
#include "ArduinoJson.h"
#include "FS.h"
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <NTPClient.h>
#include "hunter.h"

// ========== CONFIGURATION ==========
const char* ssid = "Your_Wifi_ssid";
const char* password = "Your_Password";

// OTA Configuration
const char* OTA_HOSTNAME = "hunter-irrigation";
const char* OTA_PASSWORD = "hunter123"; // Change this!

// File system for storing schedules
const char* SCHEDULE_FILE = "/schedules.json";

// NTP Configuration
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", 0, 60000); // Update every minute

// ========== GLOBAL VARIABLES ==========
struct Schedule {
  int zone;
  int duration;  // minutes
  bool enabled;
  String days;   // "1111100" = Mon-Fri (1=active, 0=inactive)
  String startTime; // "08:00"
};

std::vector<Schedule> schedules;
int activeZone = 0;
int activeTimeRemaining = 0;
unsigned long lastWateringUpdate = 0;
const unsigned long WATERING_UPDATE_INTERVAL = 60000; // 1 minute

// Web server and control variables
AsyncWebServer server(80);
bool pendingAction = false;
String actionType = "";
int actionZone = 0;
int actionDuration = 0;
int actionProgram = 0;
unsigned long actionStartTime = 0;

// Schedule simulation (using millis for testing)
unsigned long scheduleStartMillis = 0;

// ========== HTML PAGE ==========
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML>
<html>
<head>
  <title>Hunter Irrigation Controller</title>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <style>
    body { 
      font-family: Arial, sans-serif; 
      margin: 20px; 
      background: #f5f5f5;
    }
    .container { 
      max-width: 800px; 
      margin: 0 auto; 
      background: white; 
      padding: 20px; 
      border-radius: 10px;
      box-shadow: 0 2px 10px rgba(0,0,0,0.1);
    }
    .card { 
      background: #fff; 
      border: 1px solid #ddd; 
      border-radius: 5px; 
      padding: 15px; 
      margin: 10px 0;
    }
    .status { 
      background: #e8f5e8; 
      border-left: 4px solid #4CAF50;
    }
    .manual-control { 
      background: #e3f2fd; 
      border-left: 4px solid #2196F3;
    }
    .scheduling { 
      background: #fff3e0; 
      border-left: 4px solid #FF9800;
    }
    .ota { 
      background: #fce4ec; 
      border-left: 4px solid #E91E63;
    }
    button { 
      background: #4CAF50; 
      color: white; 
      border: none; 
      padding: 10px 15px; 
      margin: 5px; 
      border-radius: 4px; 
      cursor: pointer;
    }
    button.stop { background: #f44336; }
    button.program { background: #2196F3; }
    button.ota { background: #E91E63; }
    button:hover { opacity: 0.8; }
    input, select { 
      padding: 8px; 
      margin: 5px; 
      border: 1px solid #ddd; 
      border-radius: 4px;
    }
    .schedule-item { 
      background: #f9f9f9; 
      margin: 5px 0; 
      padding: 10px; 
      border-radius: 4px;
    }
    .active-zone { 
      background: #e8f5e8; 
      font-weight: bold;
      padding: 10px;
      border-radius: 4px;
    }
    .ota-status {
      padding: 10px;
      margin: 10px 0;
      border-radius: 4px;
      display: none;
    }
    .ota-progress {
      width: 100%;
      height: 20px;
      margin: 10px 0;
    }
    .action-status {
      background: #fff3e0;
      padding: 10px;
      border-radius: 4px;
      margin: 5px 0;
    }
    .time-display {
      background: #f0f0f0;
      padding: 8px;
      border-radius: 4px;
      font-family: monospace;
      margin: 5px 0;
    }
  </style>
</head>
<body>
  <div class="container">
    <h1>Hunter Irrigation Controller</h1>
    
    <div class="card status">
      <h2>System Status & Network Info</h2>
      <div id="status">Loading...</div>
      <div id="networkInfo" style="margin-top: 10px; font-size: 14px; color: #666;"></div>
      <div id="activeZone" style="margin-top: 10px;"></div>
      <div id="actionStatus" style="margin-top: 10px;"></div>
    </div>

    <div class="card manual-control">
      <h2>Manual Control</h2>
      <div>
        <label>Zone: </label>
        <select id="zoneSelect">
          <option value="0">Select Zone</option>
          <option value="1">Zone 1</option>
          <option value="2">Zone 2</option>
          <option value="3">Zone 3</option>
          <option value="4">Zone 4</option>
          <option value="5">Zone 5</option>
          <option value="6">Zone 6</option>
          <option value="7">Zone 7</option>
          <option value="8">Zone 8</option>
          <option value="9">Zone 9</option>
          <option value="10">Zone 10</option>
          <option value="11">Zone 11</option>
          <option value="12">Zone 12</option>
        </select>
        
        <label>Duration (min): </label>
        <input type="number" id="duration" min="1" max="240" value="10">
        
        <button onclick="startZone()">Start Zone</button>
        <button class="stop" onclick="stopAll()">Stop All Zones</button>
      </div>
      
      <div style="margin-top: 10px;">
        <label>Program: </label>
        <select id="programSelect">
          <option value="1">Program 1</option>
          <option value="2">Program 2</option>
          <option value="3">Program 3</option>
          <option value="4">Program 4</option>
        </select>
        <button class="program" onclick="startProgram()">Start Program</button>
      </div>
    </div>

    <div class="card scheduling">
      <h2>Scheduled Watering</h2>
      <div style="margin-bottom: 15px;">
        <strong>Current Time:</strong> <span id="currentTime" class="time-display">--:--</span> | 
        <strong>Day:</strong> <span id="currentDay" class="time-display">---</span>
      </div>
      <div id="nextSchedule" style="margin-bottom: 15px; font-size: 14px; color: #666;"></div>
      <div id="schedulesList"></div>
      
      <h3>Add New Schedule</h3>
      <div>
        <input type="number" id="newZone" placeholder="Zone (1-12)" min="1" max="12">
        <input type="number" id="newDuration" placeholder="Duration (min)" min="1" max="240">
        <input type="time" id="newTime">
        <input type="text" id="newDays" placeholder="Days (1111100)" maxlength="7" pattern="[01]{7}">
        <button onclick="addSchedule()">Add Schedule</button>
      </div>
      <div style="font-size: 12px; color: #666; margin-top: 5px;">
        <strong>Days format:</strong> 7 digits (0=off, 1=on) starting with Monday.<br>
        <strong>Example:</strong> 1111100 = Monday-Friday, 0000011 = Saturday-Sunday
      </div>
    </div>

    <div class="card ota">
      <h2>OTA Firmware Update</h2>
      <div>
        <input type="file" id="firmwareFile" accept=".bin">
        <button class="ota" onclick="updateFirmware()">Upload & Update Firmware</button>
      </div>
      <div id="otaStatus" class="ota-status"></div>
      <progress id="otaProgress" class="ota-progress" value="0" max="100" style="display: none;"></progress>
      <div style="font-size: 12px; color: #666; margin-top: 10px;">
        <strong>Note:</strong> The system will reboot after update. This may take 1-2 minutes.
      </div>
    </div>
  </div>

  <script>
    function updateCurrentTime() {
        const days = ['Sunday', 'Monday', 'Tuesday', 'Wednesday', 'Thursday', 'Friday', 'Saturday'];
        const now = new Date();
        const timeString = now.getHours().toString().padStart(2, '0') + ':' + 
                          now.getMinutes().toString().padStart(2, '0');
        const dayString = days[now.getDay()];
        
        document.getElementById('currentTime').textContent = timeString;
        document.getElementById('currentDay').textContent = dayString;
    }

    function updateStatus() {
      fetch('/status')
        .then(response => response.json())
        .then(data => {
          let statusText = `System: ${data.systemStatus}`;
          if (data.activeZone > 0) {
            statusText += ` | Zone ${data.activeZone}: ${data.timeRemaining} min remaining`;
          }
          document.getElementById('status').innerHTML = statusText;
          
          // Display network information
          document.getElementById('networkInfo').innerHTML = 
            `IP: ${data.ipAddress} | MAC: ${data.macAddress} | Signal: ${data.signalStrength}dBm`;
          
          if(data.activeZone > 0) {
            document.getElementById('activeZone').innerHTML = 
              `<div class="active-zone">🚿 Watering Zone ${data.activeZone} - ${data.timeRemaining} minutes remaining</div>`;
          } else {
            document.getElementById('activeZone').innerHTML = '';
          }

          if(data.actionStatus && data.actionStatus !== '') {
            document.getElementById('actionStatus').innerHTML = 
              `<div class="action-status">${data.actionStatus}</div>`;
          } else {
            document.getElementById('actionStatus').innerHTML = '';
          }

          // Update next schedule info
          document.getElementById('nextSchedule').innerHTML = 
            `Next check: ${data.nextScheduleCheck}s | Active schedules: ${data.scheduleCount}`;
        })
        .catch(error => {
          console.error('Status update error:', error);
          document.getElementById('status').innerHTML = 'System: Offline - Unable to connect';
        });
    }

    function loadSchedules() {
      fetch('/schedules')
        .then(response => response.json())
        .then(schedules => {
          const list = document.getElementById('schedulesList');
          list.innerHTML = '<h3>Active Schedules:</h3>';
          
          if(schedules.length === 0) {
            list.innerHTML += '<p>No schedules configured</p>';
            return;
          }
          
          schedules.forEach((schedule, index) => {
            const daysText = schedule.days.split('').map((day, i) => 
              day === '1' ? ['Mon','Tue','Wed','Thu','Fri','Sat','Sun'][i] : null
            ).filter(day => day !== null).join(', ');
            
            list.innerHTML += `
              <div class="schedule-item">
                <strong>Zone ${schedule.zone}</strong> - ${schedule.duration} minutes at ${schedule.startTime}<br>
                <small>Days: ${daysText}</small>
                <button style="float: right;" onclick="deleteSchedule(${index})">Delete</button>
                <div style="clear: both;"></div>
              </div>
            `;
          });
        })
        .catch(error => {
          console.error('Load schedules error:', error);
          document.getElementById('schedulesList').innerHTML = '<p>Error loading schedules</p>';
        });
    }

    function startZone() {
      const zone = document.getElementById('zoneSelect').value;
      const duration = document.getElementById('duration').value;
      
      if(zone > 0 && duration > 0) {
        fetch(`/control?zone=${zone}&time=${duration}`)
          .then(response => response.text())
          .then(data => {
            console.log('Zone start response:', data);
            updateStatus();
            setTimeout(updateStatus, 2000);
          })
          .catch(error => {
            console.error('Zone start error:', error);
            alert('Error starting zone: ' + error);
            updateStatus();
          });
      } else {
        alert('Please select a zone and duration');
      }
    }

    function stopAll() {
      if(confirm('Stop all zones?')) {
        fetch('/control?zone=0&time=0')
          .then(response => response.text())
          .then(data => {
            console.log('Stop all response:', data);
            updateStatus();
            setTimeout(updateStatus, 2000);
          })
          .catch(error => {
            console.error('Stop all error:', error);
            alert('Error stopping zones: ' + error);
            updateStatus();
          });
      }
    }

    function startProgram() {
      const program = document.getElementById('programSelect').value;
      fetch(`/control?program=${program}`)
        .then(response => response.text())
        .then(data => {
          console.log('Program start response:', data);
          updateStatus();
          setTimeout(updateStatus, 2000);
        })
        .catch(error => {
          console.error('Program start error:', error);
          alert('Error starting program: ' + error);
          updateStatus();
        });
    }

    function addSchedule() {
      const zone = document.getElementById('newZone').value;
      const duration = document.getElementById('newDuration').value;
      const time = document.getElementById('newTime').value;
      const days = document.getElementById('newDays').value;
      
      if(zone && duration && time && days) {
        if (!/^[01]{7}$/.test(days)) {
          alert('Days must be 7 digits of 0 or 1 (e.g., 1111100 for Mon-Fri)');
          return;
        }
        
        fetch(`/schedule?zone=${zone}&duration=${duration}&time=${time}&days=${days}`)
          .then(() => {
            loadSchedules();
            document.getElementById('newZone').value = '';
            document.getElementById('newDuration').value = '';
            document.getElementById('newTime').value = '';
            document.getElementById('newDays').value = '';
          })
          .catch(error => {
            console.error('Add schedule error:', error);
            alert('Error adding schedule: ' + error);
          });
      } else {
        alert('Please fill all schedule fields');
      }
    }

    function deleteSchedule(index) {
      if(confirm('Delete this schedule?')) {
        fetch(`/schedule?delete=${index}`)
          .then(() => loadSchedules())
          .catch(error => {
            console.error('Delete schedule error:', error);
            alert('Error deleting schedule: ' + error);
          });
      }
    }

    function updateFirmware() {
      const fileInput = document.getElementById('firmwareFile');
      const statusDiv = document.getElementById('otaStatus');
      const progressBar = document.getElementById('otaProgress');
      
      if (!fileInput.files.length) {
        statusDiv.style.display = 'block';
        statusDiv.style.background = '#ffebee';
        statusDiv.style.color = '#c62828';
        statusDiv.innerHTML = 'Please select a firmware file first.';
        return;
      }

      const file = fileInput.files[0];
      const formData = new FormData();
      formData.append('firmware', file);

      statusDiv.style.display = 'block';
      statusDiv.style.background = '#fff3e0';
      statusDiv.style.color = '#ef6c00';
      statusDiv.innerHTML = 'Uploading firmware...';
      progressBar.style.display = 'block';
      progressBar.value = 0;

      fetch('/update', {
        method: 'POST',
        body: formData
      })
      .then(response => response.text())
      .then(data => {
        if (data.includes('Update Success')) {
          statusDiv.style.background = '#e8f5e8';
          statusDiv.style.color = '#2e7d32';
          statusDiv.innerHTML = 'Update successful! System is rebooting...';
          progressBar.value = 100;
          
          setTimeout(() => {
            window.location.reload();
          }, 30000);
        } else {
          throw new Error(data);
        }
      })
      .catch(error => {
        statusDiv.style.background = '#ffebee';
        statusDiv.style.color = '#c62828';
        statusDiv.innerHTML = 'Update failed: ' + error.message;
        progressBar.style.display = 'none';
      });

      let progress = 0;
      const progressInterval = setInterval(() => {
        if (progress < 90) {
          progress += 10;
          progressBar.value = progress;
        }
      }, 1000);

      setTimeout(() => clearInterval(progressInterval), 10000);
    }

    // Update time every minute
    setInterval(updateCurrentTime, 60000);
    updateCurrentTime();
    
    // Update status every 5 seconds
    setInterval(updateStatus, 5000);
    updateStatus();
    loadSchedules();
  </script>
</body>
</html>
)rawliteral";

// ========== FUNCTION DECLARATIONS ==========
void setup();
void loop();
void setupWiFi();
void setupOTA();
void setupWebServer();
void setupFileSystem();
void loadSchedules();
void saveSchedules();
void checkSchedules();
bool shouldRunSchedule(const Schedule& schedule);
void startWatering(int zone, int duration);
void stopWatering();
void updateWatering();
void processPendingActions();
String getCurrentTimeString();
int getCurrentDayOfWeek();
bool isScheduleActiveToday(const Schedule& schedule, int currentDay);
String getDayName(int day);

// ========== SETUP ==========
void setup() {
  Serial.begin(115200);
  Serial.println("\n\nBooting Hunter Irrigation Controller...");
  
  // Initialize Hunter pin only (pump control is integrated in Hunter protocol)
  pinMode(HUNTER_PIN, OUTPUT);
  digitalWrite(HUNTER_PIN, LOW);
  
  // Initialize systems
  setupWiFi();
  setupOTA();
  setupFileSystem();
  setupWebServer();
  
  // Initialize NTP client
  timeClient.begin();
  timeClient.setTimeOffset(2 * 3600); // UTC time, adjust for your timezone if needed
  // For example: -5 * 3600 for EST, -4 * 3600 for EDT
  
  Serial.println("Hunter Irrigation Controller Ready");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
  Serial.print("MAC Address: ");
  Serial.println(WiFi.macAddress());
  Serial.println("Remote Access URLs:");
  Serial.println("Local: http://" + WiFi.localIP().toString() + "/");
  Serial.println("mDNS: http://" + String(OTA_HOSTNAME) + ".local/");
  Serial.println("========================\n");
}

void setupWiFi() {
  WiFi.begin(ssid, password);
  WiFi.setHostname(OTA_HOSTNAME);
  
  Serial.print("Connecting to WiFi");
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 30) {
    delay(1000);
    Serial.print(".");
    attempts++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nConnected to WiFi!");
    Serial.print("SSID: ");
    Serial.println(WiFi.SSID());
    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());
    Serial.print("Signal Strength: ");
    Serial.print(WiFi.RSSI());
    Serial.println(" dBm");
  } else {
    Serial.println("\nFailed to connect to WiFi");
  }
}

void setupOTA() {
  ArduinoOTA.setHostname(OTA_HOSTNAME);
  ArduinoOTA.setPassword(OTA_PASSWORD);

  ArduinoOTA.onStart([]() {
    Serial.println("Starting OTA update...");
    stopWatering();
  });
  
  ArduinoOTA.onEnd([]() {
    Serial.println("\nOTA Update Complete!");
  });
  
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("OTA Progress: %u%%\r", (progress / (total / 100)));
  });
  
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("OTA Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR) Serial.println("End Failed");
  });
  
  ArduinoOTA.begin();
  Serial.println("OTA Update Service Ready");
}

void setupFileSystem() {
  if (SPIFFS.begin()) {
    Serial.println("File system mounted");
    loadSchedules();
  } else {
    Serial.println("Failed to mount file system");
  }
}

void setupWebServer() {
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send_P(200, "text/html", index_html);
  });

  server.on("/control", HTTP_GET, [](AsyncWebServerRequest *request) {
    String response = "OK";
    
    if (request->hasParam("zone") && request->hasParam("time")) {
      int zone = request->getParam("zone")->value().toInt();
      int time = request->getParam("time")->value().toInt();
      
      if (zone > 0 && time > 0) {
        pendingAction = true;
        actionType = "ZONE";
        actionZone = zone;
        actionDuration = time;
        actionStartTime = millis();
        response = "Starting zone " + String(zone) + " for " + String(time) + " minutes";
        Serial.println("Queued zone start: " + String(zone) + " for " + String(time) + " min");
      } else {
        pendingAction = true;
        actionType = "STOP";
        actionStartTime = millis();
        response = "Stopping all zones";
        Serial.println("Queued stop all zones");
      }
    }
    
    if (request->hasParam("program")) {
      int program = request->getParam("program")->value().toInt();
      pendingAction = true;
      actionType = "PROGRAM";
      actionProgram = program;
      actionStartTime = millis();
      response = "Starting program " + String(program);
      Serial.println("Queued program start: " + String(program));
    }
    
    request->send(200, "text/plain", response);
  });

  server.on("/status", HTTP_GET, [](AsyncWebServerRequest *request) {
    String systemStatus = "Ready";
    if (activeZone > 0) {
      systemStatus = "Watering";
    }
    
    String actionStatus = "";
    if (pendingAction) {
      unsigned long elapsed = (millis() - actionStartTime) / 1000;
      actionStatus = "Processing " + actionType;
      if (actionType == "ZONE") {
        actionStatus += " (Zone " + String(actionZone) + ")";
      } else if (actionType == "PROGRAM") {
        actionStatus += " (Program " + String(actionProgram) + ")";
      }
      actionStatus += " - " + String(elapsed) + "s";
    }
    
    // Calculate next schedule check
    static unsigned long lastScheduleCheck = 0;
    unsigned long nextCheck = 60 - ((millis() - lastScheduleCheck) / 1000);
    if (nextCheck > 60) nextCheck = 60;
    
    // Get current time and day for display
    String currentTime = getCurrentTimeString();
    int currentDay = getCurrentDayOfWeek();
    String currentDayName = getDayName(currentDay);
    
    String json = "{";
    json += "\"systemStatus\":\"" + systemStatus + "\",";
    json += "\"activeZone\":" + String(activeZone) + ",";
    json += "\"timeRemaining\":" + String(activeTimeRemaining) + ",";
    json += "\"actionStatus\":\"" + actionStatus + "\",";
    json += "\"ipAddress\":\"" + WiFi.localIP().toString() + "\",";
    json += "\"macAddress\":\"" + WiFi.macAddress() + "\",";
    json += "\"signalStrength\":" + String(WiFi.RSSI()) + ",";
    json += "\"scheduleCount\":" + String(schedules.size()) + ",";
    json += "\"currentTime\":\"" + currentTime + "\",";
    json += "\"currentDay\":\"" + currentDayName + "\",";
    json += "\"nextScheduleCheck\":" + String(nextCheck);
    json += "}";
    
    request->send(200, "application/json", json);
  });

  server.on("/schedules", HTTP_GET, [](AsyncWebServerRequest *request) {
    String json = "[";
    for (size_t i = 0; i < schedules.size(); i++) {
      if (i > 0) json += ",";
      json += "{";
      json += "\"zone\":" + String(schedules[i].zone) + ",";
      json += "\"duration\":" + String(schedules[i].duration) + ",";
      json += "\"startTime\":\"" + schedules[i].startTime + "\",";
      json += "\"days\":\"" + schedules[i].days + "\"";
      json += "}";
    }
    json += "]";
    
    request->send(200, "application/json", json);
  });

  server.on("/schedule", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (request->hasParam("delete")) {
      int index = request->getParam("delete")->value().toInt();
      if (index >= 0 && index < (int)schedules.size()) {
        schedules.erase(schedules.begin() + index);
        saveSchedules();
        Serial.println("Deleted schedule index: " + String(index));
      }
    } else if (request->hasParam("zone") && request->hasParam("duration") && 
               request->hasParam("time") && request->hasParam("days")) {
      Schedule newSchedule;
      newSchedule.zone = request->getParam("zone")->value().toInt();
      newSchedule.duration = request->getParam("duration")->value().toInt();
      newSchedule.startTime = request->getParam("time")->value();
      newSchedule.days = request->getParam("days")->value();
      newSchedule.enabled = true;
      
      schedules.push_back(newSchedule);
      saveSchedules();
      Serial.println("Added schedule: Zone " + String(newSchedule.zone) + 
                    " at " + newSchedule.startTime + " on " + newSchedule.days);
    }
    
    request->send(200, "text/plain", "OK");
  });

  server.on("/update", HTTP_POST, [](AsyncWebServerRequest *request) {
    request->send(200, "text/plain", "Update Success! Rebooting...");
  }, [](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
    // OTA handling is done by ArduinoOTA
  });

  server.begin();
  Serial.println("HTTP server started");
}

// ========== TIME FUNCTIONS WITH NTP ==========
String getCurrentTimeString() {
  timeClient.update();
  String formattedTime = timeClient.getFormattedTime();
  // Return only HH:MM
  return formattedTime.substring(0, 5);
}

int getCurrentDayOfWeek() {
  timeClient.update();
  return timeClient.getDay(); // 0=Sunday, 1=Monday, ..., 6=Saturday
}

String getDayName(int day) {
  const char* days[] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
  if (day >= 0 && day <= 6) {
    return String(days[day]);
  }
  return "Unknown";
}

// ========== SCHEDULE MANAGEMENT ==========
void loadSchedules() {
  if (!SPIFFS.exists(SCHEDULE_FILE)) {
    Serial.println("No schedule file found, starting fresh");
    return;
  }
  
  File file = SPIFFS.open(SCHEDULE_FILE, "r");
  if (!file) {
    Serial.println("Failed to open schedule file");
    return;
  }
  
  DynamicJsonDocument doc(4096);
  DeserializationError error = deserializeJson(doc, file);
  file.close();
  
  if (error) {
    Serial.println("Failed to parse schedule file");
    return;
  }
  
  schedules.clear();
  JsonArray array = doc.as<JsonArray>();
  for (JsonObject obj : array) {
    Schedule schedule;
    schedule.zone = obj["zone"];
    schedule.duration = obj["duration"];
    schedule.enabled = obj["enabled"];
    schedule.days = obj["days"].as<String>();
    schedule.startTime = obj["startTime"].as<String>();
    schedules.push_back(schedule);
  }
  
  Serial.println("Loaded " + String(schedules.size()) + " schedules");
}

void saveSchedules() {
  File file = SPIFFS.open(SCHEDULE_FILE, "w");
  if (!file) {
    Serial.println("Failed to open schedule file for writing");
    return;
  }
  
  DynamicJsonDocument doc(4096);
  JsonArray array = doc.to<JsonArray>();
  
  for (const Schedule& schedule : schedules) {
    JsonObject obj = array.createNestedObject();
    obj["zone"] = schedule.zone;
    obj["duration"] = schedule.duration;
    obj["enabled"] = schedule.enabled;
    obj["days"] = schedule.days;
    obj["startTime"] = schedule.startTime;
  }
  
  serializeJson(doc, file);
  file.close();
  
  Serial.println("Saved " + String(schedules.size()) + " schedules");
}

bool isScheduleActiveToday(const Schedule& schedule, int currentDay) {
  // Schedule days: "1111100" where 1=active, 0=inactive
  // Position 0=Monday, 1=Tuesday, ..., 6=Sunday
  if (schedule.days.length() != 7) return false;
  
  // Convert currentDay (0=Sunday, 1=Monday, ..., 6=Saturday) to schedule index (0=Monday, 6=Sunday)
  int scheduleIndex;
  if (currentDay == 0) { // Sunday
    scheduleIndex = 6;
  } else {
    scheduleIndex = currentDay - 1;
  }
  
  if (scheduleIndex >= 0 && scheduleIndex < 7) {
    return schedule.days.charAt(scheduleIndex) == '1';
  }
  return false;
}

bool shouldRunSchedule(const Schedule& schedule) {
  // Add any additional conditions here (rain sensor, manual override, etc.)
  return true;
}

void checkSchedules() {
  if (activeZone > 0 || pendingAction) {
    Serial.println("Schedule check skipped - activeZone: " + String(activeZone) + 
                  ", pendingAction: " + String(pendingAction));
    return;
  }
  
  String currentTime = getCurrentTimeString();
  int currentDay = getCurrentDayOfWeek();
  String currentDayName = getDayName(currentDay);
  
  // Debug output every minute
  static unsigned long lastDebugOutput = 0;
  if (millis() - lastDebugOutput > 60000) { // 1 minute
    Serial.println("=== SCHEDULE CHECK ===");
    Serial.println("Current Time: " + currentTime + ", Day: " + currentDayName + " (" + String(currentDay) + ")");
    Serial.println("Loaded schedules: " + String(schedules.size()));
    
    for (size_t i = 0; i < schedules.size(); i++) {
      const Schedule& schedule = schedules[i];
      bool activeToday = isScheduleActiveToday(schedule, currentDay);
      
      Serial.println("Schedule " + String(i) + ": Zone " + String(schedule.zone) + 
                    " at " + schedule.startTime + " on " + schedule.days +
                    " | Active Today: " + String(activeToday) +
                    " | Enabled: " + String(schedule.enabled) +
                    " | Time Match: " + String(schedule.startTime == currentTime));
    }
    lastDebugOutput = millis();
  }
  
  for (size_t i = 0; i < schedules.size(); i++) {
    const Schedule& schedule = schedules[i];
    bool activeToday = isScheduleActiveToday(schedule, currentDay);
    
    if (schedule.enabled && activeToday && schedule.startTime == currentTime) {
      
      Serial.println("🎯 SCHEDULE TRIGGERED: Zone " + String(schedule.zone) + 
                    " for " + String(schedule.duration) + " minutes");
      
      // Queue the schedule action
      pendingAction = true;
      actionType = "ZONE";
      actionZone = schedule.zone;
      actionDuration = schedule.duration;
      actionStartTime = millis();
      
      // Log the activation
      Serial.println("✅ Scheduled watering started: Zone " + String(schedule.zone) + 
                    " for " + String(schedule.duration) + " minutes");
      break; // Only run one schedule at a time
    }
  }
}

// ========== WATERING CONTROL ==========
void startWatering(int zone, int duration) {
  if (zone < 1 || zone > 48 || duration < 1 || duration > 240) {
    Serial.println("Invalid zone or duration");
    return;
  }
  
  Serial.println("Executing HunterStart for zone " + String(zone) + " for " + String(duration) + " minutes");
  
  // This will handle both zone activation and pump control via Hunter protocol
  HunterStart(zone, duration);
  
  activeZone = zone;
  activeTimeRemaining = duration;
  lastWateringUpdate = millis();
  
  Serial.println("Zone " + String(zone) + " started successfully");
}

void stopWatering() {
  if (activeZone > 0) {
    Serial.println("Executing HunterStop for zone " + String(activeZone));
    HunterStop(activeZone);
  } else {
    Serial.println("Stopping all zones (sending stop command)");
    // Send stop command to zone 1 as a general stop
    HunterStop(1);
  }
  
  activeZone = 0;
  activeTimeRemaining = 0;
  Serial.println("All zones stopped");
}

void updateWatering() {
  if (activeZone > 0 && activeTimeRemaining > 0) {
    unsigned long currentTime = millis();
    if (currentTime - lastWateringUpdate >= WATERING_UPDATE_INTERVAL) {
      activeTimeRemaining--;
      lastWateringUpdate = currentTime;
      
      Serial.println("Zone " + String(activeZone) + " - " + String(activeTimeRemaining) + " minutes remaining");
      
      if (activeTimeRemaining <= 0) {
        stopWatering();
      }
    }
  }
}

void processPendingActions() {
  if (pendingAction) {
    Serial.println("Processing pending action: " + actionType);
    
    if (actionType == "ZONE") {
      startWatering(actionZone, actionDuration);
    } else if (actionType == "STOP") {
      stopWatering();
    } else if (actionType == "PROGRAM") {
      Serial.println("Executing HunterProgram for program " + String(actionProgram));
      HunterProgram(actionProgram);
    }
    
    pendingAction = false;
    actionType = "";
    Serial.println("Action completed");
  }
}

// ========== MAIN LOOP ==========
void loop() {
  ArduinoOTA.handle();
  processPendingActions();
  updateWatering();
  
  static unsigned long lastScheduleCheck = 0;
  if (millis() - lastScheduleCheck >= 60000) { // Check every minute
    checkSchedules();
    lastScheduleCheck = millis();
  }
  
  // Update NTP time periodically
  timeClient.update();
  
  delay(100);
}