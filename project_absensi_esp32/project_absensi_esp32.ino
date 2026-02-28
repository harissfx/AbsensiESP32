/*
 * ╔══════════════════════════════════════════════════════════╗
 *   SISTEM ABSENSI ESP32 
 *   ESP32 + RFID RC522 + OLED SH1106 1.3" + Buzzer
 * ╚══════════════════════════════════════════════════════════╝
 *
 *  HARDWARE:
 *    OLED SH1106 1.3"  │ SDA=21  SCL=22
 *    RFID RC522        │ SCK=18  MOSI=23  MISO=19  SS=5  RST=17
 *    Buzzer            │ PIN 25
 *    Tombol KIRI       │ PIN 12  (tap=scroll/back | tahan 2dtk=admin)
 *    Tombol KANAN      │ PIN 14  (pilih/konfirmasi)
 *
 *  STORAGE:
 *    LittleFS — /users.json (tidak aus, tahan hapus/tulis berulang)
 *    Format: {"count":N,"users":[{"uid":"AA:BB:CC:DD","name":"..."},...]}
 *
 *  WiFi AP:
 *    SSID   : AbsensiESP32
 *    Pass   : 12345678
 *    Domain : haris.com  (ubah AP_DOMAIN)
 *    → Konek WiFi → browser auto-redirect ke dashboard
 *
 *  LIBRARY (install via Library Manager):
 *    - U8g2            → OLED
 *    - MFRC522         → RFID
 *    - WebSockets      → Markus Sattler (arduinoWebSockets)
 *    - ArduinoJson     → Benoit Blanchon (v6 atau v7)
 *    Built-in ESP32:
 *    - WiFi, WebServer, DNSServer, LittleFS
 *
 *  FILE STRUKTUR:
 *    absensi_esp32.ino   ← file ini
 *    halaman.h           ← HTML dashboard (PROGMEM)
 * ══════════════════════════════════════════════════════════
 */

#include <SPI.h>
#include <MFRC522.h>
#include <U8g2lib.h>
#include <Wire.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <WebSocketsServer.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include "halaman.h"

// ┌──────────────────────────────────────────────────────┐
//   PIN
// └──────────────────────────────────────────────────────┘
#define RFID_SCK     18
#define RFID_MOSI    23
#define RFID_MISO    19
#define RFID_SS       5
#define RFID_RST     17
#define BUZZER_PIN   25
#define BTN_LEFT     14
#define BTN_RIGHT    12

// ┌──────────────────────────────────────────────────────┐
//   KONFIGURASI
// └──────────────────────────────────────────────────────┘
#define MAX_USERS    50
#define UID_SIZE      4
#define NAME_SIZE    20
#define MAX_LOG     200

#define HOLD_DURATION  2000UL
#define MENU_TIMEOUT  20000UL
#define RESULT_TIMEOUT 2500UL
#define DEBOUNCE_MS     50UL
#define SCAN_COOLDOWN  2000UL

const char* AP_SSID   = "AbsensiESP32";
const char* AP_PASS   = "12345678";
const char* AP_DOMAIN = "haris.com";
const char* USERS_FILE = "/users.json";
const byte  DNS_PORT  = 53;

// ┌──────────────────────────────────────────────────────┐
//   STATE
// └──────────────────────────────────────────────────────┘
enum AppMode {
  MODE_ATTEND,
  MODE_ATTEND_OK,
  MODE_ATTEND_FAIL,
  MODE_ADMIN_MENU,
  MODE_ADMIN_REGISTER,
  MODE_ADMIN_DELETE,
  MODE_ADMIN_DEL_CONFIRM,
  MODE_RESULT_OK,
  MODE_RESULT_FAIL
};

// ┌──────────────────────────────────────────────────────┐
//   STRUKTUR DATA
// └──────────────────────────────────────────────────────┘
struct User {
  byte uid[UID_SIZE];
  char name[NAME_SIZE];
};

struct LogEntry {
  char name[NAME_SIZE];
  char uid[14];
  unsigned long ts;
};

// ┌──────────────────────────────────────────────────────┐
//   OBJEK GLOBAL
// └──────────────────────────────────────────────────────┘
U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);
MFRC522          rfid(RFID_SS, RFID_RST);
WebServer        server(80);
WebSocketsServer ws(81);
DNSServer        dns;

User      users[MAX_USERS];
int       userCount    = 0;
AppMode   currentMode  = MODE_ATTEND;
int       menuIndex    = 0;
int       deleteTarget = -1;

LogEntry  logs[MAX_LOG];
int       logCount     = 0;

bool          btnLPrev      = HIGH, btnRPrev = HIGH;
unsigned long btnLTime      = 0,    btnRTime = 0;
unsigned long btnLDown      = 0;
bool          btnLHolding   = false;
bool          btnLHeldFired = false;
bool          btnLPressed   = false;
bool          btnLHeld      = false;
bool          btnRPressed   = false;

int           dotStep    = 0;
unsigned long lastFrame  = 0;
unsigned long lastAction = 0;
unsigned long lastScan   = 0;

