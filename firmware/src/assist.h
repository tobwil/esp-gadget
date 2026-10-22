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
String model();            // Live-Modell, z. B. „gemini-3.8-live“
String voice();            // Stimme, z. B. „Kore“
String sttModel();         // Modell für die Spracheingabe der Suche
void configure(const char* name, int search, const char* model = nullptr, const char* voice = nullptr,
               const char* sttModel = nullptr);  // nullptr / −1 = unverändert
// Verfügbare Modelle beim Gemini-Schlüssel abfragen (für die Auswahl in der Weboberfläche), als JSON
String models(String& err);

}  // namespace assist
