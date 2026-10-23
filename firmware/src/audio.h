// Audio: ES8311 (Lautsprecher) und ES7210 (Mikrofone) im Vollduplex über einen I2S-Bus mit 44,1 kHz.
// Ein Mischer auf Kern 0 spielt Musik (44,1 kHz mono), Funk-Stimmen (16 kHz mono) und kleine Klänge.
// Dazu nimmt er auf Wunsch das Mikrofon auf (16 kHz mono, für den Funk).
#pragma once
#include <Arduino.h>

namespace audio {

static const int SR = 44100;  // wie die meisten Streams und Podcasts

enum Fx : uint8_t { FX_TAP, FX_OK, FX_ERROR, FX_PTT_ON, FX_PTT_OFF, FX_ROGER, FX_CHIME, FX_ALARM, FX_TICK, FX_KNOCK };

bool begin(int volume);
void setVolume(int pct);
void play(Fx fx);
void shutdown();            // vor dem Tiefschlaf
void hold(bool on);         // Ausgabe und Verstärker an lassen, auch in Pausen (z. B. Zähneputzen: kein Plopp zwischen Ansagen)

// Musik: der Strom-Task schreibt, der Mischer liest
size_t musicSpace();
size_t musicUsed();
void musicWrite(const int16_t* s, size_t n);
void musicFlush();
void musicPause(bool p);    // Mischer hält an (Puffer bleibt)
uint32_t musicPlayed();     // abgespielte Abtastwerte seit dem letzten Flush

// Funk-Stimme (16 kHz)
void voiceWrite(const int16_t* s, size_t n);
size_t voiceSpace();         // freier Platz im Sprachkanal (Samples, 16 kHz)
size_t voiceUsed();
void voiceFlush();

// Mikrofon (16 kHz)
void micStart();
void micStop();
size_t micRead(int16_t* out, size_t max);
float micLevel();           // 0..1
float outLevel();           // 0..1, was gerade aus dem Lautsprecher kommt

}  // namespace audio