// ┌──────────────────────────────────────────────────────┐
//   BUZZER
// └──────────────────────────────────────────────────────┘
void beepOK()     { tone(BUZZER_PIN,1000,90);  delay(120); tone(BUZZER_PIN,1600,110); }
void beepFail()   { tone(BUZZER_PIN,350,380);  }
void beepClick()  { tone(BUZZER_PIN,900,50);   }
void beepDelete() { tone(BUZZER_PIN,600,90);   delay(120); tone(BUZZER_PIN,370,180); }
void beepBoot()   { tone(BUZZER_PIN,800,70);   delay(90);  tone(BUZZER_PIN,1100,70); delay(90); tone(BUZZER_PIN,1500,130); }

// ┌──────────────────────────────────────────────────────┐
//   Format /users.json:
//   {"count":2,"users":[
//     {"uid":"AA:BB:CC:DD","name":"Budi"},
//     {"uid":"11:22:33:44","name":"Sari"}
//   ]}
// └──────────────────────────────────────────────────────┘
void saveUsers() {

  JsonDocument doc;
  doc["count"] = userCount;
  JsonArray arr = doc["users"].to<JsonArray>();
  for (int i = 0; i < userCount; i++) {
    JsonObject obj = arr.add<JsonObject>();
    obj["uid"]  = uidToStr(users[i].uid, UID_SIZE);
    obj["name"] = users[i].name;
  }

  File f = LittleFS.open(USERS_FILE, "w");
  if (!f) {
    Serial.println("[FS] Gagal buka file untuk write!");
    return;
  }
  serializeJson(doc, f);
  f.close();
  Serial.printf("[FS] Saved %d users\n", userCount);
}

void loadUsers() {
  if (!LittleFS.exists(USERS_FILE)) {
    Serial.println("[FS] users.json tidak ditemukan, mulai kosong");
    userCount = 0;
    return;
  }

  File f = LittleFS.open(USERS_FILE, "r");
  if (!f) { userCount = 0; return; }

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, f);
  f.close();

  if (err) {
    Serial.printf("[FS] JSON parse error: %s\n", err.c_str());
    userCount = 0;
    return;
  }

  userCount = doc["count"] | 0;
  if (userCount > MAX_USERS) userCount = MAX_USERS;

  JsonArray arr = doc["users"].as<JsonArray>();
  int idx = 0;
  for (JsonObject obj : arr) {
    if (idx >= MAX_USERS) break;

    const char* uidStr = obj["uid"] | "";
    strToUid(uidStr, users[idx].uid);

    const char* nm = obj["name"] | "Unknown";
    strncpy(users[idx].name, nm, NAME_SIZE-1);
    users[idx].name[NAME_SIZE-1] = '\0';
    idx++;
  }
  userCount = idx;
  Serial.printf("[FS] Loaded %d users\n", userCount);
}

// ┌──────────────────────────────────────────────────────┐
//   RFID / UID UTILS
// └──────────────────────────────────────────────────────┘
String uidToStr(byte* uid, byte size) {
  String s;
  for (byte i = 0; i < size; i++) {
    if (uid[i] < 0x10) s += "0";
    s += String(uid[i], HEX);
    if (i < size-1) s += ":";
  }
  s.toUpperCase();
  return s;
}

void strToUid(const char* str, byte* uid) {
  for (int i = 0; i < UID_SIZE; i++) {
    uid[i] = 0;
    if (str[i*3] == '\0') break;
    char h[3] = { str[i*3], str[i*3+1], '\0' };
    uid[i] = (byte)strtol(h, nullptr, 16);
  }
}

int findUser(byte* uid) {
  for (int i = 0; i < userCount; i++) {
    bool ok = true;
    for (int j = 0; j < UID_SIZE; j++)
      if (users[i].uid[j] != uid[j]) { ok=false; break; }
    if (ok) return i;
  }
  return -1;
}

bool addUser(byte* uid) {
  if (findUser(uid) >= 0 || userCount >= MAX_USERS) return false;
  memcpy(users[userCount].uid, uid, UID_SIZE);
  snprintf(users[userCount].name, NAME_SIZE, "User%02d", userCount+1);
  userCount++;
  saveUsers();
  return true;
}

bool removeUser(int idx) {
  if (idx < 0 || idx >= userCount) return false;
  for (int i = idx; i < userCount-1; i++) users[i] = users[i+1];
  userCount--;
  saveUsers();
  return true;
}

// ┌──────────────────────────────────────────────────────┐
//   LOG (RAM only — hilang saat restart)
// └──────────────────────────────────────────────────────┘
void addLog(const char* name, const char* uid) {
  if (logCount >= MAX_LOG) {
    for (int i = 0; i < MAX_LOG-1; i++) logs[i] = logs[i+1];
    logCount = MAX_LOG-1;
  }
  strncpy(logs[logCount].name, name, NAME_SIZE-1);
  logs[logCount].name[NAME_SIZE-1] = '\0';
  strncpy(logs[logCount].uid, uid, 13);
  logs[logCount].uid[13] = '\0';
  logs[logCount].ts = millis();
  logCount++;
}

String formatUptime(unsigned long ms) {
  unsigned long s = ms/1000;
  int h = s/3600; s %= 3600;
  int m = s/60;   s %= 60;
  char b[12]; snprintf(b, sizeof(b), "%02d:%02d:%02d", h, m, (int)s);
  return String(b);
}

// ┌──────────────────────────────────────────────────────┐
//   WEBSOCKET — broadcast JSON ke semua client
// └──────────────────────────────────────────────────────┘
String jsonEsc(const char* s) {
  String out;
  while (*s) {
    char c = *s++;
    if      (c == '"')  out += "\\\"";
    else if (c == '\\') out += "\\\\";
    else                out += c;
  }
  return out;
}

