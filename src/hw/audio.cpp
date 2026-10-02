#include "hw/audio.h"
#include "hw/pins.h"
#include <Arduino.h>
#include <Wire.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <driver/i2c.h>
#include <math.h>
#include <LittleFS.h>
#include <SD_MMC.h>

// Use legacy i2s.h API (driver/deprecated). The new i2s_std.h driver
// rejects DMA setup when PSRAM is enabled because internal allocations
// can land in PSRAM ("user context not in internal RAM" GDMA error).
// The legacy driver allocates DMA structs directly via heap_caps with
// MALLOC_CAP_DMA which forces internal RAM.
#include <driver/i2s.h>

extern "C" {
#include "es8311.h"
}

static constexpr int AUDIO_SR  = 16000;
static constexpr int AUDIO_VOL = 60;
static constexpr int AUDIO_AMP = 6000;

static QueueHandle_t   s_audQ = nullptr;
static es8311_handle_t s_codec = nullptr;
// Bumped by hwAudioStop(): requests and playback from an older generation
// are dropped, anything queued after the stop plays normally.
static volatile uint32_t s_gen = 0;
static bool            s_sdOk = false;

enum : uint8_t { AQ_TONE, AQ_WAV };
struct AudioReq {
  uint32_t gen;
  uint8_t  kind;
  uint16_t freq;
  uint16_t dur;
  char     path[56];
};
static bool i2sInit() {
  i2s_config_t cfg = {};
  cfg.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
  cfg.sample_rate = AUDIO_SR;
  cfg.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
  cfg.channel_format = I2S_CHANNEL_FMT_ONLY_LEFT;
  cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
  cfg.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
  cfg.dma_buf_count = 4;
  cfg.dma_buf_len = 256;
  cfg.use_apll = false;
  cfg.tx_desc_auto_clear = true;
  cfg.fixed_mclk = AUDIO_SR * 256;
  cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;

  if (i2s_driver_install(I2S_NUM_0, &cfg, 0, nullptr) != ESP_OK) return false;

  i2s_pin_config_t pins = {};
  pins.mck_io_num   = PIN_I2S_MCLK;
  pins.bck_io_num   = PIN_I2S_BCLK;
  pins.ws_io_num    = PIN_I2S_WS;
  pins.data_out_num = PIN_I2S_DO;
  pins.data_in_num  = I2S_PIN_NO_CHANGE;
  if (i2s_set_pin(I2S_NUM_0, &pins) != ESP_OK) return false;
  return true;
}

static bool es8311CodecInit() {
  es8311_handle_t h = es8311_create((i2c_port_t)0, ES8311_ADDRRES_0);
  s_codec = h;
  if (!h) { Serial.println("hwAudio: es8311_create failed"); return false; }
  const es8311_clock_config_t clk = {
    .mclk_inverted      = false,
    .sclk_inverted      = false,
    .mclk_from_mclk_pin = true,
    .mclk_frequency     = AUDIO_SR * 256,
    .sample_frequency   = AUDIO_SR,
  };
  if (es8311_init(h, &clk, ES8311_RESOLUTION_16, ES8311_RESOLUTION_16) != ESP_OK) return false;
  if (es8311_sample_frequency_config(h, clk.mclk_frequency, clk.sample_frequency) != ESP_OK) return false;
  es8311_microphone_config(h, false);
  es8311_voice_volume_set(h, AUDIO_VOL, nullptr);
  return true;
}

static void i2sOut(const int16_t* buf, int n) {
  size_t written;
  i2s_write(I2S_NUM_0, buf, n * sizeof(int16_t), &written, portMAX_DELAY);
}

// Sine tone with 4 ms linear attack/release; freq 0 = silence.
static void playTone(uint16_t freq, uint16_t dur, uint32_t gen) {
  int16_t buf[256];
  const int total = AUDIO_SR * dur / 1000;
  const int ramp  = min(AUDIO_SR * 4 / 1000, total / 2);
  float phase = 0.0f;
  const float dphase = 2.0f * (float)M_PI * freq / (float)AUDIO_SR;
  for (int n = 0; n < total && gen == s_gen; n += 256) {
    int chunk = min(256, total - n);
    for (int i = 0; i < chunk; i++) {
      int k = n + i;
      float env = 1.0f;
      if (k < ramp) env = (float)k / ramp;
      else if (k > total - ramp) env = (float)(total - k) / ramp;
      buf[i] = freq ? (int16_t)(AUDIO_AMP * env * sinf(phase)) : 0;
      phase += dphase;
      if (phase > 2 * M_PI) phase -= 2 * M_PI;
    }
    i2sOut(buf, chunk);
  }
}

static uint32_t rd32(File& f) { uint8_t b[4]; f.read(b, 4); return b[0] | b[1] << 8 | b[2] << 16 | (uint32_t)b[3] << 24; }
static uint16_t rd16(File& f) { uint8_t b[2]; f.read(b, 2); return b[0] | b[1] << 8; }

static File openAudioFile(const char* path) {
  if (strncmp(path, "/sd/", 4) == 0) {
    if (!s_sdOk) return File();
    return SD_MMC.open(path + 3);
  }
  return LittleFS.open(path);
}

