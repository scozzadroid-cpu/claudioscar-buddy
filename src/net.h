// src/net.h — WiFi + web configuration page.
//
// Not connected to a network → the buddy opens its own WPA2 hotspot
// "claudioscar-buddy-XXXX" (password shown on the WIFI info page) with a
// captive portal, so phones pop the config page up automatically.
// Connected to your WiFi → same page at http://claudioscar-buddy.local
// (or the IP on the info page), behind HTTP basic auth (user "buddy",
// password = hotspot password).
//
// Header-only, included once from main.cpp after extras.h.
#pragma once
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <SD_MMC.h>
#include "web_page.h"
#include "web_wifi.h"

enum NetMode : uint8_t { NET_OFF, NET_CONNECTING, NET_STA, NET_AP };

static WebServer  _web(80);
static DNSServer  _dns;
static NetMode    _netMode = NET_OFF;
static bool       _webStarted = false;
static char       _wSsid[33] = "";
static char       _wPass[65] = "";
static char       _apPass[12] = "";
static char       _apSsid[32] = "";
static uint32_t   _netT0 = 0;          // when the current connect attempt started
static uint32_t   _netLastRetry = 0;   // AP mode: last background STA retry
static uint32_t   _netLostAt = 0;      // STA mode: when the link dropped
static bool       _netRestart = false; // reboot requested from the web page

static const uint32_t NET_CONNECT_MS = 20000;
static const uint32_t NET_RETRY_MS   = 60000;
static const uint32_t NET_LOST_MS    = 30000;
static const char*    NET_HOSTNAME   = "claudioscar-buddy";

void netSetCredentials(const char* ssid, const char* pass);
static bool charInstallFromSd(const char* name);
void otaRequestCheck();
void otaRequestInstall();
void otaSetAuto(bool on);
void otaStatusJson(JsonObject o);
void otaUploadChunk(HTTPUpload& up);
void usageSetToken(const char* t);
void usageSetPoll(uint16_t sec);
void usageRequestPoll();
void usageStatusJson(JsonObject o);
bool otaUploadResult(const char** err);

inline NetMode netMode() { return _netMode; }
inline const char* netApSsid() { return _apSsid; }
inline const char* netApPassword() { return _apPass; }
inline const char* netSsid() { return _wSsid; }

static void _netLoadCreds() {
  Preferences pr;
  pr.begin("buddy", false);
  pr.getString("w_ssid", _wSsid, sizeof(_wSsid));
  pr.getString("w_pass", _wPass, sizeof(_wPass));
  char legacy[12] = "";
  pr.getString("w_appw", legacy, sizeof(legacy));   // pre-1.0.0 location
  pr.end();
  // Hotspot password: generated on the very first boot and kept for good.
  // It lives in its own NVS namespace, which factory reset and OTA updates
  // never touch (only a full chip erase does).
  Preferences hw;
  hw.begin("buddy_hw", false);
  if (hw.getString("appw", _apPass, sizeof(_apPass)) < 8) {
    if (strlen(legacy) >= 8) strcpy(_apPass, legacy);
    else snprintf(_apPass, sizeof(_apPass), "%08lu", (unsigned long)(esp_random() % 100000000UL));
    hw.putString("appw", _apPass);
  }
  hw.end();
  uint8_t mac[6];
  esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
  snprintf(_apSsid, sizeof(_apSsid), "claudioscar-buddy-%02X%02X", mac[4], mac[5]);
}

static void _netStartAp() {
  // AP+STA even without saved credentials: the STA side is what scans
  // for networks on the config page.
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(_apSsid, _apPass);
  _dns.setErrorReplyCode(DNSReplyCode::NoError);
  _dns.start(53, "*", WiFi.softAPIP());
  _netMode = NET_AP;
  _netLastRetry = millis();
  Serial.printf("[net] hotspot %s up at %s\n", _apSsid, WiFi.softAPIP().toString().c_str());
}

static void _netStopAp() {
  _dns.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
}

