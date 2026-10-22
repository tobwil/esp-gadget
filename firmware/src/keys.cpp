#include "keys.h"
#include <Preferences.h>
#if __has_include("secrets.h")
#include "secrets.h"
#endif
#ifndef GEMINI_API_KEY
#define GEMINI_API_KEY ""
#endif

namespace keys {

static char gem[128];

void begin() {
  Preferences p;
  p.begin("keys", true);
  // gespeicherter Wert hat Vorrang; sonst der aus secrets.h
  if (!p.getString("gem", gem, sizeof(gem))) snprintf(gem, sizeof(gem), "%s", GEMINI_API_KEY);
  p.end();
}

const char* gemini() { return gem; }

void set(const char* g) {
  Preferences p;
  p.begin("keys", false);
  snprintf(gem, sizeof(gem), "%s", g);
  p.putString("gem", gem);
  p.end();
}

}  // namespace keys