void wsBroadcastStatus() {
  String msg = "{\"type\":\"status\","
               "\"uptime\":\"" + formatUptime(millis()) + "\","
               "\"users\":"    + String(userCount) + ","
               "\"logs\":"     + String(logCount)  + "}";
  ws.broadcastTXT(msg);
}

void wsBroadcastAttend(const char* name, const char* uid) {
  String msg = "{\"type\":\"attend\","
               "\"name\":\""  + jsonEsc(name) + "\","
               "\"uid\":\""   + jsonEsc(uid)  + "\","
               "\"time\":\""  + formatUptime(millis()) + "\"}";
  ws.broadcastTXT(msg);
}

void wsBroadcastUsers() {
  String msg = "{\"type\":\"users\",\"count\":" + String(userCount) + ",\"users\":[";
  for (int i = 0; i < userCount; i++) {
    if (i) msg += ",";
    msg += "{\"name\":\"" + jsonEsc(users[i].name)
        + "\",\"uid\":\""  + uidToStr(users[i].uid, UID_SIZE) + "\"}";
  }
  msg += "]}";
  ws.broadcastTXT(msg);
}

String buildLogsJson() {
  String l = "{\"type\":\"logs\",\"count\":" + String(logCount) + ",\"logs\":[";
  for (int i = 0; i < logCount; i++) {
    if (i) l += ",";
    l += "{\"name\":\"" + jsonEsc(logs[i].name)
      + "\",\"uid\":\""  + String(logs[i].uid)
      + "\",\"time\":\"" + formatUptime(logs[i].ts) + "\"}";
  }
  l += "]}";
  return l;
}

// ┌──────────────────────────────────────────────────────┐
//   DISPLAY HELPERS
// └──────────────────────────────────────────────────────┘
void drawHeader(const char* title) {
  u8g2.setDrawColor(1);
  u8g2.drawBox(0, 0, 128, 14);
  u8g2.setDrawColor(0);
  u8g2.setFont(u8g2_font_6x10_tf);
  u8g2.drawStr((128 - u8g2.getStrWidth(title))/2, 11, title);
  u8g2.setDrawColor(1);
}

void drawFooter(const char* left, const char* right="") {
  u8g2.drawHLine(0, 56, 128);
  u8g2.setFont(u8g2_font_5x7_tf);
  if (left  && strlen(left))  u8g2.drawStr(2,   63, left);
  if (right && strlen(right)) u8g2.drawStr(126 - u8g2.getStrWidth(right), 63, right);
}

void drawCenter(const uint8_t* font, int y, const char* str) {
  u8g2.setFont(font);
  u8g2.drawStr((128 - u8g2.getStrWidth(str))/2, y, str);
}

// ── Layar Attend ──────────────────────────────────────
void displayAttend() {
  unsigned long sec = millis()/1000;
  char upBuf[10];  snprintf(upBuf,  sizeof(upBuf),  "%02lu:%02lu", (sec/60)%60, sec%60);
  char cntBuf[14]; snprintf(cntBuf, sizeof(cntBuf), "%d/%d user", userCount, MAX_USERS);
  u8g2.clearBuffer();
  drawHeader(" SISTEM ABSENSI ");
  u8g2.drawRFrame(3, 18, 26, 18, 2);
  u8g2.drawHLine(7, 23, 18); u8g2.drawHLine(7, 27, 12);
  u8g2.setFont(u8g2_font_6x10_tf);
  u8g2.drawStr(35, 26, "Tap kartu");
  u8g2.drawStr(35, 38, "untuk absen");
  u8g2.setFont(u8g2_font_5x7_tf);
  u8g2.drawStr(3, 51, cntBuf);
  u8g2.drawStr(125 - u8g2.getStrWidth(upBuf), 51, upBuf);
  drawFooter("[<] hold=Admin", "");
  u8g2.sendBuffer();
}

// ── Layar Absen OK ────────────────────────────────────
void displayAttendOK(const char* name) {
  u8g2.clearBuffer();
  drawHeader("   ABSEN OK   ");
  u8g2.drawLine(6, 34, 13, 41); u8g2.drawLine(13, 41, 25, 26);
  u8g2.drawLine(7, 35, 14, 42); u8g2.drawLine(14, 42, 26, 27);
  u8g2.setFont(u8g2_font_7x13_tf);
  u8g2.drawStr(32, 32, name);
  drawCenter(u8g2_font_5x7_tf, 46, "Absen Tercatat!");
  u8g2.drawFrame(3, 52, 122, 6);
  u8g2.sendBuffer();
}

void updateAttendOKBar(unsigned long elapsed) {
  int w = (int)(122.0f * (float)elapsed / (float)RESULT_TIMEOUT);
  if (w > 122) w = 122;
  u8g2.drawBox(3, 52, w, 6);
  u8g2.sendBuffer();
}

// ── Layar Absen Fail ──────────────────────────────────
void displayAttendFail(const char* uidStr) {
  u8g2.clearBuffer();
  drawHeader(" KARTU UNKNOWN  ");
  u8g2.drawLine(6, 26, 18, 38); u8g2.drawLine(7, 26, 19, 38);
  u8g2.drawLine(18, 26, 6, 38); u8g2.drawLine(19, 26, 7, 38);
  u8g2.setFont(u8g2_font_6x10_tf); u8g2.drawStr(26, 30, "Tdk terdaftar");
  u8g2.setFont(u8g2_font_5x7_tf);  u8g2.drawStr(26, 42, uidStr);
  drawCenter(u8g2_font_5x7_tf, 54, "Hubungi admin");
  u8g2.sendBuffer();
}

