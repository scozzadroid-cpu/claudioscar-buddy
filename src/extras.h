// src/extras.h — claudioscar-buddy additions: sound themes and the
// "angry buddy" speech bubble. Header-only like data.h/stats.h; included
// once from main.cpp after the drawing helpers it uses.
#pragma once
#include <Preferences.h>
#include "hw/audio.h"
#include <SD_MMC.h>
#include "hw/rtc.h"

// ---------------------------------------------------------------- settings

enum SoundTheme : uint8_t { THEME_CLASSIC = 0, THEME_MEME = 1, THEME_SDPACK = 2 };
enum FlagBadge  : uint8_t { FLAG_NONE = 0, FLAG_PALESTINE = 1, FLAG_ITALY = 2, FLAG_COUNT };

struct ExtraSettings {
  uint8_t theme;      // SoundTheme
  uint8_t volume;     // 0..100
  uint8_t angryPct;   // chance (0..100) of an outburst on deny / shake
  uint8_t flag;       // FlagBadge shown in the top-right corner
  bool     nightOn;    // night mode schedule enabled
  uint16_t nightStart; // minutes after local midnight
  uint16_t nightEnd;
  uint8_t  nightLevel; // raw panel brightness at night (1..80)
  bool     nightMute;  // no sounds while night mode is active
  uint16_t nightWake;  // seconds a tap keeps the screen bright at night
  bool     bigText;    // large-text info/pet pages with only the key values
};

static ExtraSettings _xs = { THEME_MEME, 80, 50, FLAG_PALESTINE, false, 23 * 60, 7 * 60, 6, true, 60, false };

static const char* DEFAULT_PHRASES =
  "porco dio!|dio cane!|porca madonna!|dio porco!|madonna maiala!|"
  "dio boia!|cristo santo!|dio bestia!|porco il clero!|madonna santa!|"
  "dio cristo!|mortacci tua!|porca puttana!|dio canaglia!";

static char _phrases[640];
static char _soundPack[24] = "retro";   // THEME_SDPACK: /sd/soundpacks/<name>/

inline ExtraSettings& extraSettings() { return _xs; }
inline const char* angryPhrases() { return _phrases; }
inline const char* soundPack() { return _soundPack; }
inline void soundPackSet(const char* n) {
  size_t j = 0;
  for (size_t i = 0; n[i] && j < sizeof(_soundPack) - 1; i++)
    if (isalnum((unsigned char)n[i]) || n[i] == '-' || n[i] == '_') _soundPack[j++] = n[i];
  _soundPack[j] = 0;
}

inline void extrasLoad() {
  Preferences pr;
  pr.begin("buddy", true);
  _xs.theme    = pr.getUChar("x_theme", THEME_MEME);
  _xs.volume   = pr.getUChar("x_vol", 80);
  _xs.angryPct = pr.getUChar("x_angry", 50);
  _xs.flag     = pr.getUChar("x_flag", FLAG_PALESTINE);
  _xs.nightOn    = pr.getBool("n_on", false);
  _xs.nightStart = pr.getUShort("n_start", 23 * 60) % 1440;
  _xs.nightEnd   = pr.getUShort("n_end", 7 * 60) % 1440;
  _xs.nightLevel = constrain(pr.getUChar("n_lvl", 6), 1, 80);
  _xs.nightMute  = pr.getBool("n_mute", true);
  _xs.nightWake  = constrain(pr.getUShort("n_wake", 60), 10, 900);
  _xs.bigText    = pr.getBool("x_big", false);
  if (pr.isKey("x_spack")) pr.getString("x_spack", _soundPack, sizeof(_soundPack));
  if (pr.isKey("x_phr")) pr.getString("x_phr", _phrases, sizeof(_phrases));
  else strncpy(_phrases, DEFAULT_PHRASES, sizeof(_phrases) - 1);
  pr.end();
  if (_xs.theme > THEME_SDPACK) _xs.theme = THEME_MEME;
  if (_xs.volume > 100) _xs.volume = 100;
  if (_xs.angryPct > 100) _xs.angryPct = 100;
  if (_xs.flag >= FLAG_COUNT) _xs.flag = FLAG_NONE;
  hwAudioVolume(_xs.volume);
}