static void _netMdns() {
  // Real UTC time for usage-reset countdowns (the RTC holds local time).
  configTime(0, 0, "pool.ntp.org", "time.google.com");
  static bool up = false;
  if (up) return;            // the responder survives reconnects
  up = MDNS.begin(NET_HOSTNAME);
  if (up) MDNS.addService("http", "tcp", 80);
}

static void _netStartSta() {
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(NET_HOSTNAME);
  WiFi.begin(_wSsid, _wPass);
  _netMode = NET_CONNECTING;
  _netT0 = millis();
  Serial.printf("[net] connecting to '%s'\n", _wSsid);
}

// ---------------------------------------------------------------- web handlers

static bool _webAuth() {
  // The hotspot is already WPA2-protected; on the home LAN ask for a password.
  if (_netMode == NET_AP) return true;
  if (_web.authenticate("buddy", _apPass)) return true;
  _web.requestAuthentication(BASIC_AUTH, "claudioscar-buddy");
  return false;
}

static void _webSendJson(JsonDocument& doc, int code = 200) {
  String out;
  serializeJson(doc, out);
  _web.send(code, "application/json", out);
}

static void _webStatus() {
  if (!_webAuth()) return;
  JsonDocument d;
  d["fw"] = FW_VERSION;
  d["board"] = BOARD_MODEL_LINE2;
  d["pet"] = petName();
  d["owner"] = ownerName();
  d["species"] = buddyMode ? buddySpeciesIdx() : 255;
  d["gif"] = gifAvailable;
  JsonArray sp = d["speciesList"].to<JsonArray>();
  for (uint8_t i = 0; i < buddySpeciesCount(); i++) sp.add(buddySpeciesNameAt(i));
  d["bright"] = brightLevel;
  d["sound"] = settings().sound;
  d["led"] = settings().led;
  d["hud"] = settings().hud;
  d["theme"] = extraSettings().theme;
  d["volume"] = extraSettings().volume;
  d["angry"] = extraSettings().angryPct;
  d["phrases"] = angryPhrases();
  d["flag"] = extraSettings().flag;
  {
    char a[6], b[6];
    snprintf(a, sizeof(a), "%02u:%02u", extraSettings().nightStart / 60, extraSettings().nightStart % 60);
    snprintf(b, sizeof(b), "%02u:%02u", extraSettings().nightEnd / 60, extraSettings().nightEnd % 60);
    d["night"]["on"] = extraSettings().nightOn;
    d["night"]["start"] = a;
    d["night"]["end"] = b;
    d["night"]["level"] = extraSettings().nightLevel;
    d["night"]["mute"] = extraSettings().nightMute;
    d["night"]["active"] = nightActive();
  }
  d["pack"] = soundPack();
  d["char"] = gifAvailable ? characterName() : "";
  otaStatusJson(d["ota"].to<JsonObject>());
  usageStatusJson(d["usage"].to<JsonObject>());
  d["now"] = (uint32_t)time(nullptr);
  d["sd"] = hwSdMounted();
  HwBattery hb = hwBattery();
  d["bat"]["pct"] = hb.pct;
  d["bat"]["mV"] = hb.mV;
  d["bat"]["usb"] = hb.usbPresent;
  d["bat"]["charging"] = hb.charging;
  d["bt"]["linked"] = dataBtActive();
  d["bt"]["name"] = btName;
  d["up"] = millis() / 1000;
  d["heap"] = ESP.getFreeHeap();
  d["stats"]["approved"] = stats().approvals;
  d["stats"]["denied"] = stats().denials;
  d["stats"]["level"] = stats().level;
  d["wifi"]["mode"] = _netMode == NET_STA ? "sta" : _netMode == NET_AP ? "ap" : _netMode == NET_CONNECTING ? "connecting" : "off";
  d["wifi"]["ssid"] = _wSsid;
  d["wifi"]["ip"] = _netMode == NET_STA ? WiFi.localIP().toString() : WiFi.softAPIP().toString();
  d["wifi"]["rssi"] = _netMode == NET_STA ? WiFi.RSSI() : 0;
  d["wifi"]["ap"] = _apSsid;
  _webSendJson(d);
}

