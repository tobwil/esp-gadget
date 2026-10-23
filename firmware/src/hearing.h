// Hören beim Zähneputzen: aus 40 ms Mikrofon (16 kHz) erkennen, ob etwas zu hören ist (Bürste, Summen …) und ob es
// ein Ton ist (Summen) – und wie hoch. Reines Rechnen ohne Hardware, damit es auch am Rechner getestet wird
// (tests/host).
//   sound:  lauter als das Grundrauschen; das Grundrauschen fällt schnell und steigt nur sehr langsam mit, damit
//           zwei Minuten Dauerputzen nicht „verschwinden“
//   voiced: nach einem Tiefpass (≈ 900 Hz) deutlich periodisch (Autokorrelation) – Summen ist ein Ton, die Bürste
//           Rauschen; Summen zählt auch unter dreifach lauter Bürste
//   pitch:  70 … 530 Hz (Kinder summen um 250 Hz, Erwachsene um 110 Hz)
#pragma once
#include <stdint.h>

namespace hearing {

constexpr int N = 640;  // Werte je Prüfung (40 ms)

struct Result {
  bool sound = false, voiced = false;
  float pitch = 0;  // Hz, wenn voiced
  float rms = 0;
  float periodic = 0;  // beste Autokorrelation 0 … 1 (ab 0,45 ein Ton)
};

// floor: Grundrauschen, wird fortgeschrieben (Anfangswert 0.004)
Result analyze(const int16_t* w, float& floor);

}  // namespace hearing
