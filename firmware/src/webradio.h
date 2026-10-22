// Internetradio: Senderverzeichnis von radio-browser.info (frei, ohne Schlüssel), nur MP3-Sender.
// Gespielt wird live über den MP3-Spieler (podcast.h), mit Titelanzeige aus dem Stream (ICY).
// (Eigener Name, weil „radio“ schon das Walkie-Talkie ist.)
#pragma once
#include <Arduino.h>
#include "music.h"

namespace wradio {

// Listen: "rl:home", "rl:top", "rl:tag:<Stichwort>", "rlsearch:<Name>"
// Sender sind Titel mit der Kennung "radio:<Stream-Adresse>"
bool browse(const char* path, char* title, size_t titleLen, music::Item* items, int max, int* count, char* err,
            size_t errLen);
String logo(const char* streamUrl);  // Senderlogo (falls bekannt)
void setLogo(const char* streamUrl, const String& image);  // z. B. für Livestreams der Audiothek

}  // namespace wradio
