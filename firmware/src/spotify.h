// Spotify über cspot (Spotify Connect, Fork von philippe44):
//   - Kopplung: einmal in der Spotify-App diese Uhr als Lautsprecher wählen (Zeroconf), danach meldet
//     sie sich mit den gespeicherten Zugangsdaten selbst an – auch unterwegs ohne Handy-App
//   - die Spotify-App kann jederzeit parallel steuern; die Uhr kann selbst Titel starten (loadLocal)
//   - Suche und Listen über die Spotify-Web-API (Client-Credentials der eigenen Developer-App)
#pragma once
#include <Arduino.h>
#include "music.h"

namespace spotify {

void begin();
void poll();                     // aus der Hauptschleife (Zeroconf-Webserver)
bool configured();               // Client-ID/Secret in secrets.h eingetragen
bool linked();                   // Zugangsdaten gespeichert
bool connected();                // mit Spotify verbunden
const char* user();

// Mehrere Konten: jede Kopplung aus einer Spotify-App merkt sich das Konto (bis zu 3)
static const int ACCOUNTS = 3;
int accounts(char names[][64], int max, int* current);
void useAccount(int i);          // umschalten, ohne Handy
void removeAccount(int i);
void keysChanged();              // Client-ID/Secret geändert

// Wiedergabe
bool play(const std::vector<std::string>& uris, int index, const std::string& context);
void toggle();
void next();
void prev();
void stop();
void volumeChanged(int pct);     // Lautstärke an der Uhr geändert → an die App melden
music::State state();
int takeRemoteVolume();          // −1 = nichts; sonst Lautstärke aus der Spotify-App

// Katalog (Web-API)
bool browse(const char* path, char* title, size_t titleLen, music::Item* items, int max, int* count, char* err,
            size_t errLen);

}  // namespace spotify
