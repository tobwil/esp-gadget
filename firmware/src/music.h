// Musik: ein Einstieg für zwei Quellen – Spotify (cspot, siehe spotify.h) und Podcasts (podcast.h).
// Die Oberfläche blättert durch Listen (browse), startet Einträge (playList) und zeigt den Zustand.
#pragma once
#include <Arduino.h>
#include <string>
#include <vector>

namespace music {

enum Kind : uint8_t { K_TRACK = 0, K_LIST = 1 };
enum Source : uint8_t { SRC_NONE = 0, SRC_SPOTIFY = 1, SRC_PODCAST = 2, SRC_RADIO = 3 };  // PODCAST auch für die Audiothek

struct Item {
  uint8_t kind;
  uint16_t dur;      // Sekunden (Titel/Folge)
  char id[232];      // Titel: „spotify:track:…“ bzw. Folgen-Adresse; Liste: Pfad für browse()
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

// Listen: „home“, „sp:home“, „search:…“ (Spotify), „sp:album:…“, „sp:playlist:…“, „sp:artist:…“,
//         „pc:subs“, „pcsearch:…“, „pc:feed:…“ (Podcasts), „rl:…“, „rlsearch:…“ (Radio, webradio.h),
//         „ard:…“, „ardsearch:…“ (ARD Audiothek, ard.h)
void browse(const char* path);
bool browseBusy();
uint32_t browseSeq();        // zählt hoch, sobald ein neues Ergebnis da ist
int browseCopy(char* title, size_t titleLen, Item* out, int max, char* err, size_t errLen);

// Für die Weboberfläche: Liste abrufen und auf das Ergebnis warten (blockiert bis zu 20 s,
// ohne Kindermodus-Filter und ohne die Liste auf der Uhr zu ändern)
int query(const char* path, char* title, size_t titleLen, Item* out, int max, char* err, size_t errLen);

void playList(const Item* items, int n, int start);
void toggle();
void next();                 // Podcast: +30 s, Radio: nächster Sender
void prev();                 // Podcast: −15 s, Radio: voriger Sender
void stop();
State state();
const uint16_t* art(uint32_t* version);
int takeRemoteVolume();      // −1 = nichts; sonst Lautstärke aus der Spotify-App
void volumeChanged(int pct);

}  // namespace music
