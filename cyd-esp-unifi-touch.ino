struct Btn;  // forward declare for Arduino's auto-prototype
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>
#include <SPIFFS.h>
#include <vector>

using namespace fs;

// ─── THEME COLORS ─────────────────────────────────────────────────────────────
static constexpr uint16_t COLOR_BG = 0x1082;      // #121212 dark background
static constexpr uint16_t COLOR_PANEL = 0x530D;   // #54626F new panel/card bg
static constexpr uint16_t COLOR_ACCENT = 0x05FA;  // #00BCD4 accent

// ——— USER CONFIGURATION ——————————————————————————————————————————————
const char* rackName = "Rack Number";  // ← change your rack name here
const uint8_t UI_ROTATION = 1;    // set to 3 for usb connector on the left or 1 for usb connector on the right
const char* ssid = "WIFINAME"; // wifi name 
const char* password = "WIFIPASS"; // wifi password
const char* apiKey = "APIKEY"; // unifi api key 
const char* host = "192.168.2.1"; // ip of the gateway
const char* siteId = "default"; // the siteId 

// ——— Display & Touch pins ————————————————————————————————————————
#define BL_PIN 21
#define TOUCH_CS 33
#define TOUCH_IRQ 36
#define TOUCH_CLK 25
#define TOUCH_MISO 39
#define TOUCH_MOSI 32

#define TS_MINX 200
#define TS_MAXX 3800
#define TS_MINY 200
#define TS_MAXY 3800

TFT_eSPI tft = TFT_eSPI();
SPIClass touchSPI(VSPI);
XPT2046_Touchscreen ts(TOUCH_CS, TOUCH_IRQ);
WiFiClientSecure client;

// navigation button
const int NAV_W = 30;  // Up/Down button square
// back-button 75%
const int BACK_W = 30;  // 40 * 0.75
const int BACK_H = 24;  // 32 * 0.75

// layout
const int HEADER_H = 32;
const int MARGIN = 8;
const int CARD_R = 6;
const int CARD_H_SWITCH = 40;
const int CARD_H_CLIENT = 45;

// data models
struct SwitchInfo {
  String id, mac, name;
  uint16_t usedPorts, totalPorts;
};
struct ClientInfo {
  String macAddr, name, ip;
  uint16_t port;
  String patchPanel;
};

std::vector<SwitchInfo> switches;
std::vector<std::vector<ClientInfo>> allClients;

unsigned long lastTapMs = 0;
unsigned long lastRefreshMs = 0;
const unsigned long TAP_BLOCK_MS = 180;

// UI state
enum Screen {
  SCR_HOME,
  SCR_SETTINGS,
  SCR_DETAIL,
  SCR_SWITCH_SETTINGS,
  SCR_CLIENT_SETTINGS,
  SCR_EDIT_SW_PORT,
  SCR_EDIT_CL_PORT,
  SCR_EDIT_PP_LETTER,
  SCR_EDIT_PP_NUMBER
} screen = SCR_HOME;  // ✅ important

int selectedSwitch = -1;
int selectedClient = -1;
int scrollHome = 0;
int scrollDetail = 0;

inline void applyRotation() {
  tft.setRotation(UI_ROTATION);
  ts.setRotation(UI_ROTATION);
}

// edit state
bool editingSwPort = false;
String swPortInput;
bool editingClPort = false;
String clPortInput;
bool editingPpLetter = false;
String ppLetter;
bool editingPpNumber = false;
String ppNumberInput;

// numeric pad
static const char* numpad[4][3] = {
  { "1", "2", "3" },
  { "4", "5", "6" },
  { "7", "8", "9" },
  { "DEL", "0", "OK" }
};
// patch‐panel letters (add "None")
static const char* panels[2][3] = {
  { "A", "B", "C" },
  { "D", "None", "OK" }
};

// JSON docs
StaticJsonDocument<16 * 1024> settingsDoc;
DynamicJsonDocument clientsDoc(32 * 1024),
  devicesDoc(32 * 1024);

// forwards
void loadSettings(), saveSettings();
String httpsGet(const String&);
bool fetchJson(const String&, DynamicJsonDocument&);
void updateNetworkData(), prefetchClients();
void drawHome(), drawSettings(), drawDetail(), drawSwitchSettings();
void drawClientSettings();
void drawEditSwPortPad(), drawEditClPortPad();
void drawPanelLetterPad(), drawPanelNumberPad();
void handleTouch();
void drawRefresh(int, int), drawBack(int, int);
void bootScreenInit();
void bootStatus(const char*);

// forward helpers for Settings UI
void drawTogglePill(struct Btn& b, int x, int y, int w, int h, bool on);
void drawCardToggle(struct Btn& toggleBtn, int x, int y, int w, const char* label, bool on);
void drawAccentButton(struct Btn& b, int x, int y, int w, const char* label);
void drawProgressSheet(const char* title, int percent);
void clearCacheWithProgress();

void refreshWithProgress();
void drawProgressHUD(const char* title, int percent);


// ─── Tap debounce ─────────────────────────────────────────────────────────────
bool touchHeld = false;
unsigned long lastTouchUpMs = 0;
const unsigned long TAP_DEBOUNCE_MS = 180;  // tweak 120–250ms if you like

// ─── Screen sleep ─────────────────────────────────────────────────────────────
unsigned long lastInputMs = 0;
const unsigned long SCREEN_TIMEOUT_MS = 3UL * 60UL * 1000UL;  // 3 minutes
bool screenOff = false;

inline void setBacklight(bool on) {
  digitalWrite(BL_PIN, on ? HIGH : LOW);
}

void redrawCurrentScreen() {
  switch (screen) {
    case SCR_HOME: drawHome(); break;
    case SCR_SETTINGS: drawSettings(); break;
    case SCR_DETAIL: drawDetail(); break;
    case SCR_SWITCH_SETTINGS: drawSwitchSettings(); break;
    case SCR_CLIENT_SETTINGS: drawClientSettings(); break;
    case SCR_EDIT_SW_PORT: drawEditSwPortPad(); break;
    case SCR_EDIT_CL_PORT: drawEditClPortPad(); break;
    case SCR_EDIT_PP_LETTER: drawPanelLetterPad(); break;
    case SCR_EDIT_PP_NUMBER: drawPanelNumberPad(); break;
  }
}

inline void markActivity() {
  lastInputMs = millis();
  if (screenOff) {
    screenOff = false;
    setBacklight(true);
    redrawCurrentScreen();
  }
}

