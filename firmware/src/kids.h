// Kindermodus: wird nur über die Weboberfläche ein- und ausgeschaltet.
// Auf der Uhr gibt es dann keine freie Auswahl mehr – nur freigegebene Playlists, Alben und Podcasts,
// die erlaubten Apps, eine Lautstärke-Obergrenze und das Pixel-Zifferblatt mit dem Tagesablauf.
#pragma once
#include <Arduino.h>
#include "music.h"

namespace kids {

// Apps, die im Kindermodus erlaubt sein können (Musik ist immer da)
enum App : uint8_t { A_RADIO = 1, A_TIMER = 2, A_STOPWATCH = 4, A_GAME = 8, A_ALARM = 16, A_WEATHER = 32 };

struct Cfg {
  bool on;
  uint16_t wake, bed;    // Minuten nach Mitternacht (Aufstehen, spätestens ins Bett)
  uint8_t maxVol;        // %
  uint8_t apps;          // App-Bits
  bool school;           // tagsüber „Schule“ statt „Kita“
  bool nap;              // Mittagsruhe
  char pin[9];           // Eltern-PIN für die Weboberfläche ("" = keine)
};

void begin();
const Cfg& cfg();
void set(const Cfg& c);  // speichert; ein Wechsel des Modus führt zurück zum Zifferblatt
bool on();
bool allowed(App a);     // außerhalb des Kindermodus immer true
int limitVolume(int pct);

// Freigaben
bool released(const char* path);
void release(const char* path, const char* title, const char* sub, bool on);
int list(music::Item* out, int max);

// Tagesablauf für das Zifferblatt
enum Act : uint8_t { SLEEP, WAKE, BREAKFAST, DAYCARE, LUNCH, NAP, PLAY, DINNER, BATH, STORY, ACTS };
struct Slot { Act act; uint16_t start; };  // Minuten nach Mitternacht
int schedule(Slot* out, bool weekend);     // Abschnitte des Tages in zeitlicher Reihenfolge
const char* actName(Act a, bool weekend);

}  // namespace kids
