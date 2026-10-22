// HTTPS-Server der Uhr (Port 443, selbst ausgestelltes Zertifikat – der Browser warnt einmal):
//   /funk  Funk vom Handy: Knopf halten und sprechen, die Uhr spielt die Stimme ab; BOOT-Halten auf der Uhr antwortet
//   /ws    Sprachkanal (WebSocket, 16 kHz PCM in beide Richtungen)
//   alles andere ist die Weboberfläche (webui::handle) – sie läuft komplett über HTTPS, Port 80 leitet nur um.
#pragma once
#include <Arduino.h>

namespace phone {

void poll();          // aus der Hauptschleife: startet den Server, sobald WLAN da ist
bool talking();       // gerade spricht jemand vom Handy
bool connected();     // die Handy-Seite ist offen (dann geht BOOT-Halten ans Handy statt an alle Uhren)
void talk(bool on);   // von der Uhr ans Handy sprechen (Mikrofon → Handy)
String url();         // Adresse der Sprechseite, "" = (noch) nicht bereit

}  // namespace phone