// ─── Header text buttons ──────────────────────────────────────────────────────
struct Btn {
  int x, y, w, h;
};
static Btn BTN_REFRESH{ 0, 0, 0, 0 }, BTN_SETTINGS{ 0, 0, 0, 0 };
static Btn BTN_TOGGLE_IP{ 0, 0, 0, 0 }, BTN_TOGGLE_MAC{ 0, 0, 0, 0 }, BTN_CLEAR_CACHE{ 0, 0, 0, 0 };


static inline bool hit(const Btn& b, int x, int y) {
  return x >= b.x && x < b.x + b.w && y >= b.y && y < b.y + b.h;
}

void drawTextButton(Btn& b, int x, int y, const char* label) {
  tft.setTextSize(1);
  tft.setTextColor(TFT_WHITE, COLOR_PANEL);
  const int padX = 6;
  const int th = tft.fontHeight();
  const int tw = tft.textWidth(label);
  b.x = x;
  b.y = y;
  b.w = tw + padX * 2;
  b.h = max(BACK_H, th + 6);
  tft.fillRoundRect(b.x, b.y, b.w, b.h, CARD_R, COLOR_PANEL);
  tft.setCursor(b.x + (b.w - tw) / 2, b.y + (b.h - th) / 2);
  tft.print(label);
}

void drawHeaderButtons(bool showRefresh) {
  tft.setTextSize(1);
  int xRight = tft.width() - MARGIN;

  // Settings button (always)
  int sw = tft.textWidth("Settings") + 12;
  int sx = xRight - sw;
  drawTextButton(BTN_SETTINGS, sx, 0, "Settings");

  // Refresh button (home only)
  if (showRefresh) {
    int rw = tft.textWidth("Refresh") + 12;
    int rx = sx - MARGIN - rw;
    drawTextButton(BTN_REFRESH, rx, 0, "Refresh");
  } else {
    // disable hit-area so taps don’t accidentally trigger
    BTN_REFRESH = { 0, 0, 0, 0 };
  }
}

void drawProgressHUD(const char* title, int percent) {
  // modal-ish card in the center with a progress bar
  int w = tft.width() - 2 * MARGIN - 20;
  int h = 60;
  int x = (tft.width() - w) / 2;
  int y = (tft.height() - h) / 2;

  tft.fillRoundRect(x, y, w, h, CARD_R, COLOR_BG);
  tft.drawRoundRect(x, y, w, h, CARD_R, TFT_WHITE);

  tft.setTextSize(1);
  tft.setTextColor(TFT_WHITE, COLOR_BG);
  tft.setCursor(x + 12, y + 8);
  tft.print(title);

  int bw = w - 24;
  int bh = 16;
  int bx = x + 12;
  int by = y + h - bh - 10;

  tft.drawRoundRect(bx, by, bw, bh, 6, COLOR_PANEL);
  int fw = map(constrain(percent, 0, 100), 0, 100, 0, bw - 2);
  tft.fillRoundRect(bx + 1, by + 1, fw, bh - 2, 5, COLOR_ACCENT);
}

void refreshWithProgress() {
  drawProgressHUD("Refreshing…", 10);
  delay(50);  // let the HUD paint

  drawProgressHUD("Refreshing…  contacting controller", 20);
  updateNetworkData();  // fetch devices/clients index

  drawProgressHUD("Refreshing…  building client lists", 65);
  prefetchClients();  // build vectors

  drawProgressHUD("Refreshing…  done", 100);
  delay(150);
  lastRefreshMs = millis();
  drawHome();
}

// 15% shorter client cards on the Detail list
inline int clientCardH() {
  return (CARD_H_CLIENT * 85) / 100;
}

// ──────────────────────────────────────────────────────────────────────────────
// — HTTP & JSON
String httpsGet(const String& url) {
  HTTPClient http;
  http.begin(client, url);
  http.addHeader("X-API-Key", apiKey);
  http.addHeader("Accept", "application/json");
  int code = http.GET();
  String b = http.getString();
  http.end();
  Serial.printf("GET %s → %d bytes, code %d\n", url.c_str(), b.length(), code);
  return code == 200 ? b : String();
}
bool fetchJson(const String& url, DynamicJsonDocument& doc) {
  String b = httpsGet(url);
  if (b.isEmpty()) return false;
  auto err = deserializeJson(doc, b);
  if (err != DeserializationError::Ok) {
    Serial.print("JSON err: ");
    Serial.println(err.c_str());
    return false;
  }
  return true;
}

// — persistence
void loadSettings() {
  File f = SPIFFS.open("/settings.json", "r");
  if (!f) {
    settingsDoc["rackName"] = rackName;
    settingsDoc["showIP"] = true;
    settingsDoc["showMAC"] = true;
    saveSettings();
    return;
  }
  if (deserializeJson(settingsDoc, f) != DeserializationError::Ok) {
    settingsDoc.clear();
    settingsDoc["rackName"] = rackName;
    settingsDoc["showIP"] = true;
    settingsDoc["showMAC"] = true;
    saveSettings();
  }
  f.close();
}
void saveSettings() {
  // Keep rackName in sync before writing
  if (rackName && rackName[0]) {
    settingsDoc["rackName"] = rackName;
  }
  File f = SPIFFS.open("/settings.json", "w");
  if (!f) {
    Serial.println("⚠ save failed");
    return;
  }
  serializeJson(settingsDoc, f);
  f.close();
}


// — build switch list (only wired)
void updateNetworkData() {
  switches.clear();
  fetchJson("https://" + String(host) + "/proxy/network/integration/v1/sites/" + siteId + "/clients?limit=200", clientsDoc);
  fetchJson("https://" + String(host) + "/proxy/network/integration/v1/sites/" + siteId + "/devices", devicesDoc);
  for (auto d : devicesDoc["data"].as<JsonArray>()) {
    bool isSw = false;
    for (auto f : d["features"].as<JsonArray>())
      if (String(f | "") == "switching") {
        isSw = true;
        break;
      }
    if (!isSw) continue;
    SwitchInfo s;
    s.id = d["id"] | d["_id"] | "";
    s.mac = d["mac"] | "";
    s.name = d["name"] | d["model"] | s.mac;
    s.totalPorts = d["num_ports"] | 0;
    int cnt = 0;
    for (auto c : clientsDoc["data"].as<JsonArray>()) {
      if (String(c["type"] | "") != "WIRED") continue;
      if (String(c["uplinkDeviceId"] | "") != s.id) continue;
      cnt++;
    }
    s.usedPorts = cnt;
    switches.push_back(s);
  }
}