// Minimal RIFF/WAVE reader: PCM only, nearest-neighbour resample to AUDIO_SR.
static void playWav(const char* path, uint32_t gen) {
  File f = openAudioFile(path);
  if (!f) return;
  char id[4];
  f.read((uint8_t*)id, 4); rd32(f); char wave[4]; f.read((uint8_t*)wave, 4);
  if (memcmp(id, "RIFF", 4) || memcmp(wave, "WAVE", 4)) { f.close(); return; }
  uint16_t fmt = 0, ch = 1, bits = 16; uint32_t rate = AUDIO_SR, dataLen = 0;
  while (f.available() >= 8) {
    f.read((uint8_t*)id, 4);
    uint32_t len = rd32(f);
    if (!memcmp(id, "fmt ", 4)) {
      fmt = rd16(f); ch = rd16(f); rate = rd32(f); rd32(f); rd16(f); bits = rd16(f);
      if (len > 16) f.seek(f.position() + (len - 16));
    } else if (!memcmp(id, "data", 4)) {
      dataLen = len; break;
    } else {
      f.seek(f.position() + len + (len & 1));
    }
  }
  if (fmt != 1 || !dataLen || (bits != 8 && bits != 16) || ch < 1 || ch > 2 || !rate) { f.close(); return; }

  const int frameBytes = ch * bits / 8;
  static uint8_t in[1024];
  int16_t out[256];
  int outN = 0;
  uint32_t pos = 0;                         // 16.16 fixed-point source frame index
  const uint32_t step = (uint32_t)(((uint64_t)rate << 16) / AUDIO_SR);
  uint32_t baseFrame = 0;                   // frame index of in[0]
  int inFrames = 0;
  uint32_t remaining = dataLen;
  while (gen == s_gen) {
    uint32_t want = pos >> 16;
    while (want >= baseFrame + inFrames) {
      baseFrame += inFrames;
      int rd = f.read(in, min<uint32_t>(sizeof(in) / frameBytes * frameBytes, remaining));
      if (rd <= 0) { inFrames = 0; break; }
      remaining -= rd;
      inFrames = rd / frameBytes;
    }
    if (want >= baseFrame + inFrames) break;  // EOF
    const uint8_t* fr = in + (want - baseFrame) * frameBytes;
    int32_t v;
    if (bits == 16) {
      v = (int16_t)(fr[0] | fr[1] << 8);
      if (ch == 2) v = (v + (int16_t)(fr[2] | fr[3] << 8)) / 2;
    } else {
      v = ((int)fr[0] - 128) << 8;
      if (ch == 2) v = (v + (((int)fr[1] - 128) << 8)) / 2;
    }
    out[outN++] = (int16_t)v;
    if (outN == 256) { i2sOut(out, outN); outN = 0; }
    pos += step;
  }
  if (outN) i2sOut(out, outN);
  f.close();
}

static void audioTask(void*) {
  AudioReq r;
  while (xQueueReceive(s_audQ, &r, portMAX_DELAY) == pdTRUE) {
    if (r.gen != s_gen) continue;
    if (r.kind == AQ_TONE) playTone(r.freq, r.dur, r.gen);
    else playWav(r.path, r.gen);
    if (uxQueueMessagesWaiting(s_audQ) == 0) {
      // Flush the DMA ring with silence so the tail doesn't loop.
      int16_t z[256] = {0};
      for (int i = 0; i < 4; i++) i2sOut(z, 256);
    }
  }
}

static void sdInit() {
#if BOARD_HAS_SD
  SD_MMC.setPins(PIN_SD_CLK, PIN_SD_CMD, PIN_SD_D0);
  s_sdOk = SD_MMC.begin("/sdcard", true /* 1-bit */, false, 20000, 5);
  if (s_sdOk) Serial.printf("hwSd: card %lluMB\n", SD_MMC.cardSize() / (1024 * 1024));
  else Serial.println("hwSd: no card");
#endif
}

bool hwAudioInit() {
#if BOARD_HAS_PA_CTRL
  pinMode(PIN_PA_CTRL, OUTPUT);
  digitalWrite(PIN_PA_CTRL, HIGH);
#endif

  if (!i2sInit())          { Serial.println("hwAudio: I2S init failed");   return false; }
  if (!es8311CodecInit())  { Serial.println("hwAudio: ES8311 init failed"); return false; }

  s_audQ = xQueueCreate(48, sizeof(AudioReq));
  if (!s_audQ) return false;
  xTaskCreatePinnedToCore(audioTask, "audio", 6144, nullptr, 5, nullptr, tskNO_AFFINITY);
  sdInit();
  return true;
}

static void enqueueTone(uint16_t f, uint16_t d) {
  if (!s_audQ) return;
  AudioReq r{}; r.gen = s_gen; r.kind = AQ_TONE; r.freq = f; r.dur = d;
  xQueueSend(s_audQ, &r, 0);
}

void hwBeep(uint16_t freqHz, uint16_t durMs) { enqueueTone(freqHz, durMs); }

void hwPlayNotes(const HwNote* notes, uint8_t n) {
  for (uint8_t i = 0; i < n; i++) enqueueTone(notes[i].freq, notes[i].ms);
}

bool hwPlayWav(const char* path) {
  if (!s_audQ) return false;
  File f = openAudioFile(path);
  if (!f) return false;
  f.close();
  AudioReq r{}; r.gen = s_gen; r.kind = AQ_WAV;
  strncpy(r.path, path, sizeof(r.path) - 1);
  return xQueueSend(s_audQ, &r, 0) == pdTRUE;
}

void hwAudioStop() {
  if (!s_audQ) return;
  s_gen++;
  xQueueReset(s_audQ);
}

void hwAudioVolume(uint8_t vol) {
  if (vol > 100) vol = 100;
  if (s_codec) es8311_voice_volume_set(s_codec, vol, nullptr);
}

bool hwSdMounted() { return s_sdOk; }
