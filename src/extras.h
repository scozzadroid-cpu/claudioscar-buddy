// src/extras.h — claudioscar-buddy additions: sound themes and the
// "angry buddy" speech bubble. Header-only like data.h/stats.h; included
// once from main.cpp after the drawing helpers it uses.
#pragma once
#include <Preferences.h>
#include "hw/audio.h"

// ---------------------------------------------------------------- settings

enum SoundTheme : uint8_t { THEME_CLASSIC = 0, THEME_MEME = 1, THEME_SDPACK = 2 };
enum FlagBadge  : uint8_t { FLAG_NONE = 0, FLAG_PALESTINE = 1, FLAG_ITALY = 2, FLAG_COUNT };

struct ExtraSettings {
  uint8_t theme;      // SoundTheme
  uint8_t volume;     // 0..100
  uint8_t angryPct;   // chance (0..100) of an outburst on deny / shake
  uint8_t flag;       // FlagBadge shown in the top-right corner
};

static ExtraSettings _xs = { THEME_MEME, 80, 50, FLAG_PALESTINE };

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
inline void sfxPlay(SfxEvent ev) {
  if (!settings().sound || ev >= SFX_COUNT) return;
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