// ── Layar Admin Menu ──────────────────────────────────
const char* ADMIN_LABELS[] = { "Register Kartu", "Hapus Kartu" };
const int   ADMIN_COUNT    = 2;

void displayAdminMenu() {
  u8g2.clearBuffer();
  drawHeader("  ADMIN MENU  ");
  u8g2.setFont(u8g2_font_5x7_tf);
  u8g2.drawStr(2, 22, "MODE ADMIN AKTIF");
  for (int i = 0; i < ADMIN_COUNT; i++) {
    int y = 34 + i*13;
    if (i == menuIndex) {
      u8g2.drawBox(0, y-10, 128, 12);
      u8g2.setDrawColor(0);
      u8g2.setFont(u8g2_font_6x10_tf);
      u8g2.drawStr(4, y, ">"); u8g2.drawStr(14, y, ADMIN_LABELS[i]);
      u8g2.setDrawColor(1);
    } else {
      u8g2.setFont(u8g2_font_6x10_tf);
      u8g2.drawStr(14, y, ADMIN_LABELS[i]);
    }
  }
  drawFooter("[<] Scroll", "[>] Pilih");
  u8g2.sendBuffer();
}

// ── Layar Scan Admin ──────────────────────────────────
void displayAdminScan(const char* title) {
  u8g2.clearBuffer();
  drawHeader(title);
  drawCenter(u8g2_font_7x13_tf, 34, "Tap Kartu");
  drawCenter(u8g2_font_7x13_tf, 47, "RFID...");
  for (int d = 0; d < 3; d++) {
    int dx = 50 + d*14;
    if (d == dotStep) u8g2.drawDisc(dx, 55, 3);
    else              u8g2.drawCircle(dx, 55, 2);
  }
  drawFooter("[<] Kembali", "");
  u8g2.sendBuffer();
}

// ── Layar Konfirmasi Hapus ────────────────────────────
void displayDeleteConfirm(int idx) {
  u8g2.clearBuffer();
  drawHeader(" KONFIRMASI HAPUS ");
  drawCenter(u8g2_font_6x10_tf, 27, "Hapus user ini?");
  int nw = u8g2.getStrWidth(users[idx].name);
  u8g2.drawRFrame((128-nw-10)/2, 30, nw+10, 14, 2);
  drawCenter(u8g2_font_7x13_tf, 41, users[idx].name);
  drawFooter("[<] Batal", "[>] HAPUS");
  u8g2.sendBuffer();
}

// ── Layar Result Admin ────────────────────────────────
void displayResult(bool ok, const char* l1, const char* l2="", const char* l3="") {
  u8g2.clearBuffer();
  drawHeader(ok ? "  >> BERHASIL <<  " : "  !! GAGAL !!  ");
  if (ok) {
    u8g2.drawLine(5, 34, 12, 41); u8g2.drawLine(12, 41, 24, 26);
    u8g2.drawLine(6, 35, 13, 42); u8g2.drawLine(13, 42, 25, 27);
  } else {
    u8g2.drawLine(5, 26, 17, 38); u8g2.drawLine(6, 26, 18, 38);
    u8g2.drawLine(17, 26, 5, 38); u8g2.drawLine(18, 26, 6, 38);
  }
  u8g2.setFont(u8g2_font_6x10_tf);
  u8g2.drawStr(30, 28, l1);
  if (strlen(l2)) u8g2.drawStr(30, 40, l2);
  if (strlen(l3)) { u8g2.setFont(u8g2_font_5x7_tf); u8g2.drawStr(30, 52, l3); }
  u8g2.sendBuffer();
}

// ┌──────────────────────────────────────────────────────┐
//   STATE TRANSITION
// └──────────────────────────────────────────────────────┘
void goTo(AppMode mode) {
  currentMode = mode;
  lastAction  = millis();
  switch (mode) {
    case MODE_ATTEND:         displayAttend(); break;
    case MODE_ADMIN_MENU:     menuIndex=0; displayAdminMenu(); break;
    case MODE_ADMIN_REGISTER: displayAdminScan("   REGISTER   "); break;
    case MODE_ADMIN_DELETE:   displayAdminScan("  HAPUS KARTU  "); break;
    default: break;
  }
}

// ┌──────────────────────────────────────────────────────┐
//   BACA TOMBOL
// └──────────────────────────────────────────────────────┘
void readButtons() {
  btnLPressed = false; btnLHeld = false; btnRPressed = false;
  unsigned long now = millis();
  bool lNow = digitalRead(BTN_LEFT);
  bool rNow = digitalRead(BTN_RIGHT);

  if (lNow == LOW) {
    if (!btnLHolding) {
      if (btnLPrev == HIGH && now - btnLTime > DEBOUNCE_MS) {
        btnLHolding = true; btnLHeldFired = false;
        btnLDown = now;     btnLTime = now;
      }
    } else {
      if (!btnLHeldFired && (now - btnLDown >= HOLD_DURATION)) {
        btnLHeld = true; btnLHeldFired = true;
      }
    }
  } else {
    if (btnLHolding) {
      if (!btnLHeldFired && (now - btnLDown > DEBOUNCE_MS)) btnLPressed = true;
      btnLHolding = false; btnLHeldFired = false;
    }
  }
  if (rNow == LOW && btnRPrev == HIGH && now - btnRTime > DEBOUNCE_MS) {
    btnRPressed = true; btnRTime = now;
  }
  btnLPrev = lNow; btnRPrev = rNow;
}

