// API-Schlüssel: im Gerät gespeichert (über die Weboberfläche änderbar), Startwerte aus include/secrets.h.
#pragma once
#include <Arduino.h>

namespace keys {

void begin();
const char* spotifyId();
const char* spotifySecret();
const char* gemini();
void set(const char* spotifyId, const char* spotifySecret, const char* gemini);  // nullptr = unverändert

}  // namespace keys
