#include "keys.h"
#include <Preferences.h>
#if __has_include("secrets.h")
#include "secrets.h"
#endif
#ifndef SPOTIFY_CLIENT_ID
#define SPOTIFY_CLIENT_ID ""
#endif
#ifndef SPOTIFY_CLIENT_SECRET
#define SPOTIFY_CLIENT_SECRET ""
#endif
#ifndef GEMINI_API_KEY
#define GEMINI_API_KEY ""
#endif

namespace keys {

static char spId[80], spSecret[80], gem[128];

void begin() {
  Preferences p;
  p.begin("keys", true);
  // gespeicherte Werte haben Vorrang; sonst die aus secrets.h
  if (!p.getString("spid", spId, sizeof(spId))) snprintf(spId, sizeof(spId), "%s", SPOTIFY_CLIENT_ID);
  if (!p.getString("spsec", spSecret, sizeof(spSecret))) snprintf(spSecret, sizeof(spSecret), "%s", SPOTIFY_CLIENT_SECRET);
  if (!p.getString("gem", gem, sizeof(gem))) snprintf(gem, sizeof(gem), "%s", GEMINI_API_KEY);
  p.end();
}

const char* spotifyId() { return spId; }
const char* spotifySecret() { return spSecret; }
const char* gemini() { return gem; }

void set(const char* id, const char* secret, const char* g) {
  Preferences p;
  p.begin("keys", false);
  if (id) { snprintf(spId, sizeof(spId), "%s", id); p.putString("spid", spId); }
  if (secret) { snprintf(spSecret, sizeof(spSecret), "%s", secret); p.putString("spsec", spSecret); }
  if (g) { snprintf(gem, sizeof(gem), "%s", g); p.putString("gem", gem); }
  p.end();
}

}  // namespace keys
