// Wetter von Open-Meteo (ohne Schlüssel), Standort automatisch per IP (ip-api.com). Beides per HTTP,
// das spart den Speicher für TLS. Alle 15 Minuten neu, solange WLAN da ist.
#pragma once
#include <Arduino.h>

namespace weather {

struct Now {
  bool ok;          // Werte vorhanden (höchstens 3 Stunden alt)
  float temp;       // °C
  float tmin, tmax;
  int code;         // WMO-Wettercode
  int rain;         // Regenwahrscheinlichkeit heute in %
  float wind;       // km/h
};

void begin();
Now now();
const char* text(int code);  // „heiter“, „Regen“ …
const char* icon(int code, bool day);  // Material-Symbol (UTF-8)
const char* city();
bool located();              // Standort bekannt
float lat();
float lon();

}  // namespace weather