inline void extrasSave() {
  Preferences pr;
  pr.begin("buddy", false);
  pr.putUChar("x_theme", _xs.theme);
  pr.putUChar("x_vol", _xs.volume);
  pr.putUChar("x_angry", _xs.angryPct);
  pr.putUChar("x_flag", _xs.flag);
  pr.putBool("n_on", _xs.nightOn);
  pr.putUShort("n_start", _xs.nightStart);
  pr.putUShort("n_end", _xs.nightEnd);
  pr.putUChar("n_lvl", _xs.nightLevel);
  pr.putBool("n_mute", _xs.nightMute);
  pr.putUShort("n_wake", _xs.nightWake);
  pr.putBool("x_big", _xs.bigText);
  pr.putString("x_phr", _phrases);
  pr.putString("x_spack", _soundPack);
  pr.end();
  hwAudioVolume(_xs.volume);
}

// Phrases are '|'-separated; non-printable / non-ASCII bytes are dropped
// because the bubble font is ASCII-only.
inline void angryPhrasesSet(const char* s) {
  size_t j = 0;
  for (size_t i = 0; s[i] && j < sizeof(_phrases) - 1; i++) {
    char c = s[i];
    if (c == '\n' || c == '\r') c = '|';
    if ((c >= 0x20 && c < 0x7F) || c == '|') _phrases[j++] = c;
  }
  _phrases[j] = 0;
  if (!_phrases[0]) strncpy(_phrases, DEFAULT_PHRASES, sizeof(_phrases) - 1);
}

// ---------------------------------------------------------------- sounds

enum SfxEvent : uint8_t { SFX_BOOT, SFX_PROMPT, SFX_APPROVE, SFX_DENY, SFX_CELEBRATE, SFX_DIZZY, SFX_ANGRY, SFX_COUNT };
static const char* SFX_NAMES[SFX_COUNT] = { "boot", "prompt", "approve", "deny", "celebrate", "dizzy", "angry" };

// Boot: Francisco Tárrega's "Gran Vals" (1902, public domain) — the phrase
// the whole world knows as a phone ringtone.
static const HwNote MEL_BOOT[] = {
  {1319,125},{1175,125},{740,250},{831,250},
  {1109,125},{988,125},{587,250},{659,250},
  {988,125},{880,125},{554,250},{659,250},{880,600},
};
// Prompt: dramatic "dun dun duuun".
static const HwNote MEL_PROMPT[] = { {294,180},{0,60},{294,180},{0,60},{277,700} };
// Approve: bright two-note "ding!".
static const HwNote MEL_APPROVE[] = { {1047,80},{0,20},{1568,160} };
// Deny: sad trombone, last note with a wobble.
static const HwNote MEL_DENY[] = {
  {392,330},{370,330},{349,330},
  {330,90},{322,90},{330,90},{322,90},{330,90},{322,90},{330,90},{322,90},{330,180},
};
// Celebrate: a quick tarantella-flavoured flourish.
static const HwNote MEL_CELEBRATE[] = {
  {880,110},{1047,110},{988,110},{880,110},{831,110},{880,110},
  {659,110},{880,110},{1047,110},{1319,320},
};
// Dizzy: boing.
static const HwNote MEL_DIZZY[] = {
  {300,40},{400,40},{500,40},{600,40},{700,40},{600,40},{500,40},{400,40},{300,40},
};
// Angry: the TV censor bleep — goes over whatever the buddy just said.
static const HwNote MEL_ANGRY[] = { {1000,420} };

struct _Mel { const HwNote* n; uint8_t len; };
#define _MEL(a) { a, (uint8_t)(sizeof(a) / sizeof(a[0])) }
static const _Mel MELODIES[SFX_COUNT] = {
  _MEL(MEL_BOOT), _MEL(MEL_PROMPT), _MEL(MEL_APPROVE), _MEL(MEL_DENY),
  _MEL(MEL_CELEBRATE), _MEL(MEL_DIZZY), _MEL(MEL_ANGRY),
};