// ┌──────────────────────────────────────────────────────┐
//   WEBSOCKET EVENT HANDLER
// └──────────────────────────────────────────────────────┘
void onWsEvent(uint8_t num, WStype_t type, uint8_t* payload, size_t length) {
  switch (type) {
    case WStype_CONNECTED:
      Serial.printf("[WS] Client #%d connected\n", num);
      {
        // Kirim semua data awal ke client baru
        String s = "{\"type\":\"status\",\"uptime\":\"" + formatUptime(millis())
                 + "\",\"users\":" + String(userCount)
                 + ",\"logs\":"    + String(logCount) + "}";
        ws.sendTXT(num, s);

        String u = "{\"type\":\"users\",\"count\":" + String(userCount) + ",\"users\":[";
        for (int i = 0; i < userCount; i++) {
          if (i) u += ",";
          u += "{\"name\":\"" + jsonEsc(users[i].name)
            + "\",\"uid\":\"" + uidToStr(users[i].uid, UID_SIZE) + "\"}";
        }
        u += "]}";
        ws.sendTXT(num, u);
        String logsJson = buildLogsJson();
        ws.sendTXT(num, logsJson);
      }
      break;

    case WStype_TEXT:
      {
        String msg = String((char*)payload);
        if (msg.indexOf("getUsers") >= 0) wsBroadcastUsers();
        if (msg.indexOf("getLogs")  >= 0) { String logsJson = buildLogsJson(); ws.sendTXT(num, logsJson); }
      }
      break;

    case WStype_DISCONNECTED:
      Serial.printf("[WS] Client #%d disconnected\n", num);
      break;

    default: break;
  }
}

// ┌──────────────────────────────────────────────────────┐
//   HTTP ROUTES
// └──────────────────────────────────────────────────────┘
void handleCaptivePortal() {
  String url = "http://"; url += AP_DOMAIN; url += "/";
  server.sendHeader("Location", url, true);
  server.send(302, "text/plain", "");
}

void handleRoot() {
  server.send_P(200, "text/html", HTML_PAGE);
}

void handleApiRename() {
  if (!server.hasArg("idx") || !server.hasArg("name")) {
    server.send(400, "application/json", "{\"ok\":false}"); return;
  }
  int idx = server.arg("idx").toInt();
  String nm = server.arg("name"); nm.trim();
  if (idx < 0 || idx >= userCount || nm.length() == 0) {
    server.send(400, "application/json", "{\"ok\":false}"); return;
  }
  strncpy(users[idx].name, nm.c_str(), NAME_SIZE-1);
  users[idx].name[NAME_SIZE-1] = '\0';
  saveUsers();
  server.send(200, "application/json", "{\"ok\":true}");

  String msg = "{\"type\":\"userchange\",\"msg\":\"Nama diperbarui\","
               "\"count\":" + String(userCount) + ",\"users\":[";
  for (int i = 0; i < userCount; i++) {
    if (i) msg += ",";
    msg += "{\"name\":\"" + jsonEsc(users[i].name)
        + "\",\"uid\":\"" + uidToStr(users[i].uid, UID_SIZE) + "\"}";
  }
  msg += "]}";
  ws.broadcastTXT(msg);
}

void handleApiDelete() {
  if (!server.hasArg("idx")) {
    server.send(400, "application/json", "{\"ok\":false}"); return;
  }
  int idx = server.arg("idx").toInt();
  String dname = (idx >= 0 && idx < userCount) ? String(users[idx].name) : "";
  if (removeUser(idx)) {   // removeUser sudah panggil saveUsers()
    server.send(200, "application/json", "{\"ok\":true}");
    String msg = "{\"type\":\"userchange\",\"msg\":\""
               + jsonEsc(dname.c_str()) + " dihapus\","
               + "\"count\":" + String(userCount) + ",\"users\":[";
    for (int i = 0; i < userCount; i++) {
      if (i) msg += ",";
      msg += "{\"name\":\"" + jsonEsc(users[i].name)
          + "\",\"uid\":\"" + uidToStr(users[i].uid, UID_SIZE) + "\"}";
    }
    msg += "]}";
    ws.broadcastTXT(msg);
  } else {
    server.send(400, "application/json", "{\"ok\":false}");
  }
}

void handleApiLogsCsv() {
  String csv = "No,Nama,UID,Waktu(uptime)\r\n";
  for (int i = 0; i < logCount; i++) {
    csv += String(i+1) + "," + logs[i].name + ","
         + logs[i].uid + "," + formatUptime(logs[i].ts) + "\r\n";
  }
  server.sendHeader("Content-Disposition", "attachment; filename=absensi_log.csv");
  server.send(200, "text/csv", csv);
}

void handleApiDebugFs() {
  String out = "LittleFS Files:\n";
  File root = LittleFS.open("/");
  File file = root.openNextFile();
  while (file) {
    out += "  " + String(file.name()) + "  (" + String(file.size()) + " bytes)\n";
    file = root.openNextFile();
  }
  if (LittleFS.exists(USERS_FILE)) {
    File f = LittleFS.open(USERS_FILE, "r");
    out += "\nusers.json:\n" + f.readString();
    f.close();
  }
  server.send(200, "text/plain", out);
}

