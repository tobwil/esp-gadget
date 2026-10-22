// Gemerkte Listen (Spotify-Alben, -Playlists, -Interpreten, Podcasts), im Gerät gespeichert.
// Dieselbe Ablage dient auch für die Freigaben im Kindermodus (kids.h).
#pragma once
#include <Arduino.h>
#include <vector>
#include "music.h"

namespace favs {

static const int MAX = 30;

class Store {
 public:
  explicit Store(const char* ns) : ns_(ns) {}
  void begin();
  bool has(const char* path);
  void toggle(const char* path, const char* title, const char* sub);
  void set(const char* path, const char* title, const char* sub, bool on);
  // Einträge als Listenzeilen; prefix = nur diese Art (z. B. „pc:feed:“), nullptr = alle
  int list(music::Item* out, int max, const char* prefix = nullptr);

 private:
  struct Entry { String path, title, sub; };
  const char* ns_;
  std::vector<Entry> all_;
  SemaphoreHandle_t mtx_ = nullptr;
  void save();
};

// Favoriten
void begin();
bool has(const char* path);
void toggle(const char* path, const char* title, const char* sub);
int list(music::Item* out, int max, const char* prefix = nullptr);

}  // namespace favs
