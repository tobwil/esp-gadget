// Weboberfläche zum Einrichten vom PC oder Handy aus: http://<netHost()>.local bzw. die IP der Uhr.
// Gerätename, WLAN, Spotify-Konten und -Schlüssel, Gemini-Schlüssel, Favoriten, Infos.
// Schlüssel werden nie ausgelesen, nur gesetzt. Nur im eigenen WLAN erreichbar.
#pragma once

namespace webui {

void begin();
void poll();   // aus der Hauptschleife

}  // namespace webui
