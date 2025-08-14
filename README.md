CYD ESP UniFi Touchscreen Network Mapper
This Arduino-based project turns a CYD ESP32 touchscreen into a live UniFi network mapping tool. It connects to the UniFi Controller API and shows:

Client connection details – which switch port each client is plugged into

Patch panel mapping – so you know exactly where to find the cable

Switch information – quick access for troubleshooting and network tracing

Perfect for IT closets, data centers, and field techs, this device eliminates the guesswork of tracing cables and speeds up network troubleshooting.

Features
Real-time UniFi API integration

Touch-friendly UI with CYD ESP32 + TFT_eSPI + XPT2046

Color-coded, easy-to-read interface

Simple Wi-Fi and API setup

Portable and standalone — no PC required

User Configuration
Before compiling, update the following in the .ino file:

cpp
Copy
Edit
// ——— USER CONFIGURATION —————————————
const char* rackName = "Rack Number";    // Friendly name for display
const uint8_t UI_ROTATION = 1;           // 1 = USB right, 3 = USB left
const char* ssid = "WIFINAME";           // Wi-Fi SSID
const char* password = "WIFIPASS";       // Wi-Fi password
const char* apiKey = "APIKEY";           // UniFi API key
const char* host = "192.168.2.1";        // UniFi Gateway IP
const char* siteId = "default";          // UniFi site ID
Hardware Requirements
CYD ESP32 with built-in touchscreen

TFT_eSPI display driver

XPT2046 touch controller

Access to a UniFi Controller with API key

Libraries Needed
Install in Arduino IDE Library Manager:

TFT_eSPI

XPT2046_Touchscreen

ArduinoJson

WiFi

WiFiClientSecure

HTTPClient

How It Works
On startup, the device connects to your Wi-Fi and UniFi Controller.

The touchscreen displays connected clients and their switch port.

Tap through the interface to view switch details and patch panel mapping.
