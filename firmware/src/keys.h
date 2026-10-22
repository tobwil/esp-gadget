// API-Schlüssel: im Gerät gespeichert (über die Weboberfläche änderbar), Startwert aus include/secrets.h.
#pragma once
#include <Arduino.h>

namespace keys {

void begin();
const char* gemini();
void set(const char* gemini);

}  // namespace keys
