// Akku: Verlauf, Restlaufzeit, Warnungen und Sparmodus.
// Sparmodus: Display höchstens 40 % hell und nach 10 s aus, sparsamere Zifferblätter, kein Funk im Hintergrund,
// ohne laufende Musik WLAN im Stromsparbetrieb und Prozessor auf 80 MHz, Tiefschlaf schon nach 3 Minuten.
#pragma once
#include <Arduino.h>

namespace pwr {

enum Mode : uint8_t { M_OFF = 0, M_AUTO = 1, M_ON = 2 };  // Auto: ab 20 % bis zum Laden

void begin();
void poll();                 // aus der Hauptschleife
Mode mode();
void setMode(Mode m);
bool saving();               // Sparmodus gerade aktiv
uint16_t screenOffS();       // Display-aus-Zeit unter Berücksichtigung des Sparmodus (0 = nie)
uint16_t deepSleepMin();
int remainingMin();          // geschätzte Restlaufzeit, −1 = unbekannt
float drainPerHour();        // Verbrauch in %/h (aus dem Verlauf), 0 = unbekannt
String historyJson();        // [[Minuten vor jetzt, %, mV, lädt], …]

}  // namespace pwr