void setupNetwork() {
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS);
  delay(100);
  Serial.printf("[WiFi] AP IP: %s\n", WiFi.softAPIP().toString().c_str());

  dns.start(DNS_PORT, "*", WiFi.softAPIP());

  server.on("/",             HTTP_GET,  handleRoot);
  server.on("/index.html",   HTTP_GET,  handleRoot);
  server.on("/favicon.ico",  HTTP_GET,  [](){server.send(204,"","");});
  server.on("/api/rename",   HTTP_POST, handleApiRename);
  server.on("/api/delete",   HTTP_POST, handleApiDelete);
  server.on("/api/logs/csv", HTTP_GET,  handleApiLogsCsv);
  server.on("/api/debug/fs", HTTP_GET,  handleApiDebugFs);
  // Captive portal endpoints
  server.on("/generate_204",              HTTP_GET, handleCaptivePortal);
  server.on("/gen_204",                   HTTP_GET, handleCaptivePortal);
  server.on("/hotspot-detect.html",       HTTP_GET, handleCaptivePortal);
  server.on("/library/test/success.html", HTTP_GET, handleCaptivePortal);
  server.on("/ncsi.txt",                  HTTP_GET, handleCaptivePortal);
  server.on("/connecttest.txt",           HTTP_GET, handleCaptivePortal);
  server.on("/redirect",                  HTTP_GET, handleCaptivePortal);
  server.onNotFound(handleCaptivePortal);

  server.begin();
  ws.begin();
  ws.onEvent(onWsEvent);
}