// Play the sound for an event. A per-event upload wins over the theme:
// /sd/sounds/<event>.wav, then LittleFS /sounds/<event>.wav. The SD-pack
// theme reads /sd/soundpacks/<pack>/<event>.wav and falls back to the
// meme melodies for events the pack doesn't cover.
bool nightActive();

inline void sfxPlay(SfxEvent ev) {
  if (!settings().sound || ev >= SFX_COUNT) return;
  if (_xs.nightMute && nightActive()) return;
  char path[48];
  snprintf(path, sizeof(path), "/sd/sounds/%s.wav", SFX_NAMES[ev]);
  if (hwPlayWav(path)) return;
  snprintf(path, sizeof(path), "/sounds/%s.wav", SFX_NAMES[ev]);
  if (hwPlayWav(path)) return;
  if (_xs.theme == THEME_SDPACK) {
    snprintf(path, sizeof(path), "/sd/soundpacks/%s/%s.wav", _soundPack, SFX_NAMES[ev]);
    if (hwPlayWav(path)) return;
  }

  if (_xs.theme != THEME_CLASSIC) {
    hwPlayNotes(MELODIES[ev].n, MELODIES[ev].len);
    return;
  }
  // Classic theme: the original firmware's chirps.
  switch (ev) {
    case SFX_PROMPT:  hwBeep(1200, 80); break;
    case SFX_APPROVE: hwBeep(2400, 60); break;
    case SFX_DENY:    hwBeep(600, 60);  break;
    default: break;
  }
}

// ---------------------------------------------------------------- angry bubble

static char     _angryText[48] = "";
static uint32_t _angryUntil = 0;

// Pick a random phrase and show it for 3 s with the censor bleep.
inline void angryOutburst() {
  int n = 1;
  for (const char* p = _phrases; *p; p++) if (*p == '|') n++;
  int pick = esp_random() % n;
  const char* p = _phrases;
  for (int i = 0; i < pick; i++) p = strchr(p, '|') + 1;
  const char* e = strchr(p, '|');
  size_t len = e ? (size_t)(e - p) : strlen(p);
  if (len >= sizeof(_angryText)) len = sizeof(_angryText) - 1;
  memcpy(_angryText, p, len);
  _angryText[len] = 0;
  if (!_angryText[0]) return;
  _angryUntil = millis() + 3000;
  hwAudioStop();
  sfxPlay(SFX_ANGRY);
}

// Roll the dice: outburst with the configured probability.
inline void angryMaybe() {
  if (_xs.angryPct && (esp_random() % 100) < _xs.angryPct) angryOutburst();
}

inline bool angryActive() { return (int32_t)(millis() - _angryUntil) < 0; }

// Speech bubble across the top of the canvas.
inline void drawAngry() {
  if (!angryActive()) return;
  size_t len = strlen(_angryText);
  int sz = len <= 13 ? 2 : 1;
  char l1[32] = "", l2[32] = "";
  if (sz == 1 && len > 28) {
    // Wrap at the last space before col 28.
    int cut = 28;
    while (cut > 0 && _angryText[cut] != ' ') cut--;
    if (cut == 0) cut = 28;
    strncpy(l1, _angryText, cut); l1[cut] = 0;
    strncpy(l2, _angryText + cut + (_angryText[cut] == ' '), sizeof(l2) - 1);
  } else {
    strncpy(l1, _angryText, sizeof(l1) - 1);
  }
  const int bx = 6, by = 14, bw = W - 12;
  const int bh = l2[0] ? 34 : (sz == 2 ? 30 : 22);
  spr.fillRoundRect(bx, by, bw, bh, 8, 0x0000);
  spr.drawRoundRect(bx, by, bw, bh, 8, HOT);
  spr.drawRoundRect(bx + 1, by + 1, bw - 2, bh - 2, 7, HOT);
  // Tail pointing down at the buddy.
  spr.fillTriangle(W / 2 - 6, by + bh - 1, W / 2 + 6, by + bh - 1, W / 2, by + bh + 8, HOT);
  if (l2[0]) {
    drawCenteredText(l1, W / 2, by + 11, 1, HOT, 0x0000);
    drawCenteredText(l2, W / 2, by + 23, 1, HOT, 0x0000);
  } else {
    drawCenteredText(l1, W / 2, by + bh / 2, sz, HOT, 0x0000);
  }
  spr.setTextSize(1);
}

