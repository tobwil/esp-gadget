// Podcasts: Suche über die iTunes-Suche (ohne Schlüssel), Folgen aus dem RSS-Feed, Wiedergabe als
// MP3-Stream (libhelix). Die Position je Folge bleibt gespeichert; gemerkte Podcasts sind Favoriten (favs.h).
#pragma once
#include <Arduino.h>
#include "music.h"

namespace podcast {

void begin();
// Listen: "pc:subs" (Abos), "pcsearch:<Begriff>", "pc:feed:<Feed-URL>"
bool browse(const char* path, char* title, size_t titleLen, music::Item* items, int max, int* count, char* err,
            size_t errLen);

// Wiedergabe einer Folge (id = Audio-URL)
void play(const music::Item& episode);
void currentFeed(String& url, String& title);  // Feed der zuletzt gestarteten Folge (für „merken“)
// beliebige MP3-Adresse: Audiothek-Folge (mit gemerkter Position) oder Radiosender (live = ohne Position,
// ohne Springen, Titel aus dem Stream, verbindet bei Abbruch neu)
void playUrl(const char* url, const char* title, const char* show, const char* image, uint16_t dur, bool live);
void toggle();
void skip(int seconds);          // vor/zurück springen
void stop();
music::State state();

}  // namespace podcast