// — cache clients + include APs in count
void prefetchClients() {
  allClients.clear();
  for (int i = 0; i < switches.size(); i++) {
    String swId = switches[i].id;
    std::vector<ClientInfo> vec;
    // APs
    for (JsonObject d : devicesDoc["data"].as<JsonArray>()) {
      bool isAP = false;
      for (auto f : d["features"].as<JsonArray>())
        if (String(f | "").equalsIgnoreCase("accessPoint")) {
          isAP = true;
          break;
        }
      if (!isAP) continue;
      DynamicJsonDocument dt(8192);
      String url = "https://" + String(host)
                   + "/proxy/network/integration/v1/sites/" + siteId
                   + "/devices/" + String(d["id"].as<const char*>());
      if (!fetchJson(url, dt)) continue;
      if (String(dt["uplink"]["deviceId"] | "") != swId) continue;
      ClientInfo ci;
      ci.macAddr = String(d["mac"] | "");
      ci.name = String(d["name"] | d["model"] | "AP");
      ci.ip = String(dt["ipAddress"] | d["ipAddress"] | "No IP");
      ci.port = settingsDoc["switchSettings"][swId]["maxPorts"]
                | switches[i].totalPorts;
      ci.patchPanel = settingsDoc["clientSettings"][ci.macAddr]["patchPanel"] | "—";
      vec.push_back(ci);
    }
    // wired
    for (JsonObject c : clientsDoc["data"].as<JsonArray>()) {
      if (String(c["type"] | "") != "WIRED") continue;
      if (String(c["uplinkDeviceId"] | "") != swId) continue;
      ClientInfo ci;
      ci.macAddr = String(c["macAddress"] | c["mac"] | "");
      ci.name = String(c["name"] | c["hostname"] | "Client");
      ci.ip = String(c["ipAddress"] | "No IP");
      ci.port = settingsDoc["clientSettings"][ci.macAddr]["port"]
                | settingsDoc["switchSettings"][swId]["maxPorts"]
                | switches[i].totalPorts;
      ci.patchPanel = settingsDoc["clientSettings"][ci.macAddr]["patchPanel"] | "—";
      vec.push_back(ci);
    }
    switches[i].usedPorts = vec.size();
    allClients.push_back(vec);
  }
  selectedSwitch = -1;
}

// ─── icon helpers (kept for back/refresh symbol if needed) ────────────────────
void drawRefresh(int x, int y) {
  tft.fillRoundRect(x, y, BACK_W, BACK_H, CARD_R, COLOR_PANEL);
  int cx = x + BACK_W / 2, cy = y + BACK_H / 2, R = 10;
  for (int a = -120; a <= 90; a += 6) {
    float r0 = a * PI / 180, r1 = (a + 6) * PI / 180;
    int x0 = cx + cos(r0) * R, y0 = cy + sin(r0) * R;
    int x1 = cx + cos(r1) * R, y1 = cy + sin(r1) * R;
    tft.drawLine(x0, y0, x1, y1, COLOR_ACCENT);
  }
  float a = 90 * PI / 180;
  int ax = cx + cos(a) * R, ay = cy + sin(a) * R;
  tft.fillTriangle(ax, ay, ax - 4, ay - 4, ax + 4, ay - 4, COLOR_ACCENT);
}

void drawBack(int x, int y) {
  tft.fillRoundRect(x, y, BACK_W, BACK_H, CARD_R, COLOR_PANEL);
  tft.fillTriangle(x + BACK_W / 4, y + BACK_H / 2,
                   x + BACK_W * 3 / 4, y + BACK_H / 4,
                   x + BACK_W * 3 / 4, y + BACK_H * 3 / 4,
                   COLOR_ACCENT);
}

// — draw home ──────────────────────────────────────────────────────────────────
void drawHome() {
  screen = SCR_HOME;

  // Clear header + body first
  tft.fillRect(0, 0, tft.width(), HEADER_H, COLOR_BG);
  tft.fillRect(0, HEADER_H, tft.width(), tft.height() - HEADER_H, COLOR_BG);

  // Draw header buttons first so BTN_REFRESH has real coordinates
  drawHeaderButtons(true);

  // Top-right "last updated", placed to the LEFT of the Refresh button
  tft.setTextSize(1);
  tft.setTextColor(TFT_WHITE, COLOR_BG);
  if (lastRefreshMs) {
    unsigned long secs = (millis() - lastRefreshMs) / 1000;
    String ago = String(secs / 60) + "m " + String(secs % 60) + "s ago";
    int tw = tft.textWidth(ago);
    int rightEdge = BTN_REFRESH.x - MARGIN;  // space ends just before Refresh
    int ax = max(MARGIN, rightEdge - tw);    // keep inside the header
    int ay = 2;                              // <— above the rack name
    tft.setCursor(ax, ay);
    tft.print(ago);
  }

  // Rack name (stays on the next line, left side)
  tft.setTextSize(2);
  tft.setTextColor(TFT_WHITE, COLOR_BG);
  tft.setCursor(MARGIN, MARGIN + 4);
  tft.print(settingsDoc["rackName"].as<String>());


  // Nav arrows...
  int perPage = 4, total = switches.size();
  if (total > perPage) {
    int ax = tft.width() - NAV_W - MARGIN;
    int ay = HEADER_H + MARGIN, by = ay + NAV_W + MARGIN;
    tft.fillTriangle(ax + NAV_W / 2, ay + 6, ax + 6, ay + NAV_W - 6, ax + NAV_W - 6, ay + NAV_W - 6, TFT_WHITE);
    tft.fillTriangle(ax + 6, by + 6, ax + NAV_W - 6, by + 6, ax + NAV_W / 2, by + NAV_W - 6, TFT_WHITE);
  }

  // Switch cards
  int y0 = HEADER_H + MARGIN;
  for (int i = 0; i < perPage; i++) {
    int idx = scrollHome + i;
    if (idx >= total) break;
    int y = y0 + i * (CARD_H_SWITCH + MARGIN);

    tft.fillRoundRect(
      MARGIN, y,
      tft.width() - 2 * MARGIN - NAV_W - 2,
      CARD_H_SWITCH,
      CARD_R,
      COLOR_PANEL);

    int textY = y + (CARD_H_SWITCH - 8) / 2;
    tft.setTextSize(1);
    tft.setTextColor(TFT_WHITE, COLOR_PANEL);
    tft.setCursor(MARGIN + 6, textY);
    tft.print(switches[idx].name);

    uint16_t maxP = settingsDoc["switchSettings"][switches[idx].id]["maxPorts"]
                    | switches[idx].totalPorts;
    String pct = String(switches[idx].usedPorts) + "/" + String(maxP);
    int tw = tft.textWidth(pct);
    tft.setCursor(tft.width() - MARGIN - NAV_W - 2 - tw - 6, textY);
    tft.print(pct);
  }
}

