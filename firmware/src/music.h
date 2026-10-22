// Musik über die YT-Music-Brücke (bridge/server.py):
//   - Listen durchblättern (Bibliothek, Mixe, Suche …); die Brücke bestimmt die Menüstruktur
//   - Titel als 32-kHz-PCM streamen, Cover als RGB565 holen
//   - Fernbedienung: die Brücke kann Befehle schicken (Weboberfläche auf dem Handy)
#pragma once
#include <Arduino.h>

namespace music {

enum Kind : uint8_t { K_TRACK = 0, K_LIST = 1 };

struct Item {
  uint8_t kind;
  uint16_t dur;      // Sekunden (Titel)
  char id[100];      // Titel: Video-ID, Liste: Pfad für browse()
  char title[72];
  char sub[56];
};

struct State {
  bool active;       // es gibt eine Warteschlange
  bool playing;      // nicht pausiert
  bool buffering;
  int index, count;
  char title[72];
  char artist[56];
  char id[16];
  uint16_t dur;
  float pos;         // Sekunden
  char msg[64];      // Fehler / Hinweis
};

static const int ART = 220;  // Cover-Kantenlänge in Pixeln

void begin();
bool bridgeKnown();
const char* bridgeUrl();
void bridgeChanged();        // Adresse in den Einstellungen geändert

void browse(const char* path);
bool browseBusy();
uint32_t browseSeq();        // zählt hoch, sobald ein neues Ergebnis da ist
// Ergebnis kopieren: Titel der Liste und bis zu max Einträge
int browseCopy(char* title, size_t titleLen, Item* out, int max, char* err, size_t errLen);

void playList(const Item* items, int n, int start);  // nur Einträge vom Typ K_TRACK
void playRadio(const char* videoId);
void toggle();
void next();
void prev();
void stop();
State state();
const uint16_t* art(uint32_t* version);  // RGB565, ART × ART, nullptr = keins

int takeRemoteVolume();      // −1 = nichts; sonst Lautstärke von der Fernbedienung

}  // namespace music
