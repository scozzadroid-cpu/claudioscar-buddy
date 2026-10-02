// src/usage.h — Claude Pro/Max plan usage (5-hour and weekly windows),
// fetched by the buddy itself over WiFi. No PC needed.
//
// The user pastes a long-lived token created with `claude setup-token` on
// the web page. Every poll the buddy first asks the usage endpoint behind
// Claude Code's /usage (costs nothing); if that isn't available for the
// token, it falls back to a 1-token Messages request and reads the
// anthropic-ratelimit-unified-* response headers, as claude-usage-stick and
// Clawdmeter do. Both are UNOFFICIAL, undocumented interfaces: they may
// change or stop working, and using a subscription token outside Claude
// Code may conflict with Anthropic's terms. Opt-in, at your own risk.
//
// Header-only, included once from main.cpp after ota.h.
#pragma once
#include <HTTPClient.h>
#include <NetworkClientSecure.h>

static char     _uTok[256] = "";
static uint16_t _uPollSec = 300;
static float    _u5h = -1, _u7d = -1;        // percent, -1 = unknown
static uint32_t _u5hReset = 0, _u7dReset = 0; // epoch seconds
static uint32_t _uLastOkMs = 0, _uLastTryMs = 0;
static bool     _uPollNow = false;
static char     _uErr[48] = "";
static char     _uVia[12] = "";
static uint32_t _uSkipEndpointUntil = 0;   // back off the usage endpoint after a failure (it 429s a lot)

inline bool usageConfigured() { return _uTok[0] != 0; }
inline float usage5h() { return _u5h; }
inline float usage7d() { return _u7d; }
inline uint32_t usage5hReset() { return _u5hReset; }
inline uint32_t usage7dReset() { return _u7dReset; }
inline bool usageFresh() { return _uLastOkMs && millis() - _uLastOkMs < 3UL * _uPollSec * 1000UL; }
inline const char* usageError() { return _uErr; }

inline void usageLoad() {
  Preferences pr;
  pr.begin("buddy", true);
  pr.getString("u_tok", _uTok, sizeof(_uTok));
  _uPollSec = pr.getUShort("u_poll", 300);
  pr.end();
  if (_uPollSec < 60) _uPollSec = 60;
  if (_uPollSec > 3600) _uPollSec = 3600;
}

void usageSetToken(const char* t) {
  // Keep printable ASCII only — pasted tokens often carry spaces/newlines.
  size_t j = 0;
  for (size_t i = 0; t[i] && j < sizeof(_uTok) - 1; i++)
    if (t[i] > 0x20 && t[i] < 0x7F) _uTok[j++] = t[i];
  _uTok[j] = 0;
  Preferences pr;
  pr.begin("buddy", false);
  pr.putString("u_tok", _uTok);
  pr.end();
  _u5h = _u7d = -1; _u5hReset = _u7dReset = 0; _uLastOkMs = 0; _uErr[0] = 0; _uVia[0] = 0;
  _uPollNow = _uTok[0] != 0;
}

void usageSetPoll(uint16_t sec) {
  _uPollSec = constrain(sec, 60, 3600);
  Preferences pr;
  pr.begin("buddy", false);
  pr.putUShort("u_poll", _uPollSec);
  pr.end();
}

void usageRequestPoll() { _uPollNow = true; }

// ISO-8601 "2026-10-02T23:00:00.000Z" (or "+00:00") -> epoch seconds.
static uint32_t _uIso(const char* s) {
  if (!s || !*s) return 0;
  struct tm t = {};
  if (sscanf(s, "%d-%d-%dT%d:%d:%d", &t.tm_year, &t.tm_mon, &t.tm_mday, &t.tm_hour, &t.tm_min, &t.tm_sec) != 6) return 0;
  t.tm_year -= 1900; t.tm_mon -= 1;
  // timegm() equivalent: days from civil.
  int y = t.tm_year + 1900, m = t.tm_mon + 1, d = t.tm_mday;
  y -= m <= 2;
  int era = (y >= 0 ? y : y - 399) / 400;
  unsigned yoe = (unsigned)(y - era * 400);
  unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  long days = era * 146097L + (long)doe - 719468L;
  return (uint32_t)(days * 86400L + t.tm_hour * 3600 + t.tm_min * 60 + t.tm_sec);
}

static void _uClient(NetworkClientSecure& c) {
  c.setCACertBundle(_ota_ca_start, _ota_ca_end - _ota_ca_start);
  c.setTimeout(15);
}

static void _uSetErr(const char* e) {
  strncpy(_uErr, e, sizeof(_uErr) - 1); _uErr[sizeof(_uErr) - 1] = 0;
  Serial.printf("[usage] %s\n", e);
}