void drawTogglePill(Btn& b, int x, int y, int w, int h, bool on) {
  int r = h / 2;
  tft.fillRoundRect(x, y, w, h, r, on ? COLOR_ACCENT : COLOR_BG);
  tft.drawRoundRect(x, y, w, h, r, TFT_WHITE);
  int knobR = r - 2;
  int cx = on ? (x + w - knobR - 2) : (x + knobR + 2);
  int cy = y + h / 2;
  tft.fillCircle(cx, cy, knobR, on ? COLOR_BG : TFT_WHITE);
  b.x = x;
  b.y = y;
  b.w = w;
  b.h = h;
}

void drawCardToggle(Btn& toggleBtn, int x, int y, int w, const char* label, bool on) {
  tft.fillRoundRect(x, y, w, CARD_H_SWITCH, CARD_R, COLOR_PANEL);
  tft.setTextSize(2);
  tft.setTextColor(TFT_WHITE, COLOR_PANEL);
  int th = tft.fontHeight();
  tft.setCursor(x + 10, y + (CARD_H_SWITCH - th) / 2);
  tft.print(label);

  const int tw = 52, thp = 22;
  int tx = x + w - tw - 10;
  int ty = y + (CARD_H_SWITCH - thp) / 2;
  drawTogglePill(toggleBtn, tx, ty, tw, thp, on);
}

void drawAccentButton(Btn& b, int x, int y, int w, const char* label) {
  tft.fillRoundRect(x, y, w, CARD_H_SWITCH, CARD_R, COLOR_ACCENT);
  tft.setTextSize(2);
  tft.setTextColor(TFT_BLACK, COLOR_ACCENT);
  int th = tft.fontHeight();
  int tw = tft.textWidth(label);
  tft.setCursor(x + (w - tw) / 2, y + (CARD_H_SWITCH - th) / 2);
  tft.print(label);
  b.x = x;
  b.y = y;
  b.w = w;
  b.h = CARD_H_SWITCH;
}

void clearCacheAndRebuild() {
  // remove any saved per-switch & per-client overrides
  settingsDoc.remove("switchSettings");
  settingsDoc.remove("clientSettings");
  saveSettings();

  // rebuild in-RAM client list so ports/panels fall back to defaults
  prefetchClients();

  // tiny toast
  tft.setTextSize(1);
  tft.setTextColor(TFT_WHITE, COLOR_BG);
  tft.fillRect(MARGIN, tft.height() - 20, tft.width() - 2 * MARGIN, 16, COLOR_BG);
  tft.setCursor(MARGIN, tft.height() - 18);
  tft.print("Cache cleared.");
}


// small bottom sheet progress bar
void drawProgressSheet(const char* title, int percent) {
  int w = tft.width() - 2 * MARGIN;
  int h = 62;
  int x = MARGIN;
  int y = tft.height() - h - MARGIN;
  tft.fillRoundRect(x, y, w, h, CARD_R, COLOR_PANEL);
  tft.drawRoundRect(x, y, w, h, CARD_R, TFT_WHITE);

  tft.setTextSize(1);
  tft.setTextColor(TFT_WHITE, COLOR_PANEL);
  tft.setCursor(x + 10, y + 8);
  tft.print(title);

  int barX = x + 10, barY = y + 30, barW = w - 20, barH = 18;
  tft.drawRoundRect(barX, barY, barW, barH, barH / 2, TFT_WHITE);
  int fillW = map(percent, 0, 100, 0, barW - 2);
  tft.fillRoundRect(barX + 1, barY + 1, fillW, barH - 2, (barH - 2) / 2, COLOR_ACCENT);
}

void clearCacheWithProgress() {
  drawProgressSheet("Clearing cache… (0%)", 0);
  delay(80);
  settingsDoc.remove("switchSettings");
  drawProgressSheet("Removing switch ports…", 30);
  delay(80);
  settingsDoc.remove("clientSettings");
  drawProgressSheet("Removing client data…", 60);
  delay(80);
  saveSettings();
  drawProgressSheet("Saving settings…", 80);
  delay(80);
  prefetchClients();
  drawProgressSheet("Rebuilding list…", 95);
  delay(120);
  drawProgressSheet("Done.", 100);
  delay(250);
}


void drawSettings() {
  screen = SCR_SETTINGS;
  tft.fillScreen(COLOR_BG);

  // header
  drawBack(MARGIN, MARGIN);
  tft.setTextSize(2);
  tft.setTextColor(TFT_WHITE, COLOR_BG);
  tft.setCursor(MARGIN * 2 + BACK_W, 4);
  tft.print("Settings");

  bool showIP = settingsDoc["showIP"] | true;
  bool showMAC = settingsDoc["showMAC"] | true;

  // stacked cards: full width
  int fullW = tft.width() - 2 * MARGIN;
  int y1 = HEADER_H + MARGIN;
  int y2 = y1 + CARD_H_SWITCH + MARGIN;

  drawCardToggle(BTN_TOGGLE_IP, MARGIN, y1, fullW, "Show IP", showIP);
  drawCardToggle(BTN_TOGGLE_MAC, MARGIN, y2, fullW, "Show MAC", showMAC);

  // Clear cache
  int y3 = y2 + CARD_H_SWITCH + 2 * MARGIN;
  drawAccentButton(BTN_CLEAR_CACHE, MARGIN, y3, fullW, "Clear cached ports");
}



