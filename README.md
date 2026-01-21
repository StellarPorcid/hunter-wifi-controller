# Hunter Irrigation Controller - WiFi Upgrade

A smart WiFi irrigation controller for Hunter X-Core systems using ESP8266/ESP32, featuring web-based control, scheduling, and OTA updates.

## ✨ Features
* **Web Interface:** Control zones manually via any browser (mobile friendly).
* **Smart Scheduling:** Set automated watering schedules with day-specific patterns.
* **OTA Updates:** Wireless firmware updates.
* **Multi-Platform:** Works with ESP8266, ESP32-C3 Mini, and ESP32-S3 Mini.
* **Real-time Clock:** NTP-based accurate timekeeping.

## 📋 Prerequisites

### Hardware
* **Controller:** ESP8266 (NodeMCU/Wemos) OR ESP32-C3/S3 Mini
* **Hunter X-Core Irrigation Controller**
* **Power:** 5V Power Supply
* Jumper Wires

### Software Libraries
These can be installed via the Arduino IDE Library Manager:
* ArduinoJSON
* ESPAsyncWebServer
* AsyncTCP (for ESP32) or ESPAsyncTCP (for ESP8266)
* NTPClient

## 🔧 Wiring Setup

The Hunter protocol handles both zone selection and pump control through a single data line.

| ESP Pin | Hunter Terminal |
| :--- | :--- |
| **GPIO** | **COMMON** |
| **GND** | **GND** |
| **5V** | **External 5V PSU** |

**Recommended GPIO Pins:**
* **ESP8266:** D1, D2, D5, D6, D7
* **ESP32-C3:** GPIO 2, 3, 4, 5
* **ESP32-S3:** GPIO 1, 2, 3, 4, 5

*Note: Update `hunter.h` with your chosen pin: `#define HUNTER_PIN 2`*

## ⚙️ Configuration

1.  Open `HunterController.ino`.
2.  Update your WiFi credentials:
    ```cpp
    const char* ssid = "YOUR_WIFI_SSID";
    const char* password = "YOUR_WIFI_PASSWORD";
    ```
3.  Set your Timezone offset in `setup()`:
    ```cpp
    timeClient.setTimeOffset(2 * 3600); // UTC+2 (adjust as needed)
    ```

## 🌐 How to Use

1.  **Access:** Navigate to `http://[ESP_IP_ADDRESS]/` or `http://hunter-irrigation.local/`
2.  **Manual Control:** Select a zone and duration, then click "Start Zone".
3.  **Scheduling:**
    * **Days Format:** `1111100` (Monday to Friday Active, Sat/Sun Inactive).
    * 1 = Active, 0 = Inactive.

## 📄 License
This project is open source. Hunter protocol reverse engineering based on community research.
