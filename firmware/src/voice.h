// Spracheingabe: Mikrofon aufnehmen, an Gemini schicken, Text zurück (für die Suche).
// Bedienung: Knopf halten und sprechen (start), loslassen (finish).
#pragma once
#include <Arduino.h>

namespace voice {

enum Phase : uint8_t { IDLE, LISTENING, THINKING, DONE, FAILED };

void begin();
bool configured();       // Gemini-Schlüssel in secrets.h
void start();
void finish();
void cancel();
Phase phase();
const char* text();      // erkannter Text (DONE) bzw. Fehlermeldung (FAILED)
float level();           // Mikrofonpegel 0..1 beim Zuhören

}  // namespace voice