void drawDetail() {
  screen = SCR_DETAIL;
  tft.fillScreen(COLOR_BG);
  tft.setTextSize(1);
  tft.setTextColor(TFT_WHITE);

  drawBack(MARGIN, MARGIN);
  int xRight = tft.width() - MARGIN;
  int sw = tft.textWidth("Settings") + 12;
  drawTextButton(BTN_SETTINGS, xRight - sw, 0, "Settings");

  tft.setCursor(MARGIN * 2 + BACK_W, 8);
  tft.print(switches[selectedSwitch].name);

  int perPage = 4, total = allClients[selectedSwitch].size();
  if (total > perPage) {
    int ax = tft.width() - NAV_W - MARGIN, ay = HEADER_H + MARGIN, by = ay + NAV_W + MARGIN;
    tft.fillTriangle(ax + NAV_W / 2, ay + 6, ax + 6, ay + NAV_W - 6, ax + NAV_W - 6, ay + NAV_W - 6, TFT_WHITE);
    tft.fillTriangle(ax + 6, by + 6, ax + NAV_W - 6, by + 6, ax + NAV_W / 2, by + NAV_W - 6, TFT_WHITE);
  }

  int cardH = clientCardH();  // <-- new height
  int listAreaW = tft.width() - 2 * MARGIN - NAV_W - 2;
  int cardW = (listAreaW * 85) / 100;  // keep your 15% narrower width if you want

  int y0 = HEADER_H + MARGIN;
  for (int i = 0; i < perPage; i++) {
    int idx = scrollDetail + i;
    if (idx >= total) break;
    int y = y0 + i * (cardH + MARGIN);

    tft.fillRoundRect(MARGIN, y, cardW, cardH, CARD_R, COLOR_PANEL);

    int textY = y + (cardH - 8) / 2;
    tft.setTextColor(TFT_WHITE, COLOR_PANEL);
    tft.setCursor(MARGIN + 6, textY);
    tft.print(allClients[selectedSwitch][idx].name);

    String tag = "P:" + String(allClients[selectedSwitch][idx].port)
                 + " PP-" + allClients[selectedSwitch][idx].patchPanel;
    int tw = tft.textWidth(tag);
    tft.setCursor(MARGIN + cardW - tw - 6, textY);
    tft.print(tag);

    if (settingsDoc["showIP"]) {
      tft.setCursor(MARGIN + 6, textY + 10);
      tft.print(allClients[selectedSwitch][idx].ip);
    }
    if (settingsDoc["showMAC"]) {
      String macs = allClients[selectedSwitch][idx].macAddr;
      int mx = MARGIN + cardW - tft.textWidth(macs) - 6;
      tft.setCursor(mx, textY + 10);
      tft.print(macs);
    }
  }
}


// — boot screen (simple) ───────────────────────────────────────────────────────
void bootScreenInit() {
  tft.fillScreen(COLOR_BG);
  tft.setTextColor(TFT_WHITE, COLOR_BG);

  tft.setTextSize(2);
  int tw = tft.textWidth("Starting...");
  tft.setCursor((tft.width() - tw) / 2, HEADER_H / 2);
  tft.print("Starting...");

  tft.fillRect(MARGIN, tft.height() - 24, tft.width() - 2 * MARGIN, 16, COLOR_BG);
}
void bootStatus(const char* status) {
  tft.setTextSize(1);
  tft.setTextColor(TFT_WHITE, COLOR_BG);
  tft.fillRect(MARGIN, tft.height() - 24, tft.width() - 2 * MARGIN, 16, COLOR_BG);
  tft.setCursor(MARGIN, tft.height() - 20);
  tft.print(status);
}

// — draw switch settings ───────────────────────────────────────────────────────
void drawSwitchSettings() {
  screen = SCR_SWITCH_SETTINGS;
  tft.fillScreen(COLOR_BG);
  tft.setTextSize(1);
  tft.setTextColor(TFT_WHITE);

  drawBack(MARGIN, MARGIN);

  tft.setCursor(MARGIN * 2 + BACK_W, 8);
  tft.print(String("Settings: ") + switches[selectedSwitch].name);

  int y1 = HEADER_H + MARGIN;
  tft.fillRoundRect(MARGIN, y1, tft.width() - 2 * MARGIN, CARD_H_SWITCH, CARD_R, COLOR_PANEL);

  int th = tft.fontHeight();
  int textY = y1 + (CARD_H_SWITCH - th) / 2;

  uint16_t maxP = settingsDoc["switchSettings"][switches[selectedSwitch].id]["maxPorts"]
                  | switches[selectedSwitch].totalPorts;

  tft.setCursor(MARGIN + 6, textY);
  tft.setTextColor(TFT_WHITE, COLOR_PANEL);
  tft.print("Max ports: ");
  tft.print(maxP);
}

// — draw client settings ───────────────────────────────────────────────────────
void drawClientSettings() {
  screen = SCR_CLIENT_SETTINGS;
  tft.fillScreen(COLOR_BG);
  tft.setTextSize(1);
  tft.setTextColor(TFT_WHITE);
  drawBack(MARGIN, MARGIN);
  tft.setCursor(MARGIN * 2 + BACK_W, 8);
  tft.print(allClients[selectedSwitch][selectedClient].name + " Settings");

  int y1 = HEADER_H + MARGIN;
  tft.fillRoundRect(MARGIN, y1, tft.width() - 2 * MARGIN, CARD_H_SWITCH, CARD_R, COLOR_PANEL);
  int ty = y1 + (CARD_H_SWITCH - 8) / 2;
  tft.setCursor(MARGIN + 6, ty);
  tft.print("Switch Port: " + String(allClients[selectedSwitch][selectedClient].port));

  int y2 = y1 + CARD_H_SWITCH + MARGIN;
  tft.fillRoundRect(MARGIN, y2, tft.width() - 2 * MARGIN, CARD_H_SWITCH, CARD_R, COLOR_PANEL);
  int ty2 = y2 + (CARD_H_SWITCH - 8) / 2;
  tft.setCursor(MARGIN + 6, ty2);
  tft.print("Patch Panel: " + allClients[selectedSwitch][selectedClient].patchPanel);
}

// — draw edit switch-port pad ─────────────────────────────────────────────────
void drawEditSwPortPad() {
  screen = SCR_EDIT_SW_PORT;
  tft.fillScreen(COLOR_BG);
  tft.setTextSize(1);
  tft.setTextColor(TFT_WHITE);
  drawBack(MARGIN, MARGIN);
  tft.setCursor(MARGIN * 2 + BACK_W, 8);
  tft.print("Enter Switch Port");

  if (!editingSwPort) {
    swPortInput = String(settingsDoc["switchSettings"][switches[selectedSwitch].id]["maxPorts"] | switches[selectedSwitch].totalPorts);
    editingSwPort = true;
  }
  tft.setTextSize(2);
  tft.setCursor(MARGIN, HEADER_H + MARGIN);
  tft.print(swPortInput);

  int padY = HEADER_H + MARGIN + 30;
  int fullW = tft.width() - 4 * MARGIN, fullH = tft.height() - padY - 2 * MARGIN;
  int bw = (fullW * 85) / 100 / 3, bh = (fullH * 85) / 100 / 4;

  for (int r = 0; r < 4; r++) {
    for (int c = 0; c < 3; c++) {
      int bx = MARGIN + c * (bw + MARGIN), by = padY + r * (bh + MARGIN);
      tft.fillRoundRect(bx, by, bw, bh, CARD_R, COLOR_PANEL);
      int tw = tft.textWidth(numpad[r][c]);
      tft.setCursor(bx + (bw - tw) / 2, by + (bh - 16) / 2 + 4);
      tft.print(numpad[r][c]);
    }
  }
}