// 1) usage endpoint (free). Returns true on success.
static bool _uFetchUsageEndpoint() {
  NetworkClientSecure tls; _uClient(tls);
  HTTPClient http;
  http.begin(tls, "https://api.anthropic.com/api/oauth/usage");
  http.addHeader("Authorization", String("Bearer ") + _uTok);
  http.addHeader("anthropic-beta", "oauth-2025-04-20");
  http.setUserAgent("claudioscar-buddy/" FW_VERSION);
  int code = http.GET();
  if (code != 200) {
    http.end();
    Serial.printf("[usage] usage endpoint HTTP %d, using headers for 6 h\n", code);
    _uSkipEndpointUntil = millis() + 6UL * 3600UL * 1000UL;
    return false;
  }
  JsonDocument filter;
  // Newer shape: limits[] {id: session|weekly_all|..., percent, resets_at}.
  filter["limits"][0]["id"] = true;
  filter["limits"][0]["percent"] = true;
  filter["limits"][0]["resets_at"] = true;
  filter["five_hour"]["utilization"] = true;
  filter["five_hour"]["resets_at"] = true;
  filter["seven_day"]["utilization"] = true;
  filter["seven_day"]["resets_at"] = true;
  JsonDocument d;
  DeserializationError e = deserializeJson(d, http.getStream(), DeserializationOption::Filter(filter));
  http.end();
  if (e) return false;
  bool got = false;
  for (JsonObject l : d["limits"].as<JsonArray>()) {
    const char* id = l["id"] | "";
    if (!strcmp(id, "session"))    { _u5h = l["percent"] | -1.0f; _u5hReset = _uIso(l["resets_at"] | ""); got = true; }
    if (!strcmp(id, "weekly_all")) { _u7d = l["percent"] | -1.0f; _u7dReset = _uIso(l["resets_at"] | ""); got = true; }
  }
  if (got) { strcpy(_uVia, "usage"); return _u5h >= 0 || _u7d >= 0; }
  if (d["five_hour"].isNull()) return false;
  // This endpoint reports utilization in percent (0-100).
  _u5h = d["five_hour"]["utilization"] | -1.0f;
  _u7d = d["seven_day"]["utilization"] | -1.0f;
  _u5hReset = _uIso(d["five_hour"]["resets_at"] | "");
  _u7dReset = _uIso(d["seven_day"]["resets_at"] | "");
  strcpy(_uVia, "usage");
  return _u5h >= 0;
}

// 2) fallback: 1-token Messages request, read the unified rate-limit headers.
static bool _uFetchHeaders() {
  static const char* H[] = {
    "anthropic-ratelimit-unified-5h-utilization", "anthropic-ratelimit-unified-5h-reset",
    "anthropic-ratelimit-unified-7d-utilization", "anthropic-ratelimit-unified-7d-reset",
  };
  NetworkClientSecure tls; _uClient(tls);
  HTTPClient http;
  http.begin(tls, "https://api.anthropic.com/v1/messages");
  http.addHeader("Authorization", String("Bearer ") + _uTok);
  http.addHeader("anthropic-version", "2023-06-01");
  http.addHeader("anthropic-beta", "oauth-2025-04-20");
  http.addHeader("content-type", "application/json");
  http.setUserAgent("claudioscar-buddy/" FW_VERSION);
  http.collectHeaders(H, 4);
  int code = http.POST("{\"model\":\"claude-haiku-4-5-20251001\",\"max_tokens\":1,"
                       "\"messages\":[{\"role\":\"user\",\"content\":\".\"}]}");
  String u5 = http.header(H[0]), r5 = http.header(H[1]), u7 = http.header(H[2]), r7 = http.header(H[3]);
  http.end();
  if (code <= 0) {
    char tlsErr[64] = "";
    tls.lastError(tlsErr, sizeof(tlsErr));
    char b[48];
    snprintf(b, sizeof(b), "network error %d %s", code, tlsErr);
    Serial.printf("[usage] connect failed %d: %s (internal largest %u)\n", code, tlsErr,
                  (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
    _uSetErr(b);
    return false;
  }
  if (!u5.length() && !u7.length()) {
    char b[48];
    if (code == 401 || code == 403) snprintf(b, sizeof(b), "token rejected (HTTP %d)", code);
    else snprintf(b, sizeof(b), "no usage data (HTTP %d) - Pro/Max token?", code);
    _uSetErr(b);
    return false;
  }
  // Headers carry utilization as a 0-1 fraction and resets as epoch seconds.
  _u5h = u5.length() ? u5.toFloat() * 100.0f : -1;
  _u7d = u7.length() ? u7.toFloat() * 100.0f : -1;
  _u5hReset = (uint32_t)r5.toInt();
  _u7dReset = (uint32_t)r7.toInt();
  strcpy(_uVia, "headers");
  return true;
}

static void _uPoll() {
  _uLastTryMs = millis();
  if (netMode() != NET_STA) { _uSetErr("needs a home WiFi connection"); return; }
  bool tryEndpoint = !_uSkipEndpointUntil || (int32_t)(millis() - _uSkipEndpointUntil) >= 0;
  bool ok = (tryEndpoint && _uFetchUsageEndpoint()) || _uFetchHeaders();
  if (ok) {
    _uErr[0] = 0;
    _uLastOkMs = millis();
    Serial.printf("[usage] 5h %.0f%%  7d %.0f%%  via %s\n", _u5h, _u7d, _uVia);
  }
}

inline void usageLoop() {
  if (!usageConfigured()) return;
  if (netMode() != NET_STA) return;
  uint32_t now = millis();
  if (_uPollNow || !_uLastTryMs || now - _uLastTryMs > (uint32_t)_uPollSec * 1000UL) {
    _uPollNow = false;
    _uPoll();
  }
}

void usageStatusJson(JsonObject o) {
  o["configured"] = usageConfigured();
  // Never echo the token back; just enough to recognise which one is set.
  if (usageConfigured()) {
    size_t n = strlen(_uTok);
    o["hint"] = String("…") + (_uTok + (n > 4 ? n - 4 : 0));
  }
  o["h5"] = _u5h;
  o["d7"] = _u7d;
  o["h5reset"] = _u5hReset;
  o["d7reset"] = _u7dReset;
  o["ageSec"] = _uLastOkMs ? (millis() - _uLastOkMs) / 1000 : -1;
  o["via"] = _uVia;
  o["error"] = _uErr;
  o["poll"] = _uPollSec;
}