// "23:30" -> minutes after midnight (fallback on bad input).
static uint16_t _hhmm(const char* s, uint16_t fallback) {
  int h, m;
  if (!s || sscanf(s, "%d:%d", &h, &m) != 2 || h < 0 || h > 23 || m < 0 || m > 59) return fallback;
  return h * 60 + m;
}

// Apply a settings object (web page or USB {"cmd":"set",...}); unknown keys ignored.
void settingsApplyJson(JsonVariantConst in) {
  if (in["pet"].is<const char*>())   petNameSet(in["pet"]);
  if (in["owner"].is<const char*>()) ownerSet(in["owner"]);
  if (in["species"].is<int>()) {
    uint8_t idx = in["species"].as<int>();
    if (idx == 255 && !gifAvailable) idx = 0;
    if (idx != 255 && idx >= buddySpeciesCount()) idx = 0;
    speciesIdxSave(idx);
    buddyMode = !(gifAvailable && idx == 255);
    if (buddyMode) buddySetSpeciesIdx(idx);
    characterInvalidate();
    if (buddyMode) buddyInvalidate();
  }
  if (in["bright"].is<int>()) { brightLevel = constrain(in["bright"].as<int>(), 0, 4); applyBrightness(); }
  Settings& s = settings();
  if (in["sound"].is<bool>()) s.sound = in["sound"];
  if (in["led"].is<bool>())   s.led = in["led"];
  if (in["hud"].is<bool>())   s.hud = in["hud"];
  settingsSave();
  ExtraSettings& x = extraSettings();
  if (in["theme"].is<int>())  x.theme = constrain(in["theme"].as<int>(), 0, 2);
  if (in["pack"].is<const char*>()) soundPackSet(in["pack"]);
  if (in["volume"].is<int>()) x.volume = constrain(in["volume"].as<int>(), 0, 100);
  if (in["angry"].is<int>())  x.angryPct = constrain(in["angry"].as<int>(), 0, 100);
  if (in["phrases"].is<const char*>()) angryPhrasesSet(in["phrases"]);
  if (in["flag"].is<int>())   x.flag = constrain(in["flag"].as<int>(), 0, FLAG_COUNT - 1);
  if (in["otaAuto"].is<bool>()) otaSetAuto(in["otaAuto"]);
  if (in["nightOn"].is<bool>())   x.nightOn = in["nightOn"];
  if (in["nightStart"].is<const char*>()) x.nightStart = _hhmm(in["nightStart"], x.nightStart);
  if (in["nightEnd"].is<const char*>())   x.nightEnd = _hhmm(in["nightEnd"], x.nightEnd);
  if (in["nightLevel"].is<int>()) x.nightLevel = constrain(in["nightLevel"].as<int>(), 1, 80);
  if (in["nightMute"].is<bool>()) x.nightMute = in["nightMute"];
  if (in["usageToken"].is<const char*>()) usageSetToken(in["usageToken"]);
  if (in["usagePoll"].is<int>()) usageSetPoll(in["usagePoll"].as<int>());
  extrasSave();
  characterInvalidate();
}

static void _webSaveSettings() {
  if (!_webAuth()) return;
  JsonDocument in;
  if (deserializeJson(in, _web.arg("plain"))) { _web.send(400, "text/plain", "bad json"); return; }
  settingsApplyJson(in.as<JsonVariantConst>());
  _web.send(200, "application/json", "{\"ok\":true}");
}