// ┌──────────────────────────────────────────────────────┐
//   SETUP
// └──────────────────────────────────────────────────────┘
void setup() {
  Serial.begin(115200);
  delay(200);

  pinMode(BTN_LEFT,   INPUT_PULLUP);
  pinMode(BTN_RIGHT,  INPUT_PULLUP);
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);

  // ── LittleFS init ──────────────────────────────────────
  if (!LittleFS.begin(true)) {
    Serial.println("[FS] LittleFS mount GAGAL!");
  } else {
    Serial.println("[FS] LittleFS OK");
    loadUsers();
  }

  Wire.begin(21, 22);
  u8g2.begin();
  u8g2.setContrast(220);

  SPI.begin(RFID_SCK, RFID_MISO, RFID_MOSI, RFID_SS);
  rfid.PCD_Init();
  delay(50);

  setupNetwork();

  // ══════════════════════════════════════════════════════
  //   ANIMATED SPLASH — 30fps unified loop (~3.5 detik)
  // ══════════════════════════════════════════════════════
  auto easeOut = [](float t) -> float {
    float inv = 1.0f - t;
    return 1.0f - (inv * inv * inv * inv);
  };
  auto easeInOut = [](float t) -> float {
    return t < 0.5f ? 2*t*t : 1 - (-2*t+2)*(-2*t+2)/2;
  };
  auto clampf = [](float v, float lo, float hi) -> float {
    return v < lo ? lo : (v > hi ? hi : v);
  };

  auto drawCard = [&](int cx, int cy, float scale) {
    int w = (int)(40*scale), h = (int)(26*scale);
    int x0 = cx - w/2, y0 = cy - h/2;
    int r = (int)(3*scale); if (r < 1) r = 1;
    u8g2.drawRFrame(x0, y0, w, h, r);
    int chipX = x0 + (int)(8*scale), chipY = y0 + (int)(7*scale);
    int chipW = (int)(11*scale); if (chipW < 3) chipW = 3;
    int chipH = (int)(8*scale);  if (chipH < 2) chipH = 2;
    u8g2.drawRFrame(chipX, chipY, chipW, chipH, 1);
    if (scale > 0.4f) {
      int lineY = y0 + (int)(13*scale);
      u8g2.drawHLine(x0 + (int)(22*scale), lineY, (int)(14*scale));
    }
  };

  auto drawSignal = [&](int cx, int cy, int rings, float progress) {
    for (int i = 0; i < rings; i++) {
      float ringT = clampf((progress * rings) - i, 0.0f, 1.0f);
      if (ringT <= 0.0f) break;
      int r = 10 + i * 9;
      u8g2.drawCircle(cx, cy, r, U8G2_DRAW_UPPER_LEFT | U8G2_DRAW_LOWER_LEFT);
    }
  };

  const unsigned long SPLASH_TOTAL = 3300UL;
  const unsigned long FRAME_MS     = 33UL;
  unsigned long splashStart = millis();
  const int CARD_SMALL_CX = 13, CARD_SMALL_CY = 7;
  const float CARD_SMALL_S = 0.45f;
  const int CARD_BIG_CX = 64, CARD_BIG_CY = 31;
  char infoBuf[30];
  snprintf(infoBuf, sizeof(infoBuf), "%d user  |  %s", userCount, AP_DOMAIN);
  bool beepDone = false;

  while (true) {
    unsigned long now = millis();
    unsigned long elapsed = now - splashStart;
    if (elapsed > SPLASH_TOTAL) break;
    float T = (float)elapsed / (float)SPLASH_TOTAL;

    float t1   = clampf(T / 0.12f, 0.0f, 1.0f);
    float cardS = easeOut(t1);
    float t2   = clampf((T - 0.12f) / 0.33f, 0.0f, 1.0f);
    float sigProg = easeInOut(t2) * 3.0f;
    float t3   = clampf((T - 0.40f) / 0.20f, 0.0f, 1.0f);
    float t3e  = easeOut(t3);
    int cardCX = (int)(CARD_BIG_CX + (CARD_SMALL_CX - CARD_BIG_CX) * t3e);
    int cardCY = (int)(CARD_BIG_CY + (CARD_SMALL_CY - CARD_BIG_CY) * t3e);
    float cardSc = cardS * (1.0f - t3e) + CARD_SMALL_S * t3e;
    float t3b  = clampf((T - 0.45f) / 0.20f, 0.0f, 1.0f);
    int lineY  = (int)(-2 + 17 * easeOut(t3b));
    float t3c  = clampf((T - 0.50f) / 0.22f, 0.0f, 1.0f);
    int titleY = (int)(-15 + 29 * easeOut(t3c));
    float t3d  = clampf((T - 0.58f) / 0.20f, 0.0f, 1.0f);
    int subY   = (int)(-8 + 50 * easeOut(t3d));
    float t4   = clampf((T - 0.72f) / 0.24f, 0.0f, 1.0f);
    float barP = easeInOut(t4);
    int barW   = (int)(118.0f * barP);
    bool showInfo = (T >= 0.78f);

    if (!beepDone && T >= 0.72f) { beepBoot(); beepDone = true; }

    u8g2.clearBuffer();
    if (T < 0.42f) {
      drawCard(CARD_BIG_CX, CARD_BIG_CY, cardS);
      if (t2 > 0.0f) drawSignal(CARD_BIG_CX - 20, CARD_BIG_CY, 3, sigProg);
    } else {
      if (lineY >= 0 && lineY <= 63) u8g2.drawHLine(0, lineY, 128);
      drawCard(cardCX, cardCY, cardSc);
      if (titleY > 0 && titleY < 70) {
        u8g2.setFont(u8g2_font_9x15_tf);
        const char* ttl = "ABSENSI";
        u8g2.drawStr((128 - u8g2.getStrWidth(ttl))/2, titleY, ttl);
      }
      if (subY > 0 && subY < 70) {
        u8g2.setFont(u8g2_font_6x10_tf);
        const char* sub = "ESP32  v2.0";
        u8g2.drawStr((128 - u8g2.getStrWidth(sub))/2, subY, sub);
      }
      if (T >= 0.72f) {
        u8g2.drawRFrame(4, 56, 120, 7, 2);
        if (barW > 0) u8g2.drawBox(5, 57, barW, 5);
        if (barP < 0.99f) {
          int dotX = 5 + barW;
          if (dotX < 122) u8g2.drawDisc(dotX, 59, 2);
        }
      }
      if (showInfo) {
        u8g2.setFont(u8g2_font_4x6_tf);
        u8g2.drawStr((128 - u8g2.getStrWidth(infoBuf))/2, 53, infoBuf);
      }
    }
    u8g2.sendBuffer();

    unsigned long spent = millis() - now;
    if (spent < FRAME_MS) delay(FRAME_MS - spent);
  }

  u8g2.setDrawColor(2);
  u8g2.drawBox(0, 0, 128, 64);
  u8g2.sendBuffer();
  delay(60);
  u8g2.setDrawColor(1);
  u8g2.clearBuffer();
  u8g2.sendBuffer();
  delay(40);

  goTo(MODE_ATTEND);
  lastAction = millis();
  lastFrame  = millis();
  Serial.printf("[BOOT] Siap. %d users.\n", userCount);
}

