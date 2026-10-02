// src/ota.h — over-the-air updates from the project's GitHub releases.
//
// The buddy asks api.github.com for the latest release of OTA_REPO, and if
// its tag is newer than FW_VERSION and it carries an asset named OTA_ASSET
// (the app-only firmware.bin for this board), downloads and flashes it into
// the spare OTA slot. TLS is verified against the ESP-IDF root CA bundle;
// the image itself is checked (SHA-256) by the Update library before boot.
//
// Checks run once after the WiFi link comes up and then every 24 h. Installs
// only happen when asked (web page button) unless auto-update is enabled.
//
// Header-only, included once from main.cpp after net.h.
#pragma once
#include <HTTPClient.h>
#include <NetworkClientSecure.h>
#include <Update.h>

#ifndef OTA_REPO
  #define OTA_REPO ""
#endif
#ifndef OTA_ASSET
  #define OTA_ASSET ""
#endif

extern const uint8_t _ota_ca_start[] asm("_binary_x509_crt_bundle_start");
extern const uint8_t _ota_ca_end[]   asm("_binary_x509_crt_bundle_end");

enum OtaState : uint8_t { OTA_IDLE, OTA_UPTODATE, OTA_AVAILABLE, OTA_INSTALLING, OTA_FAILED };

static OtaState _otaState = OTA_IDLE;
static char     _otaLatest[24] = "";
static char     _otaUrl[256] = "";
static char     _otaErr[64] = "";
static uint32_t _otaLastCheck = 0;
static bool     _otaAuto = false;
static bool     _otaCheckNow = false, _otaInstallNow = false;
static uint8_t  _otaPct = 0;

static const uint32_t OTA_CHECK_EVERY_MS = 24UL * 60UL * 60UL * 1000UL;

inline bool otaConfigured() { return OTA_REPO[0] && OTA_ASSET[0]; }

inline void otaLoadPrefs() {
  Preferences pr;
  pr.begin("buddy", true);
  _otaAuto = pr.getBool("o_auto", false);
  pr.end();
}

void otaSetAuto(bool on) {
  _otaAuto = on;
  Preferences pr;
  pr.begin("buddy", false);
  pr.putBool("o_auto", on);
  pr.end();
}

// "1.2.10" > "1.2.9". Non-numeric suffixes are ignored.
static bool _otaNewer(const char* a, const char* b) {
  int x[3] = {0}, y[3] = {0};
  sscanf(a, "%d.%d.%d", &x[0], &x[1], &x[2]);
  sscanf(b, "%d.%d.%d", &y[0], &y[1], &y[2]);
  for (int i = 0; i < 3; i++) if (x[i] != y[i]) return x[i] > y[i];
  return false;
}

static void _otaFail(const char* why) {
  strncpy(_otaErr, why, sizeof(_otaErr) - 1);
  _otaErr[sizeof(_otaErr) - 1] = 0;
  _otaState = OTA_FAILED;
  Serial.printf("[ota] %s\n", why);
}

static void _otaClient(NetworkClientSecure& c) {
  c.setCACertBundle(_ota_ca_start, _ota_ca_end - _ota_ca_start);
  c.setTimeout(15);
}

static void _otaCheck() {
  _otaLastCheck = millis();
  if (!otaConfigured()) { _otaFail("no release repo configured"); return; }
  if (netMode() != NET_STA) { _otaFail("not connected to a WiFi network"); return; }
  NetworkClientSecure tls; _otaClient(tls);
  HTTPClient http;
  http.begin(tls, "https://api.github.com/repos/" OTA_REPO "/releases/latest");
  http.setUserAgent("claudioscar-buddy/" FW_VERSION);
  http.addHeader("Accept", "application/vnd.github+json");
  int code = http.GET();
  if (code != 200) { char b[48]; snprintf(b, sizeof(b), "release check failed (HTTP %d)", code); _otaFail(b); http.end(); return; }

  // Keep only what we need out of a potentially large response.
  JsonDocument filter;
  filter["tag_name"] = true;
  filter["assets"][0]["name"] = true;
  filter["assets"][0]["browser_download_url"] = true;
  JsonDocument doc;
  DeserializationError e = deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter));
  http.end();
  if (e) { _otaFail("bad release data"); return; }

  const char* tag = doc["tag_name"] | "";
  if (*tag == 'v' || *tag == 'V') tag++;
  strncpy(_otaLatest, tag, sizeof(_otaLatest) - 1);
  _otaUrl[0] = 0;
  for (JsonObject a : doc["assets"].as<JsonArray>()) {
    if (strcmp(a["name"] | "", OTA_ASSET) == 0) {
      strncpy(_otaUrl, a["browser_download_url"] | "", sizeof(_otaUrl) - 1);
      break;
    }
  }
  if (!_otaNewer(_otaLatest, FW_VERSION)) { _otaState = OTA_UPTODATE; }
  else if (!_otaUrl[0]) { _otaFail("new release has no file for this board"); }
  else { _otaState = OTA_AVAILABLE; Serial.printf("[ota] %s available\n", _otaLatest); }
}