// Asynchronous scan: ?start=1 (or no previous scan) kicks one off and
// answers {"state":"scanning"}; poll until {"state":"done","nets":[...]}.
static void _webScan() {
  if (!_webAuth()) return;
  int n = WiFi.scanComplete();
  if (n == WIFI_SCAN_RUNNING) { _web.send(200, "application/json", "{\"state\":\"scanning\"}"); return; }
  if (_web.hasArg("start") || n < 0) {
    WiFi.scanDelete();
    WiFi.scanNetworks(true /* async */, false);
    _web.send(200, "application/json", "{\"state\":\"scanning\"}");
    return;
  }
  JsonDocument d;
  d["state"] = "done";
  JsonArray a = d["nets"].to<JsonArray>();
  for (int i = 0; i < n; i++) {
    String ssid = WiFi.SSID(i);
    if (!ssid.length()) continue;            // hidden networks
    bool dup = false;                        // same SSID on several APs: keep the strongest
    for (JsonObject o : a) {
      if (ssid == (const char*)o["ssid"]) { if (WiFi.RSSI(i) > (int)o["rssi"]) o["rssi"] = WiFi.RSSI(i); dup = true; break; }
    }
    if (dup || a.size() >= 25) continue;
    JsonObject o = a.add<JsonObject>();
    o["ssid"] = ssid;
    o["rssi"] = WiFi.RSSI(i);
    o["open"] = WiFi.encryptionType(i) == WIFI_AUTH_OPEN;
  }
  _webSendJson(d);
}

static void _webWifiPage() {
  if (!_webAuth()) return;
  _web.send_P(200, "text/html", WEB_WIFI_PAGE);
}

static void _webSaveWifi() {
  if (!_webAuth()) return;
  JsonDocument in;
  if (deserializeJson(in, _web.arg("plain"))) { _web.send(400, "text/plain", "bad json"); return; }
  netSetCredentials(in["ssid"] | "", in["pass"] | "");
  _web.send(200, "application/json", "{\"ok\":true}");
}

static void _webAction() {
  if (!_webAuth()) return;
  JsonDocument in;
  deserializeJson(in, _web.arg("plain"));
  const char* a = in["do"] | "";
  if (!strcmp(a, "time")) {
    // Browser clock: {"do":"time","epoch":..,"tz":..} — same as the bridge.
    char j[64];
    snprintf(j, sizeof(j), "{\"time\":[%lu,%ld]}", (unsigned long)(in["epoch"] | 0UL), (long)(in["tz"] | 0L));
    _applyJson(j, &tama);
  } else if (!strcmp(a, "ota_check")) {
    otaRequestCheck();
  } else if (!strcmp(a, "usage_poll")) {
    usageRequestPoll();
  } else if (!strcmp(a, "ota_install")) {
    otaRequestInstall();
  } else if (!strcmp(a, "char")) {
    if (!charInstallFromSd(in["name"] | "")) { _web.send(500, "application/json", "{\"ok\":false}"); return; }
  } else if (!strcmp(a, "unpair")) {
    bleClearBonds();
  } else if (!strcmp(a, "reboot")) {
    _netRestart = true;
  } else if (!strcmp(a, "test")) {
    uint8_t ev = in["ev"] | 0;
    if (ev == SFX_ANGRY) angryOutburst();
    else { hwAudioStop(); sfxPlay((SfxEvent)ev); }
    wake();
  } else {
    _web.send(400, "text/plain", "unknown action");
    return;
  }
  _web.send(200, "application/json", "{\"ok\":true}");
}

// Upload a custom WAV for an event: POST /api/sound?ev=<name> (multipart).
// Stored on the SD card if present, else LittleFS. ?del=1 removes it.
static File _upFile;
static bool _upOk = false;
static bool _webSoundPath(char* path, size_t n, bool sd) {
  String ev = _web.arg("ev");
  for (uint8_t i = 0; i < SFX_COUNT; i++) {
    if (ev == SFX_NAMES[i]) { snprintf(path, n, "/sounds/%s.wav", SFX_NAMES[i]); return true; }
  }
  return false;
}
static void _webSoundUpload() {
  HTTPUpload& up = _web.upload();
  if (up.status == UPLOAD_FILE_START) {
    _upOk = false;
    if (_netMode != NET_AP && !_web.authenticate("buddy", _apPass)) return;
    char path[48];
    if (!_webSoundPath(path, sizeof(path), hwSdMounted())) return;
    if (hwSdMounted()) { SD_MMC.mkdir("/sounds"); _upFile = SD_MMC.open(path, FILE_WRITE); }
    else { LittleFS.mkdir("/sounds"); _upFile = LittleFS.open(path, "w"); }
    _upOk = (bool)_upFile;
    Serial.printf("[web] upload %s -> %s%s (%s)\n", up.filename.c_str(), hwSdMounted() ? "sd:" : "flash:", path, _upOk ? "ok" : "open failed");
  } else if (up.status == UPLOAD_FILE_WRITE) {
    if (_upFile && _upFile.write(up.buf, up.currentSize) != up.currentSize) _upOk = false;
  } else if (up.status == UPLOAD_FILE_END || up.status == UPLOAD_FILE_ABORTED) {
    if (_upFile) _upFile.close();
    if (up.status == UPLOAD_FILE_ABORTED) _upOk = false;
    Serial.printf("[web] upload end %u bytes %s\n", (unsigned)up.totalSize, _upOk ? "ok" : "FAILED");
  }
}
static void _webSoundDone() {
  if (!_webAuth()) return;
  if (_web.hasArg("del")) {
    char path[48];
    if (_webSoundPath(path, sizeof(path), false)) {
      SD_MMC.remove(path);
      LittleFS.remove(path);
    }
    _web.send(200, "application/json", "{\"ok\":true}");
    return;
  }
  _web.send(_upOk ? 200 : 500, "application/json", _upOk ? "{\"ok\":true}" : "{\"ok\":false}");
}

