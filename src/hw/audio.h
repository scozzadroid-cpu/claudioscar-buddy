#pragma once
#include <stdint.h>

bool hwAudioInit();
void hwBeep(uint16_t freqHz, uint16_t durMs);

// A note of a built-in melody. freq 0 = rest.
struct HwNote { uint16_t freq; uint16_t ms; };

// Queue a melody (notes are copied). Each note gets a short attack/release
// envelope so consecutive notes don't click.
void hwPlayNotes(const HwNote* notes, uint8_t n);

// Queue a PCM WAV file (8/16-bit, mono/stereo, any rate — resampled to the
// 16 kHz output). Paths starting with "/sd/" are read from the microSD card,
// everything else from LittleFS. Returns false if the file doesn't exist.
bool hwPlayWav(const char* path);

// Drop everything queued and stop the current sound.
void hwAudioStop();

// Output volume 0..100 (ES8311 DAC volume).
void hwAudioVolume(uint8_t vol);

// microSD (SDMMC 1-bit) — mounted at boot when the board has a slot and a
// card is inserted. Optional: everything works without a card.
bool hwSdMounted();