// ┌──────────────────────────────────────────────────────┐
//   MAIN LOOP
// └──────────────────────────────────────────────────────┘
void loop() {
  dns.processNextRequest();
  server.handleClient();
  ws.loop();
  readButtons();

  unsigned long now = millis();

  static unsigned long idleRef = 0;
  if (currentMode == MODE_ATTEND && now - idleRef > 1000) {
    idleRef = now;
    displayAttend();
    wsBroadcastStatus();
  }

  if ((currentMode == MODE_ADMIN_REGISTER || currentMode == MODE_ADMIN_DELETE) &&
       now - lastFrame > 380) {
    lastFrame = now; dotStep = (dotStep+1) % 3;
    if (currentMode == MODE_ADMIN_REGISTER) displayAdminScan("   REGISTER   ");
    else                                    displayAdminScan("  HAPUS KARTU  ");
  }

  static unsigned long attendOKStart = 0;
  if (currentMode == MODE_ATTEND_OK) {
    static unsigned long lastBar = 0;
    if (now - lastBar > 50) { lastBar = now; updateAttendOKBar(now - attendOKStart); }
    if (now - attendOKStart > RESULT_TIMEOUT) goTo(MODE_ATTEND);
  }

  if (currentMode == MODE_ATTEND_FAIL && now - lastAction > RESULT_TIMEOUT)
    goTo(MODE_ATTEND);

  bool inAdmin = (currentMode == MODE_ADMIN_MENU     || currentMode == MODE_ADMIN_REGISTER ||
                  currentMode == MODE_ADMIN_DELETE    || currentMode == MODE_ADMIN_DEL_CONFIRM);
  if (inAdmin && now - lastAction > MENU_TIMEOUT) goTo(MODE_ATTEND);

  if ((currentMode == MODE_RESULT_OK || currentMode == MODE_RESULT_FAIL) &&
       now - lastAction > RESULT_TIMEOUT) goTo(MODE_ADMIN_MENU);

  // ── Tombol ────────────────────────────────────────────
  if (btnLHeld) {
    beepClick();
    if (currentMode == MODE_ATTEND || currentMode == MODE_ATTEND_OK ||
        currentMode == MODE_ATTEND_FAIL) goTo(MODE_ADMIN_MENU);
    else                                 goTo(MODE_ATTEND);
  }

  if (btnLPressed) {
    beepClick(); lastAction = now;
    switch (currentMode) {
      case MODE_ADMIN_MENU:
        menuIndex = (menuIndex+1) % ADMIN_COUNT; displayAdminMenu(); break;
      case MODE_ADMIN_REGISTER:
      case MODE_ADMIN_DELETE:
        goTo(MODE_ADMIN_MENU); break;
      case MODE_ADMIN_DEL_CONFIRM:
        deleteTarget = -1; goTo(MODE_ADMIN_DELETE); break;
      case MODE_RESULT_OK:
      case MODE_RESULT_FAIL:
        goTo(MODE_ADMIN_MENU); break;
      default: break;
    }
  }

  if (btnRPressed) {
    beepClick(); lastAction = now;
    switch (currentMode) {
      case MODE_ADMIN_MENU:
        if      (menuIndex==0) goTo(MODE_ADMIN_REGISTER);
        else if (menuIndex==1) goTo(MODE_ADMIN_DELETE);
        break;
      case MODE_ADMIN_DEL_CONFIRM:
        if (deleteTarget >= 0) {
          char dname[NAME_SIZE];
          strncpy(dname, users[deleteTarget].name, NAME_SIZE);
          beepDelete();
          removeUser(deleteTarget);
          deleteTarget = -1;
          wsBroadcastUsers();
          char sub[28]; snprintf(sub, 28, "%s dihapus", dname);
          currentMode = MODE_RESULT_OK; lastAction = now;
          displayResult(true, "Terhapus!", sub);
        }
        break;
      case MODE_RESULT_OK:
      case MODE_RESULT_FAIL:
        goTo(MODE_ADMIN_MENU); break;
      default: break;
    }
  }

  // ── RFID ──────────────────────────────────────────────
  if (!rfid.PICC_IsNewCardPresent() || !rfid.PICC_ReadCardSerial()) return;
  if (now - lastScan < SCAN_COOLDOWN) {
    rfid.PICC_HaltA(); rfid.PCD_StopCrypto1(); return;
  }
  lastScan = now; lastAction = now;

  byte*  uid   = rfid.uid.uidByte;
  byte   uidSz = rfid.uid.size;
  String uStr  = uidToStr(uid, uidSz);
  Serial.printf("[RFID] %s\n", uStr.c_str());

  if (currentMode == MODE_ATTEND || currentMode == MODE_ATTEND_OK ||
      currentMode == MODE_ATTEND_FAIL) {
    int idx = findUser(uid);
    if (idx >= 0) {
      beepOK();
      addLog(users[idx].name, uStr.c_str());
      wsBroadcastAttend(users[idx].name, uStr.c_str());
      wsBroadcastStatus();
      Serial.printf("[ABSEN] %s\n", users[idx].name);
      displayAttendOK(users[idx].name);
      currentMode = MODE_ATTEND_OK;
      attendOKStart = millis(); lastAction = millis();
    } else {
      beepFail();
      displayAttendFail(uStr.c_str());
      currentMode = MODE_ATTEND_FAIL; lastAction = millis();
    }
    rfid.PICC_HaltA(); rfid.PCD_StopCrypto1(); return;
  }

  if (currentMode == MODE_ADMIN_REGISTER) {
    int idx = findUser(uid);
    if (idx >= 0) {
      beepFail(); currentMode = MODE_RESULT_FAIL; lastAction = now;
      displayResult(false, "Sudah Ada!", users[idx].name, uStr.c_str());
    } else if (userCount >= MAX_USERS) {
      beepFail(); currentMode = MODE_RESULT_FAIL; lastAction = now;
      displayResult(false, "Penuh!", "Max 50 user");
    } else {
      addUser(uid);
      beepOK();
      wsBroadcastUsers(); wsBroadcastStatus();
      Serial.printf("[REG] %s | %s\n", users[userCount-1].name, uStr.c_str());
      currentMode = MODE_RESULT_OK; lastAction = now;
      displayResult(true, users[userCount-1].name, "Terdaftar!", "Ganti nama di web");
    }
    rfid.PICC_HaltA(); rfid.PCD_StopCrypto1(); return;
  }

  if (currentMode == MODE_ADMIN_DELETE) {
    int idx = findUser(uid);
    if (idx >= 0) {
      deleteTarget = idx; currentMode = MODE_ADMIN_DEL_CONFIRM;
      lastAction = now; displayDeleteConfirm(idx);
    } else {
      beepFail(); currentMode = MODE_RESULT_FAIL; lastAction = now;
      displayResult(false, "Tdk Dikenal!", uStr.c_str());
    }
    rfid.PICC_HaltA(); rfid.PCD_StopCrypto1(); return;
  }

  rfid.PICC_HaltA();
  rfid.PCD_StopCrypto1();
}