// — draw edit client-port pad ─────────────────────────────────────────────────
void drawEditClPortPad() {
  screen = SCR_EDIT_CL_PORT;
  tft.fillScreen(COLOR_BG);
  tft.setTextSize(1);
  tft.setTextColor(TFT_WHITE);
  drawBack(MARGIN, MARGIN);
  tft.setCursor(MARGIN * 2 + BACK_W, 8);
  tft.print("Enter Client Port");

  if (!editingClPort) {
    clPortInput = String(settingsDoc["clientSettings"][allClients[selectedSwitch][selectedClient].macAddr]["port"] | allClients[selectedSwitch][selectedClient].port);
    editingClPort = true;
  }
  tft.setTextSize(2);
  tft.setCursor(MARGIN, HEADER_H + MARGIN);
  tft.print(clPortInput);

  int padY = HEADER_H + MARGIN + 30;
  int fullW = tft.width() - 4 * MARGIN, fullH = tft.height() - padY - 2 * MARGIN;
  int bw = (fullW * 85) / 100 / 3, bh = (fullH * 85) / 100 / 4;

  for (int r = 0; r < 4; r++) {
    for (int c = 0; c < 3; c++) {
      int bx = MARGIN + c * (bw + MARGIN), by = padY + r * (bh + MARGIN);
      tft.fillRoundRect(bx, by, bw, bh, CARD_R, COLOR_PANEL);
      int tw = tft.textWidth(numpad[r][c]);
      tft.setCursor(bx + (bw - tw) / 2, by + (bh - 16) / 2 + 4);
      tft.print(numpad[r][c]);
    }
  }
}

// — draw panel-letter pad ─────────────────────────────────────────────────────
void drawPanelLetterPad() {
  screen = SCR_EDIT_PP_LETTER;
  tft.fillScreen(COLOR_BG);
  tft.setTextSize(1);
  tft.setTextColor(TFT_WHITE);
  drawBack(MARGIN, MARGIN);
  tft.setCursor(MARGIN * 2 + BACK_W, 8);
  tft.print("Select Panel Letter");

  int padY = HEADER_H + MARGIN;
  int fullW = tft.width() - 4 * MARGIN, fullH = tft.height() - padY - 2 * MARGIN;
  int bw = (fullW * 85) / 100 / 3, bh = (fullH * 85) / 100 / 2;

  for (int r = 0; r < 2; r++) {
    for (int c = 0; c < 3; c++) {
      int bx = MARGIN + c * (bw + MARGIN), by = padY + r * (bh + MARGIN);
      tft.fillRoundRect(bx, by, bw, bh, CARD_R, COLOR_PANEL);
      int tw = tft.textWidth(panels[r][c]);
      tft.setCursor(bx + (bw - tw) / 2, by + (bh - 16) / 2 + 4);
      tft.print(panels[r][c]);
    }
  }
}

void drawPanelNumberPad() {
  screen = SCR_EDIT_PP_NUMBER;
  tft.fillScreen(COLOR_BG);
  tft.setTextSize(1);
  tft.setTextColor(TFT_WHITE);
  drawBack(MARGIN, MARGIN);
  tft.setCursor(MARGIN * 2 + BACK_W, 8);
  tft.print("Enter Panel Number");

  if (!editingPpNumber) {
    if (!editingPpLetter) {
      // preload existing if coming from Client Settings
      String existing = settingsDoc["clientSettings"]
                                   [allClients[selectedSwitch][selectedClient].macAddr]
                                   ["patchPanel"]
                                     .as<String>();

      if (existing.length() >= 1) {
        char c0 = existing[0];
        if ((c0 >= 'A' && c0 <= 'Z') || (c0 >= 'a' && c0 <= 'z')) {
          ppLetter = String(c0);
          ppNumberInput = existing.substring(1);
        } else {
          ppLetter = "";
          ppNumberInput = existing;
        }
      } else {
        ppNumberInput = "";
      }
    } else {
      // we explicitly chose A/B/C/D/None just now
      ppNumberInput = "";
    }
    editingPpNumber = true;
    editingPpLetter = false;
  }

  // current value preview
  tft.setTextSize(2);
  tft.setCursor(MARGIN, HEADER_H + MARGIN);
  tft.print(ppLetter + ppNumberInput);

  // draw keypad (same style as other pads)
  int padY = HEADER_H + MARGIN + 30;
  int fullW = tft.width() - 4 * MARGIN, fullH = tft.height() - padY - 2 * MARGIN;
  int bw = (fullW * 85) / 100 / 3, bh = (fullH * 85) / 100 / 4;

  tft.setTextSize(1);
  for (int r = 0; r < 4; r++) {
    for (int c = 0; c < 3; c++) {
      int bx = MARGIN + c * (bw + MARGIN), by = padY + r * (bh + MARGIN);
      tft.fillRoundRect(bx, by, bw, bh, CARD_R, COLOR_PANEL);
      int tw = tft.textWidth(numpad[r][c]);
      tft.setCursor(bx + (bw - tw) / 2, by + (bh - 16) / 2 + 4);
      tft.print(numpad[r][c]);
    }
  }
}


