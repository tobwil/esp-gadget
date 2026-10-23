// Kinder-Routinen mit Sternen: morgens und abends eine kurze Liste zum Abhaken (Kindermodus).
// Morgens: ab dem Aufstehen 2 Stunden. Abends: 2 Stunden vor der Bettzeit bis zur Bettzeit.
// Jeder Haken gibt einen Stern, eine ganz erledigte Routine zwei Extrasterne.
#pragma once
#include <Arduino.h>

namespace routines {

enum Which : uint8_t { NONE = 0, MORNING = 1, EVENING = 2 };

void begin();
bool enabled();                      // Aufgaben überhaupt anbieten (Weboberfläche)
void setEnabled(bool on);
String configJson();                 // {"morning":[…], "evening":[…]}
void setConfig(const String& json);
Which active();                      // laufende Routine (nur im Kindermodus), NONE = keine
Which next(int* startMin);           // die nächste, falls keine läuft
const char* name(Which w);           // „Morgens“ / „Abends“
int count(Which w);
String item(Which w, int i);
bool done(Which w, int i);
int check(Which w, int i, bool on);  // Haken setzen/entfernen; liefert neue Sterne (0, 1 oder 3 bei „fertig“)
bool complete(Which w);
int starsToday();
void addStars(int n);                // Bonus-Sterne (z. B. Rätsel beim Zähneputzen)
int checkMatching(const char* text); // Aufgabe der laufenden Routine abhaken, die text enthält; liefert neue Sterne
int starsWeek();
int starsTotal();
void resetStars();
String stateJson();                  // für die Weboberfläche

}  // namespace routines
