// Musik: ein Einstieg für Radio (webradio.h), ARD Audiothek (ard.h) und Podcasts (podcast.h); gespielt wird
// alles über den MP3-Spieler in podcast.h.
// Die Oberfläche blättert durch Listen (browse), startet Einträge (playList) und zeigt den Zustand.
#pragma once
#include <Arduino.h>
#include <string>
#include <vector>

namespace music {

enum Kind : uint8_t { K_TRACK = 0, K_LIST = 1 };
enum Source : uint8_t { SRC_NONE = 0, SRC_PODCAST = 2, SRC_RADIO = 3 };  // PODCAST auch für die Audiothek

struct Item {
  uint8_t kind;
  uint16_t dur;      // Sekunden (Titel/Folge)
  char id[232];      // Titel: Folgen-Adresse, „ard:a:…“ oder „radio:…“; Liste: Pfad für browse()
  char title[72];
  char sub[56];
};

struct State {
  bool active;       // es läuft etwas (oder ist pausiert)
  bool playing;      // nicht pausiert
  bool buffering;
  Source src;
  int index, count;
  char title[72];
  char artist[56];
  char id[64];
  uint16_t dur;
  float pos;         // Sekunden
  char msg[64];      // Fehler / Hinweis
};

static const int ART = 220;  // Cover-Kantenlänge in Pixeln

void begin();
void poll();                 // aus der Hauptschleife

// Listen: „home“, „favs“, „pc:subs“, „pcsearch:…“, „pc:feed:…“ (Podcasts), „rl:…“, „rlsearch:…“ (Radio, webradio.h),
//         „ard:…“, „ardsearch:…“ (ARD Audiothek, ard.h)
void browse(const char* path);
bool browseBusy();
uint32_t browseSeq();        // zählt hoch, sobald ein neues Ergebnis da ist
int browseCopy(char* title, size_t titleLen, Item* out, int max, char* err, size_t errLen);

// Für die Weboberfläche: Liste abrufen und auf das Ergebnis warten (blockiert bis zu 20 s,
// ohne Kindermodus-Filter und ohne die Liste auf der Uhr zu ändern)
int query(const char* path, char* title, size_t titleLen, Item* out, int max, char* err, size_t errLen);

void playList(const Item* items, int n, int start, bool force = false);  // force: auch nachts (Eltern per Weboberfläche)
// Was sich vom gerade Laufenden merken lässt: Sender, Audiothek-Sendung oder Podcast (false = nichts)
bool favTarget(String& path, String& title, String& sub);
void toggle();
void next();                 // Podcast: +30 s, Radio: nächster Sender
void prev();                 // Podcast: −15 s, Radio: voriger Sender
void stop();
State state();
const uint16_t* art(uint32_t* version);

// Radiowecker: Sender ("radio:<Adresse>") oder neueste Folge einer Audiothek-Sendung ("ard:set:<id>") spielen,
// leise beginnend und über 45 s lauter werdend
void wake(const char* src, const char* name);
// Sleep-Timer: nach so vielen Minuten ausblenden und anhalten (0 = aus)
void sleepSet(int minutes);
int sleepLeftS();            // 0 = aus
void volumeChanged(int pct);

}  // namespace music
