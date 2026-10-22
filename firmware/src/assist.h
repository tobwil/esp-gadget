// KI-Assistent (nur außerhalb des Kindermodus): Gemini Live über WebSocket, Sprache rein (16 kHz) und
// raus (24 kHz → Sprachkanal). Bedienung: halten und sprechen (Push-to-Talk), loslassen = Frage fertig.
// Werkzeuge: Google-Suche (in Gemini), Timer, Wecker und – falls eingerichtet – freigegebene n8n-Workflows/-Agenten.
#pragma once
#include <Arduino.h>

namespace assist {

enum Phase : uint8_t { OFF, CONNECTING, READY, LISTENING, THINKING, SPEAKING, TOOL, FAILED };

void begin();
bool available();          // Schlüssel da und nicht im Kindermodus
void open();               // Sitzung aufbauen (im Hintergrund)
void close();
void talk(bool down);      // Taste gedrückt / losgelassen
Phase phase();
float level();             // Pegel für die Figur (Mikrofon beim Zuhören, Ausgabe beim Sprechen)
String said();             // was die Assistentin zuletzt gesagt hat (Mitschrift)
String heard();            // was verstanden wurde
String status();           // Hinweis (Werkzeug läuft, Fehler)

// Einstellungen (Weboberfläche)
String name();
bool search();             // Google-Suche erlaubt
void configure(const char* name, int search);  // nullptr / −1 = unverändert

}  // namespace assist