// ---------------------------------------------------------------- flag badge

static const char* FLAG_NAMES[FLAG_COUNT] = { "none", "palestine", "italy" };

// 21x14 badge with a 1 px dim outline so the white stripe reads on black.
inline void drawFlagBadge(int x, int y) {
  const int w = 21, h = 14;
  switch (_xs.flag) {
    case FLAG_PALESTINE: {
      const uint16_t BLK = 0x0000, WHT = 0xFFFF, GRN = 0x03C7, RED_ = 0xC884;   // #007A3D, #CE1126
      spr.fillRect(x, y,             w, h / 3 + 1, BLK);
      spr.fillRect(x, y + h / 3 + 1, w, h / 3,     WHT);
      spr.fillRect(x, y + 2 * h / 3 + 1, w, h - 2 * h / 3 - 1, GRN);
      spr.fillTriangle(x, y, x, y + h - 1, x + 9, y + (h - 1) / 2, RED_);
      break;
    }
    case FLAG_ITALY:
      spr.fillRect(x,             y, w / 3,         h, 0x0488);   // #009246
      spr.fillRect(x + w / 3,     y, w / 3,         h, 0xFFFF);
      spr.fillRect(x + 2 * w / 3, y, w - 2 * w / 3, h, 0xC946);   // #CE2B37
      break;
    default:
      return;
  }
  spr.drawRect(x - 1, y - 1, w + 2, h + 2, 0x4208);
}

// Secret toggle: 7 taps, each within 1 s of the previous, flip the badge
// between off and the last flag chosen (Palestine by default). Persisted.
inline void flagTap() {
  static uint8_t  count = 0;
  static uint32_t last = 0;
  static uint8_t  remembered = FLAG_PALESTINE;
  uint32_t now = millis();
  count = (now - last < 1000) ? count + 1 : 1;
  last = now;
  Serial.printf("flag tap %u/7\n", count);
  if (count < 7) return;
  count = 0;
  if (_xs.flag != FLAG_NONE) { remembered = _xs.flag; _xs.flag = FLAG_NONE; }
  else _xs.flag = remembered;
  extrasSave();
  characterInvalidate();   // full repaint so a removed badge doesn't linger
  hwBeep(_xs.flag ? 1568 : 523, 120);
}

void sfxTest(uint8_t ev) {
  Serial.printf("[sfx] test %u sound=%d theme=%u vol=%u sd=%d\n", ev, settings().sound, _xs.theme, _xs.volume, hwSdMounted());
  if (ev == SFX_ANGRY) { angryOutburst(); return; }
  hwAudioStop();
  sfxPlay((SfxEvent)ev);
}

// ---------------------------------------------------------------- night mode

// Between nightStart and nightEnd (local time from the RTC) the panel drops
// to nightLevel. A tap or key press brightens it for nightWake seconds
// (each further touch extends that), then it dims again until morning. The
// schedule needs a valid clock (synced from Claude Desktop, the web page,
// or NTP once the timezone is known).
static uint32_t _nightWakeUntil = 0;
static bool     _nightAwake = false;

static bool _nightWindow() {
  if (!_xs.nightOn || _xs.nightStart == _xs.nightEnd) return false;
  if (_clkTm.Y < 2024) return false;           // clock never set
  uint16_t m = _clkTm.H * 60 + _clkTm.M;
  return _xs.nightStart < _xs.nightEnd ? (m >= _xs.nightStart && m < _xs.nightEnd)
                                       : (m >= _xs.nightStart || m < _xs.nightEnd);
}

bool nightActive() { return _nightWindow() && !_nightAwake; }
uint8_t nightLevel() { return _xs.nightLevel; }

// Call once per frame. Returns true when the night state flipped so the
// caller re-applies brightness.
inline bool nightTick() {
  static bool was = false;
  if (_nightAwake && (int32_t)(millis() - _nightWakeUntil) >= 0) _nightAwake = false;  // back to sleep
  bool now = nightActive();
  bool changed = now != was;
  was = now;
  return changed;
}

