// Timer, Stoppuhr und Wecker (Logik ohne Oberfläche). Läuft im Hintergrund weiter.
#pragma once
#include <Arduino.h>

namespace cw {

// Timer
void timerStart(uint32_t seconds);
void timerPause();
void timerResume();
void timerReset();
bool timerRunning();
bool timerPaused();
uint32_t timerLeftMs();
uint32_t timerTotalS();

// Stoppuhr
void swStartStop();
void swLap();
void swReset();
bool swRunning();
uint32_t swMs();
int swLaps(uint32_t* out, int max);  // Rundenzeiten (ms), neueste zuerst

// Wecker / Klingeln
enum Ring : uint8_t { RING_NONE, RING_TIMER, RING_ALARM };
Ring ringing();
void ringStop();
void ringQuiet(bool quiet);    // Radiowecker spielt: nicht piepen, bis zu 60 Minuten statt 90 Sekunden
void snooze();                 // Wecker: 5 Minuten später nochmal
uint32_t alarmInS();           // Sekunden bis zum nächsten Wecken (0 = keiner)

void poll();                   // aus der Hauptschleife
bool busy();                   // Timer/Stoppuhr laufen oder es klingelt (kein Tiefschlaf)

}  // namespace cw
