@ -1,2 +1,105 @@
# cyd-esp-unifi-touch
 An Arduino-based project for the CYD ESP32 touchscreen that connects to the UniFi Controller API to display real-time client and switch information.
# CYD ESP UniFi Touchscreen Network Mapper

> **Touchscreen UniFi Network Port Mapper for CYD ESP32** – Instantly see which switch port and patch panel a UniFi client is connected to, right from a handheld touchscreen.

![CYD ESP32](docs/device-photo.jpg) <!-- Replace with actual photo path if available -->

---

## 📖 Overview

This Arduino-based project turns a **CYD ESP32 touchscreen** into a live UniFi network mapping tool. It connects to the UniFi Controller API and displays:

- **Client connection details** – which switch port each client is plugged into  
- **Patch panel mapping** – instantly locate cables  
- **Switch information** – quick access for troubleshooting and network tracing  

Perfect for **IT closets, data centers, and field techs**, this device eliminates the guesswork of tracing cables and speeds up network troubleshooting.

---

## ✨ Features

- Real-time UniFi API integration  
- Touch-friendly UI (CYD ESP32 + TFT_eSPI + XPT2046)  
- Color-coded, easy-to-read interface  
- Simple Wi-Fi and API setup  
- Portable and standalone — no PC required

---

## ⚙️ User Configuration

Before compiling, update these values in the `.ino` file:

```cpp
// ——— USER CONFIGURATION —————————————
const char* rackName = "Rack Number";    // Friendly name for display
const uint8_t UI_ROTATION = 1;           // 1 = USB right, 3 = USB left
const char* ssid = "WIFINAME";           // Wi-Fi SSID
const char* password = "WIFIPASS";       // Wi-Fi password
const char* apiKey = "APIKEY";           // UniFi API key
const char* host = "192.168.2.1";        // UniFi Gateway IP
const char* siteId = "default";          // UniFi site ID
```

> ⚠️ **Security note:** Keep your API key private. If sharing code publicly, remove or replace it with a placeholder.

---

## 🛠 Hardware Requirements

- CYD ESP32 with built-in touchscreen  
- TFT_eSPI display driver  
- XPT2046 touch controller  
- Access to a UniFi Controller with API key

---

## 📦 Required Libraries

Install via Arduino IDE Library Manager:

- `TFT_eSPI`
- `XPT2046_Touchscreen`
- `ArduinoJson`
- `WiFi`
- `WiFiClientSecure`
- `HTTPClient`

---

## 🚀 How It Works

1. On startup, the device connects to your Wi-Fi and UniFi Controller.  
2. The touchscreen displays connected clients and their switch port.  
3. Tap through the interface to view switch details and patch panel mapping.  

---

## 📸 Screenshots

| Home Screen | Client Details | Patch Panel Mapping |
|-------------|----------------|---------------------|
| ![Home](docs/home.jpg) | ![Client](docs/client.jpg) | ![Patch](docs/patch.jpg) |

---

## 📄 License

This project is licensed under the MIT License — see the [LICENSE](LICENSE) file for details.

---

## 🤝 Contributing

Contributions, issues, and feature requests are welcome!  
Feel free to check [issues page](../../issues) if you want to contribute.

---

## ⭐ Acknowledgements

- Built with Arduino IDE  
- Uses UniFi Controller API  
- UI powered by TFT_eSPI and XPT2046  