// Tap / key inside the night window: bright for nightWake seconds. Returns
// true if the screen was dimmed (caller re-applies brightness).
inline bool nightPoke() {
  if (!_nightWindow()) return false;
  bool wasDim = !_nightAwake;
  _nightAwake = true;
  _nightWakeUntil = millis() + (uint32_t)_xs.nightWake * 1000UL;
  return wasDim;
}

inline bool bigTextOn() { return _xs.bigText; }

// ---------------------------------------------------------------- fake hwclock

// Like fake-hwclock on a Raspberry Pi: the PCF85063 keeps time across
// reboots but not across a power loss, so every 10 minutes the current local
// time is saved (microSD /buddy/clock.txt, or NVS without a card) and
// restored at boot when the RTC came up empty. A restored time is behind by
// however long the buddy was off — good enough for night mode until NTP,
// Claude Desktop or the web page provide the real time.

static uint32_t _civilToEpoch(uint16_t Y, uint8_t Mo, uint8_t D, uint8_t h, uint8_t mi, uint8_t se) {
  int y = Y - (Mo <= 2);
  int era = (y >= 0 ? y : y - 399) / 400;
  unsigned yoe = (unsigned)(y - era * 400);
  unsigned doy = (153 * (Mo + (Mo > 2 ? -3 : 9)) + 2) / 5 + D - 1;
  unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return (uint32_t)((era * 146097L + (long)doe - 719468L) * 86400L + h * 3600L + mi * 60L + se);
}

static void _clockSave(uint32_t localEpoch) {
  char b[16]; snprintf(b, sizeof(b), "%lu", (unsigned long)localEpoch);
  if (hwSdMounted()) {
    SD_MMC.mkdir("/buddy");
    File f = SD_MMC.open("/buddy/clock.txt", FILE_WRITE);
    if (f) { f.print(b); f.close(); return; }
  }
  Preferences pr; pr.begin("buddy_hw", false); pr.putULong("clk", localEpoch); pr.end();
}

static uint32_t _clockLoad() {
  uint32_t e = 0;
  if (hwSdMounted()) {
    File f = SD_MMC.open("/buddy/clock.txt");
    if (f) { e = (uint32_t)f.readString().toInt(); f.close(); }
  }
  if (!e) { Preferences pr; pr.begin("buddy_hw", true); e = pr.getULong("clk", 0); pr.end(); }
  return e;
}

// At boot. Returns 2 if the RTC still had the time, 1 if it was restored
// (approximately) from the saved copy, 0 if there's no time at all.
inline uint8_t clockRestoreAtBoot() {
  HwTime t;
  if (hwRtcRead(&t) && t.Y >= 2025 && t.Y < 2100) return 2;
  uint32_t e = _clockLoad();
  if (e < 1735689600UL) return 0;                 // nothing saved (before 2025)
  time_t tt = (time_t)e;
  struct tm lt; gmtime_r(&tt, &lt);
  HwTime w;
  w.H = lt.tm_hour; w.M = lt.tm_min; w.S = lt.tm_sec;
  w.Y = lt.tm_year + 1900; w.Mo = lt.tm_mon + 1; w.D = lt.tm_mday; w.dow = lt.tm_wday;
  hwRtcWrite(w);
  Serial.printf("[clock] RTC was empty, restored %04u-%02u-%02u %02u:%02u from %s copy\n",
                w.Y, w.Mo, w.D, w.H, w.M, hwSdMounted() ? "SD" : "flash");
  return 1;
}

// Call from loop(): saves the clock every 10 minutes while it looks valid.
inline void clockSaveLoop() {
  static uint32_t last = 0;
  if (last && millis() - last < 10UL * 60UL * 1000UL) return;
  HwTime t;
  if (!hwRtcRead(&t) || t.Y < 2025 || t.Y >= 2100) return;
  last = millis();
  _clockSave(_civilToEpoch(t.Y, t.Mo, t.D, t.H, t.M, t.S));
}
