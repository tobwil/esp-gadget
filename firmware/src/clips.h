// Sprachclips aus der Firmware (Zähneputzen: Luna und Fips sprechen). Die MP3-Stücke liegen in brushvoice.bin
// (tools/brush_voice.py) und laufen über den Sprachkanal – Musik wird dabei leiser, wie beim Funk.
#pragma once
#include <Arduino.h>

namespace clips {

bool has(const char* id);   // gibt es den Clip?
bool play(const char* id);  // hinten anstellen; false = unbekannt
void stop();                // alles abbrechen
bool busy();                // spricht gerade oder hat noch etwas vor

}  // namespace clips