static void _webSounds() {
  if (!_webAuth()) return;
  JsonDocument d;
  for (uint8_t i = 0; i < SFX_COUNT; i++) {
    char path[48]; snprintf(path, sizeof(path), "/sounds/%s.wav", SFX_NAMES[i]);
    const char* where = "";
    if (hwSdMounted() && SD_MMC.exists(path)) where = "sd";
    else if (LittleFS.exists(path)) where = "flash";
    d[SFX_NAMES[i]] = where;
  }
  _webSendJson(d);
}

// SD library: list sub-directories of /soundpacks and /characters.
static void _sdListDirs(JsonArray out, const char* root) {
  if (!hwSdMounted()) return;
  File r = SD_MMC.open(root);
  if (!r || !r.isDirectory()) return;
  for (File e = r.openNextFile(); e; e = r.openNextFile()) {
    if (e.isDirectory()) out.add(String(e.name()));
    e.close();
  }
  r.close();
}

static void _webLibrary() {
  if (!_webAuth()) return;
  JsonDocument d;
  _sdListDirs(d["packs"].to<JsonArray>(), "/soundpacks");
  _sdListDirs(d["chars"].to<JsonArray>(), "/characters");
  _webSendJson(d);
}

// Copy /characters/<name>/ from the SD card into LittleFS (replacing the
// installed character — flash holds one at a time) and switch to it.
static bool charInstallFromSd(const char* name) {
  if (!hwSdMounted() || !name[0] || strchr(name, '/') || strstr(name, "..")) return false;
  char src[64]; snprintf(src, sizeof(src), "/characters/%s", name);
  File d = SD_MMC.open(src);
  if (!d || !d.isDirectory()) return false;
  characterClose();
  _xWipeAllChars();
  LittleFS.mkdir("/characters");
  LittleFS.mkdir(src);
  static uint8_t buf[2048];
  bool ok = true;
  for (File f = d.openNextFile(); f && ok; f = d.openNextFile()) {
    if (f.isDirectory()) { f.close(); continue; }
    char dst[96]; snprintf(dst, sizeof(dst), "%s/%s", src, f.name());
    File o = LittleFS.open(dst, "w");
    if (!o) ok = false;
    while (ok && f.available()) {
      int n = f.read(buf, sizeof(buf));
      if (n <= 0 || o.write(buf, n) != (size_t)n) ok = false;
      delay(0);
    }
    if (o) o.close();
    f.close();
  }
  d.close();
  ok = ok && characterInit(name);
  gifAvailable = characterLoaded();
  if (ok) { buddyMode = false; speciesIdxSave(SPECIES_GIF); }
  else buddyMode = true;
  characterInvalidate();
  return ok;
}

bool charInstallFromSdPublic(const char* n) { return charInstallFromSd(n); }

