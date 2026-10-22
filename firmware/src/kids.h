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
  bool angry;            // Wutmodus: auf der Uhr gibt es nur noch das Wut-Glas
  bool nightLock;        // Schlafenszeit: ab der Bettzeit bis zum Aufstehen gesperrt
  uint8_t storyMin;      // so lange darf nach der Bettzeit noch Musik/Hörspiel laufen
};

void begin();
const Cfg& cfg();
void set(const Cfg& c);  // speichert; ein Wechsel des Modus führt zurück zum Zifferblatt
bool on();
bool allowed(App a);     // außerhalb des Kindermodus immer true
bool angry();            // Wutmodus aktiv (nur zusammen mit dem Kindermodus)

// Schlafenszeit (Kindermodus): von der Bettzeit bis zum Aufstehen. Die ersten storyMin Minuten darf
// noch etwas zum Einschlafen laufen, danach ist Musik gesperrt.
bool night();
bool musicLocked();      // nachts nach der Einschlafzeit
int storyLeftS();        // Sekunden Einschlafzeit, die noch übrig sind (0 = keine)
void poll();             // aus der Hauptschleife (Musik zur Einschlafzeit ausblenden, Helligkeit)

// Wutmodus: wie oft heute die Wut rausgeschüttelt wurde (nur im Speicher, für die Eltern)
void angryDone();
int angryToday();
uint32_t angryLast();    // Unix-Zeit, 0 = noch nie
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