void handleTouch() {
  if (!ts.touched()) return;
  TS_Point p = ts.getPoint();
  if (p.z < 200) return;

  if (screenOff) {  // wake-only tap
    markActivity();
    while (ts.touched()) { delay(20); }
    return;
  }

  int x = map(p.x, TS_MINX, TS_MAXX, 0, tft.width());
  int y = map(p.y, TS_MINY, TS_MAXY, 0, tft.height());
  markActivity();
  if (millis() - lastTapMs < TAP_BLOCK_MS) return;


  switch (screen) {

    // ───────────────────────────── HOME ─────────────────────────────
    case SCR_HOME:
      {
        // header buttons
        if (hit(BTN_SETTINGS, x, y)) {
          drawSettings();
          return;
        }
        if (hit(BTN_REFRESH, x, y)) {
          refreshWithProgress();
          return;
        }

        // pager arrows (locals scoped in this block)
        {
          int ax = tft.width() - NAV_W - MARGIN;
          int ay = HEADER_H + MARGIN;
          int by = ay + NAV_W + MARGIN;
          int total = switches.size();

          if (total > 4 && x >= ax && x < ax + NAV_W && y >= ay && y < ay + NAV_W) {
            scrollHome = max(0, scrollHome - 4);
            drawHome();
            return;
          }
          if (total > 4 && x >= ax && x < ax + NAV_W && y >= by && y < by + NAV_W) {
            scrollHome = min((int)switches.size() - 4, scrollHome + 4);
            drawHome();
            return;
          }
        }
        // tap on a switch card → go to detail
        {
          int ax = tft.width() - NAV_W - MARGIN;           // right column with arrows
          if (x < ax - MARGIN && y > HEADER_H + MARGIN) {  // inside list area
            int idx = scrollHome + ((y - (HEADER_H + MARGIN)) / (CARD_H_SWITCH + MARGIN));
            if (idx >= 0 && idx < (int)switches.size()) {
              selectedSwitch = idx;
              scrollDetail = 0;
              drawDetail();
              return;
            }
          }
        }

        break;
      }

    // ─────────────────────────── SETTINGS ───────────────────────────
    case SCR_SETTINGS:
      {
        if (x < MARGIN + BACK_W && y < MARGIN + BACK_H) {
          drawHome();
          return;
        }

        if (hit(BTN_TOGGLE_IP, x, y)) {
          settingsDoc["showIP"] = !settingsDoc["showIP"].as<bool>();
          saveSettings();
          drawSettings();
          return;
        }
        if (hit(BTN_TOGGLE_MAC, x, y)) {
          settingsDoc["showMAC"] = !settingsDoc["showMAC"].as<bool>();
          saveSettings();
          drawSettings();
          return;
        }
        if (hit(BTN_CLEAR_CACHE, x, y)) {
          clearCacheWithProgress();
          drawSettings();
          return;
        }
        break;
      }

    case SCR_DETAIL:
      {
        if (x < MARGIN + BACK_W && y < MARGIN + BACK_H) {
          drawHome();
          return;
        }
        if (hit(BTN_SETTINGS, x, y)) {
          drawSwitchSettings();
          return;
        }

        // --- pager arrows (scoped) ---
        {
          int ax = tft.width() - NAV_W - MARGIN;
          int ay = HEADER_H + MARGIN;
          int by = ay + NAV_W + MARGIN;
          int total = allClients[selectedSwitch].size();

          if (total > 4 && x >= ax && x < ax + NAV_W && y >= ay && y < ay + NAV_W) {
            scrollDetail = max(0, scrollDetail - 4);
            drawDetail();
            return;
          }
          if (total > 4 && x >= ax && x < ax + NAV_W && y >= by && y < by + NAV_W) {
            scrollDetail = min((int)total - 4, scrollDetail + 4);
            drawDetail();
            return;
          }
        }

        // --- card tap (only inside the card width) ---
        {
          int cardH = clientCardH();
          int listAreaW = tft.width() - 2 * MARGIN - NAV_W - 2;
          int cardW = (listAreaW * 85) / 100;  // same calc as drawDetail()
          int left = MARGIN, right = MARGIN + cardW;

          if (x > left && x < right && y > HEADER_H + MARGIN) {
            int idx = scrollDetail + ((y - (HEADER_H + MARGIN)) / (cardH + MARGIN));
            if (idx >= 0 && idx < (int)allClients[selectedSwitch].size()) {
              selectedClient = idx;
              drawClientSettings();
              return;
            }
          }
        }


        break;
      }


    // ────────────────────── SWITCH SETTINGS ────────────────────────
    case SCR_SWITCH_SETTINGS:
      {
        if (x < MARGIN + BACK_W && y < MARGIN + BACK_H) {
          drawDetail();
          return;
        }

        {
          int y0 = HEADER_H + MARGIN;
          if (y > y0 && y < y0 + CARD_H_SWITCH) {
            editingSwPort = false;
            drawEditSwPortPad();
            return;
          }
        }
        break;
      }

    // ─────────────────────── CLIENT SETTINGS ───────────────────────
    case SCR_CLIENT_SETTINGS:
      {
        if (x < MARGIN + BACK_W && y < MARGIN + BACK_H) {
          drawDetail();
          return;
        }

        {
          int y1 = HEADER_H + MARGIN;
          if (y > y1 && y < y1 + CARD_H_SWITCH) {
            editingClPort = false;
            drawEditClPortPad();
            return;
          }
        }
        {
          int y2 = HEADER_H + MARGIN + CARD_H_SWITCH + MARGIN;
          if (y > y2 && y < y2 + CARD_H_SWITCH) {
            editingPpLetter = false;
            drawPanelLetterPad();
            return;
          }
        }
        break;
      }

    // ───────────────────────── EDIT SW PORT ─────────────────────────
    case SCR_EDIT_SW_PORT:
      {
        if (x < MARGIN + BACK_W && y < MARGIN + BACK_H) {
          drawSwitchSettings();
          return;
        }

        {
          int padY = HEADER_H + MARGIN + 30;
          int fullW = tft.width() - 4 * MARGIN, fullH = tft.height() - padY - 2 * MARGIN;
          int bw = (fullW * 85) / 100 / 3, bh = (fullH * 85) / 100 / 4;

          for (int r = 0; r < 4; r++) {
            for (int c = 0; c < 3; c++) {
              int bx = MARGIN + c * (bw + MARGIN), by = padY + r * (bh + MARGIN);
              if (x > bx && x < bx + bw && y > by && y < by + bh) {
                String lbl = numpad[r][c];
                if (lbl == "OK") {
                  if (swPortInput.length() == 0) { /* maybe beep/toast */
                    drawEditSwPortPad();
                    return;
                  }
                  int val = swPortInput.toInt();
                  val = max(1, min(val, 256));  // clamp 1..256, or pick your real limit
                  settingsDoc["switchSettings"][switches[selectedSwitch].id]["maxPorts"] = val;
                  saveSettings();
                  drawSwitchSettings();
                  return;
                }

                if (lbl == "DEL") {
                  if (swPortInput.length()) swPortInput.remove(swPortInput.length() - 1);
                } else if (swPortInput.length() < 4) {
                  swPortInput += lbl;
                }
                drawEditSwPortPad();
                return;
              }
            }
          }
        }
        break;
      }

    // ───────────────────────── EDIT CL PORT ─────────────────────────
    case SCR_EDIT_CL_PORT:
      {
        if (x < MARGIN + BACK_W && y < MARGIN + BACK_H) {
          drawClientSettings();
          return;
        }

        {
          int padY = HEADER_H + MARGIN + 30;
          int fullW = tft.width() - 4 * MARGIN, fullH = tft.height() - padY - 2 * MARGIN;
          int bw = (fullW * 85) / 100 / 3, bh = (fullH * 85) / 100 / 4;

          for (int r = 0; r < 4; r++) {
            for (int c = 0; c < 3; c++) {
              int bx = MARGIN + c * (bw + MARGIN), by = padY + r * (bh + MARGIN);
              if (x > bx && x < bx + bw && y > by && y < by + bh) {
                String lbl = numpad[r][c];
                if (lbl == "OK") {
                  String mac = allClients[selectedSwitch][selectedClient].macAddr;

                  if (clPortInput.length()) {
                    int port = clPortInput.toInt();
                    port = max(1, min(port, 256));  // optional clamp
                    settingsDoc["clientSettings"][mac]["port"] = port;
                    allClients[selectedSwitch][selectedClient].port = port;  // update RAM view
                    saveSettings();
                  }
                  int val = clPortInput.toInt();
                  val = max(1, min(val, 256));  // clamp as you prefer
                  settingsDoc["clientSettings"][mac]["port"] = val;
                  saveSettings();
                  allClients[selectedSwitch][selectedClient].port = val;
                  drawClientSettings();
                  return;
                }

                if (lbl == "DEL") {
                  if (clPortInput.length()) clPortInput.remove(clPortInput.length() - 1);
                } else if (clPortInput.length() < 4) {
                  clPortInput += lbl;
                }
                drawEditClPortPad();
                return;
              }
            }
          }
        }
        break;
      }

    // ─────────────────────── EDIT PP LETTER ────────────────────────
    case SCR_EDIT_PP_LETTER:
      {
        if (x < MARGIN + BACK_W && y < MARGIN + BACK_H) {
          drawClientSettings();
          return;
        }

        int padY = HEADER_H + MARGIN;
        int fullW = tft.width() - 4 * MARGIN, fullH = tft.height() - padY - 2 * MARGIN;
        int bw = int((fullW / 3) * 0.85f), bh = int((fullH / 2) * 0.85f);

        for (int r = 0; r < 2; r++) {
          for (int c = 0; c < 3; c++) {
            int bx = MARGIN + c * (bw + MARGIN);
            int by = padY + r * (bh + MARGIN);
            if (x > bx && x < bx + bw && y > by && y < by + bh) {
              String lbl = panels[r][c];

              if (lbl == "OK") {
                editingPpLetter = true;
                editingPpNumber = false;
                drawPanelNumberPad();
                return;
              }
              if (lbl == "None") {
                ppLetter = "";
                editingPpLetter = true;
                editingPpNumber = false;
                ppNumberInput = "";
                drawPanelNumberPad();
                return;
              }

              // A/B/C/D
              ppLetter = lbl;
              editingPpLetter = true;
              editingPpNumber = false;
              ppNumberInput = "";
              drawPanelNumberPad();
              return;
            }
          }
        }
        break;
      }

    // ─────────────────────── EDIT PP NUMBER ────────────────────────
    case SCR_EDIT_PP_NUMBER:
      {
        if (x < MARGIN + BACK_W && y < MARGIN + BACK_H) {
          drawClientSettings();
          return;
        }

        {
          int padY = HEADER_H + MARGIN + 30;
          int fullW = tft.width() - 4 * MARGIN, fullH = tft.height() - padY - 2 * MARGIN;
          int bw = (fullW * 85) / 100 / 3, bh = (fullH * 85) / 100 / 4;

          for (int r = 0; r < 4; r++) {
            for (int c = 0; c < 3; c++) {
              int bx = MARGIN + c * (bw + MARGIN), by = padY + r * (bh + MARGIN);
              if (x > bx && x < bx + bw && y > by && y < by + bh) {
                String lbl = numpad[r][c];
                if (lbl == "OK") {
                  String mac = allClients[selectedSwitch][selectedClient].macAddr;
                  String fullPP = ppLetter + ppNumberInput;

                  if (fullPP.length() == 0) {
                    settingsDoc["clientSettings"][mac].remove("patchPanel");
                    allClients[selectedSwitch][selectedClient].patchPanel = "—";
                  } else {
                    settingsDoc["clientSettings"][mac]["patchPanel"] = fullPP;
                    allClients[selectedSwitch][selectedClient].patchPanel = fullPP;
                  }
                  saveSettings();
                  drawClientSettings();
                  return;
                }
                if (lbl == "DEL") {
                  if (ppNumberInput.length()) ppNumberInput.remove(ppNumberInput.length() - 1);
                } else if (ppNumberInput.length() < 3) {
                  ppNumberInput += lbl;
                }
                drawPanelNumberPad();
                return;
              }
            }
          }
        }
        break;
      }

  }  // end switch
}



// ─── setup/loop ───────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);

  // Display + touch init
  tft.init();
  applyRotation();
  pinMode(BL_PIN, OUTPUT);
  setBacklight(true);
  lastInputMs = millis();

  touchSPI.begin(TOUCH_CLK, TOUCH_MISO, TOUCH_MOSI, TOUCH_CS);
  ts.begin(touchSPI);


  // Boot UI
  bootScreenInit();
  bootStatus("Mounting filesystem...");
  if (!SPIFFS.begin(true)) Serial.println("⚠ SPIFFS failed");
  loadSettings();

  bootStatus("Connecting to Wi-Fi...");
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) { delay(300); }

  client.setInsecure();

  bootStatus("Fetching network data...");
  updateNetworkData();

  bootStatus("Prefetching clients...");
  prefetchClients();
  lastRefreshMs = millis();
  drawHome();
}

void loop() {
  // sleep the backlight after inactivity
  if (!screenOff && (millis() - lastInputMs > SCREEN_TIMEOUT_MS)) {
    screenOff = true;
    setBacklight(false);
  }
  handleTouch();
}