static bool _otaUpAuthed = false;
static void _webOtaUploadChunk() {
  HTTPUpload& up = _web.upload();
  if (up.status == UPLOAD_FILE_START)
    _otaUpAuthed = (_netMode == NET_AP) || _web.authenticate("buddy", _apPass);
  if (_otaUpAuthed) otaUploadChunk(up);
}
static void _webOtaUploadDone() {
  if (!_webAuth()) return;
  const char* err;
  if (!_otaUpAuthed || !otaUploadResult(&err)) {
    JsonDocument d; d["ok"] = false; d["error"] = _otaUpAuthed ? err : "unauthorized";
    _webSendJson(d, 400);
    return;
  }
  _web.send(200, "application/json", "{\"ok\":true}");
  _netRestart = true;   // reboot into the new image after the reply is sent
}

static void _webIndex() {
  if (!_webAuth()) return;
  _web.send_P(200, "text/html", WEB_PAGE);
}

// Captive portal: any unknown host/path while in hotspot mode → our page.
static void _webNotFound() {
  if (_netMode == NET_AP) {
    _web.sendHeader("Location", String("http://") + WiFi.softAPIP().toString() + "/", true);
    _web.send(302, "text/plain", "");
    return;
  }
  _web.send(404, "text/plain", "not found");
}

static void _webBegin() {
  if (_webStarted) return;
  _web.on("/", HTTP_GET, _webIndex);
  _web.on("/api/status", HTTP_GET, _webStatus);
  _web.on("/api/settings", HTTP_POST, _webSaveSettings);
  _web.on("/api/scan", HTTP_GET, _webScan);
  _web.on("/wifi", HTTP_GET, _webWifiPage);
  _web.on("/api/wifi", HTTP_POST, _webSaveWifi);
  _web.on("/api/action", HTTP_POST, _webAction);
  _web.on("/api/sounds", HTTP_GET, _webSounds);
  _web.on("/api/library", HTTP_GET, _webLibrary);
  _web.on("/api/sound", HTTP_POST, _webSoundDone, _webSoundUpload);
  _web.on("/api/ota/upload", HTTP_POST, _webOtaUploadDone, _webOtaUploadChunk);
  _web.onNotFound(_webNotFound);
  _web.begin();
  _webStarted = true;
}

// ---------------------------------------------------------------- public API

// One-line JSON for the USB configurator ({"cmd":"net"}).
const char* netInfoJson() {
  static char b[512];
  JsonDocument d;
  d["ack"] = "net";
  d["ok"] = true;
  d["mode"] = _netMode == NET_STA ? "sta" : _netMode == NET_AP ? "ap" : _netMode == NET_CONNECTING ? "connecting" : "off";
  d["ssid"] = _wSsid;
  d["ip"] = (_netMode == NET_STA ? WiFi.localIP() : WiFi.softAPIP()).toString();
  d["ap"] = _apSsid;
  d["appw"] = _apPass;
  otaStatusJson(d["ota"].to<JsonObject>());
  size_t n = serializeJson(d, b, sizeof(b) - 2);
  b[n] = '\n'; b[n + 1] = 0;
  return b;
}

void netSetCredentials(const char* ssid, const char* pass) {
  strncpy(_wSsid, ssid, sizeof(_wSsid) - 1); _wSsid[sizeof(_wSsid) - 1] = 0;
  strncpy(_wPass, pass, sizeof(_wPass) - 1); _wPass[sizeof(_wPass) - 1] = 0;
  Preferences pr;
  pr.begin("buddy", false);
  pr.putString("w_ssid", _wSsid);
  pr.putString("w_pass", _wPass);
  pr.end();
  if (_netMode == NET_OFF) return;
  // Try the new network right away; the hotspot stays up meanwhile so the
  // page that sent this keeps working until we actually get a link.
  if (_wSsid[0]) {
    if (_netMode == NET_AP) { WiFi.mode(WIFI_AP_STA); WiFi.begin(_wSsid, _wPass); _netLastRetry = millis(); }
    else _netStartSta();
  } else if (_netMode != NET_AP) {
    WiFi.disconnect(true);
    _netStartAp();
  }
}