static void _otaDrawProgress(const char* line) {
  Arduino_Canvas* c = hwCanvas();
  c->fillScreen(0x0000);
  drawCenteredText("updating", W / 2, H / 2 - 30, 2, 0xFFFF, 0x0000);
  drawCenteredText(line, W / 2, H / 2, 2, HOT, 0x0000);
  drawCenteredText("don't unplug", W / 2, H / 2 + 30, 1, 0x8410, 0x0000);
  int bw = W - 40;
  c->drawRect(20, H / 2 + 46, bw, 8, 0x8410);
  c->fillRect(21, H / 2 + 47, (bw - 2) * _otaPct / 100, 6, HOT);
  hwDisplayPush();
}

static void _otaInstall() {
  if (_otaState != OTA_AVAILABLE || !_otaUrl[0]) return;
  _otaState = OTA_INSTALLING;
  _otaPct = 0;
  hwAudioStop();
  wake();
  char line[24]; snprintf(line, sizeof(line), "v%s", _otaLatest);
  _otaDrawProgress(line);

  NetworkClientSecure tls; _otaClient(tls);
  HTTPClient http;
  // Asset URLs redirect to GitHub's object storage.
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.begin(tls, _otaUrl);
  http.setUserAgent("claudioscar-buddy/" FW_VERSION);
  int code = http.GET();
  if (code != 200) { char b[48]; snprintf(b, sizeof(b), "download failed (HTTP %d)", code); _otaFail(b); http.end(); characterInvalidate(); return; }
  int len = http.getSize();
  if (len <= 0 || !Update.begin(len, U_FLASH)) { _otaFail("not enough space for update"); http.end(); characterInvalidate(); return; }

  NetworkClient* s = http.getStreamPtr();
  static uint8_t buf[2048];
  int got = 0;
  uint32_t lastData = millis();
  while (got < len && http.connected() && millis() - lastData < 20000) {
    size_t n = s->available();
    if (!n) { delay(2); continue; }
    n = s->readBytes(buf, min(n, sizeof(buf)));
    if (Update.write(buf, n) != n) break;
    got += n;
    lastData = millis();
    uint8_t pct = (uint64_t)got * 100 / len;
    if (pct != _otaPct) { _otaPct = pct; _otaDrawProgress(line); }
  }
  http.end();
  if (got != len || !Update.end(true)) {
    Update.abort();
    _otaFail(Update.hasError() ? Update.errorString() : "download interrupted");
    characterInvalidate();
    return;
  }
  Serial.println("[ota] update OK, rebooting");
  drawCenteredText("done!", W / 2, H / 2 + 70, 2, GREEN, 0x0000);
  hwDisplayPush();
  delay(800);
  ESP.restart();
}

void otaRequestCheck()   { _otaCheckNow = true; }
void otaRequestInstall() { _otaInstallNow = true; }

// Call from loop(); does the slow network work outside the web handler so
// the HTTP reply goes out before the download starts.
inline void otaLoop() {
  static bool wasLinked = false;
  bool linked = netMode() == NET_STA;
  uint32_t now = millis();
  if (linked && otaConfigured()) {
    bool due = !wasLinked || (now - _otaLastCheck > OTA_CHECK_EVERY_MS);
    if (_otaCheckNow || (due && _otaState != OTA_INSTALLING)) {
      _otaCheckNow = false;
      _otaCheck();
      if (_otaState == OTA_AVAILABLE && _otaAuto) _otaInstallNow = true;
    }
  } else if (_otaCheckNow) {
    _otaCheckNow = false;
    _otaFail(otaConfigured() ? "not connected to a WiFi network" : "no release repo configured");
  }
  wasLinked = linked;
  if (_otaInstallNow) { _otaInstallNow = false; _otaInstall(); }
}

void otaStatusJson(JsonObject o) {
  static const char* NAMES[] = { "idle", "uptodate", "available", "installing", "failed" };
  o["state"] = NAMES[_otaState];
  o["current"] = FW_VERSION;
  o["latest"] = _otaLatest;
  o["repo"] = OTA_REPO;
  o["auto"] = _otaAuto;
  o["pct"] = _otaPct;
  o["error"] = _otaErr;
}
