// Einstellungen sichern und wiederherstellen (Weboberfläche → Gerät → Sicherung).
// Ohne „mit Geheimnissen“ fehlen WLAN-Passwörter, Gemini-Schlüssel, n8n-Token und Eltern-PIN.
#pragma once
#include <Arduino.h>

namespace backup {

String make(bool secrets);                       // JSON
bool restore(const String& json, String& err);   // übernimmt alles, was in der Datei steht

}  // namespace backup