void netSetEnabled(bool on) {
  if (on && _netMode == NET_OFF) {
    _netLoadCreds();
    if (_wSsid[0]) _netStartSta(); else _netStartAp();
    _webBegin();
  } else if (!on && _netMode != NET_OFF) {
    if (_netMode == NET_AP) _dns.stop();
    WiFi.softAPdisconnect(true);
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    _netMode = NET_OFF;
  }
}

// mbedTLS in this core allocates from internal RAM only; with BLE, WiFi and
// the GIF decoder running that leaves too little contiguous memory for a
// handshake (HTTPClient then fails with -1). Route its allocations to PSRAM,
// internal RAM only as a fallback.
extern "C" int mbedtls_platform_set_calloc_free(void* (*)(size_t, size_t), void (*)(void*));
static void* _tlsCalloc(size_t n, size_t sz) {
  void* p = heap_caps_calloc(n, sz, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  return p ? p : heap_caps_calloc(n, sz, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
}

// Local timezone offset learned from the last time sync (browser, Claude
// Desktop or USB), so NTP can keep the RTC right on its own.
static int32_t _tzOff = 0;
static bool    _tzKnown = false;
void netRememberTz(int32_t off) {
  if (_tzKnown && off == _tzOff) return;
  _tzOff = off; _tzKnown = true;
  Preferences pr;
  pr.begin("buddy", false);
  pr.putInt("x_tz", off);
  pr.end();
}

static void _netNtpToRtc() {
  static uint32_t last = 0;
  if (!_tzKnown || (last && millis() - last < 3600000UL)) return;
  time_t utc = time(nullptr);
  if (utc < 1700000000) return;                 // NTP not answered yet
  last = millis();
  char j[48];
  snprintf(j, sizeof(j), "{\"time\":[%lu,%ld]}", (unsigned long)utc, (long)_tzOff);
  _applyJson(j, &tama);
  Serial.println("[net] RTC set from NTP");
}

inline void netBegin() {
  mbedtls_platform_set_calloc_free(_tlsCalloc, heap_caps_free);
  {
    Preferences pr;
    pr.begin("buddy", true);
    if (pr.isKey("x_tz")) { _tzOff = pr.getInt("x_tz", 0); _tzKnown = true; }
    pr.end();
  }
  _netLoadCreds();
  if (settings().wifi) netSetEnabled(true);
}

inline void netLoop() {
  if (_netMode == NET_OFF) return;
  uint32_t now = millis();
  bool linked = WiFi.status() == WL_CONNECTED;

  switch (_netMode) {
    case NET_CONNECTING:
      if (linked) {
        _netMode = NET_STA;
        _netMdns();
        Serial.printf("[net] connected, http://%s.local  %s\n", NET_HOSTNAME, WiFi.localIP().toString().c_str());
      } else if (now - _netT0 > NET_CONNECT_MS) {
        Serial.println("[net] connect timeout -> hotspot");
        _netStartAp();
      }
      break;
    case NET_STA:
      if (linked) { _netLostAt = 0; _netNtpToRtc(); }
      else if (!_netLostAt) _netLostAt = now;
      else if (now - _netLostAt > NET_LOST_MS) { Serial.println("[net] link lost -> hotspot"); _netStartAp(); }
      break;
    case NET_AP:
      _dns.processNextRequest();
      if (linked) {
        // A background retry (or new credentials) got through.
        _netStopAp();
        _netMode = NET_STA;
        _netLostAt = 0;
        _netMdns();
        Serial.printf("[net] connected, http://%s.local  %s\n", NET_HOSTNAME, WiFi.localIP().toString().c_str());
      } else if (_wSsid[0] && WiFi.softAPgetStationNum() == 0 && now - _netLastRetry > NET_RETRY_MS) {
        // Retry the saved network only while nobody is on the hotspot:
        // STA scanning hops channels and would kick phones off the AP.
        _netLastRetry = now;
        WiFi.begin(_wSsid, _wPass);
      }
      break;
    default: break;
  }
  _web.handleClient();
  if (_netRestart) { delay(200); ESP.restart(); }
}
