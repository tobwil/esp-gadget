// Funk vom Handy: eine kleine HTTPS-Seite auf der Uhr (https://<Uhr>/), Knopf halten und sprechen – die Uhr spielt
// die Stimme ab. HTTPS ist nötig, weil Browser das Mikrofon sonst nicht freigeben. Das Zertifikat stellt sich die
// Uhr beim ersten Start selbst aus (der Browser warnt einmal, danach merkt er es sich).
#pragma once
#include <Arduino.h>

namespace phone {

void poll();          // aus der Hauptschleife: startet den Server, sobald WLAN da ist
bool talking();       // gerade spricht jemand vom Handy
String url();         // Adresse der Sprechseite, "" = (noch) nicht bereit

}  // namespace phone
