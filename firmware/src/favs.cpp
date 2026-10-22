#include "favs.h"
#include <ArduinoJson.h>
#include <Preferences.h>

namespace favs {

void Store::save() {
  JsonDocument d;
  for (auto& f : all_) {
    JsonObject o = d.add<JsonObject>();
    o["p"] = f.path;
    o["t"] = f.title;
    o["s"] = f.sub;
  }
  String out;
  serializeJson(d, out);
  Preferences p;
  p.begin(ns_, false);
  p.putString("all", out);
  p.end();
}

void Store::begin() {
  mtx_ = xSemaphoreCreateMutex();
  Preferences p;
  p.begin(ns_, true);
  String s = p.getString("all", "[]");
  p.end();
  JsonDocument d;
  if (deserializeJson(d, s)) return;
  for (JsonObject o : d.as<JsonArray>()) all_.push_back({o["p"] | "", o["t"] | "", o["s"] | ""});
}

bool Store::has(const char* path) {
  xSemaphoreTake(mtx_, portMAX_DELAY);
  bool r = false;
  for (auto& f : all_) if (f.path == path) { r = true; break; }
  xSemaphoreGive(mtx_);
  return r;
}

void Store::set(const char* path, const char* title, const char* sub, bool on) {
  xSemaphoreTake(mtx_, portMAX_DELAY);
  int at = -1;
  for (size_t i = 0; i < all_.size(); i++) if (all_[i].path == path) { at = i; break; }
  if (!on && at >= 0) all_.erase(all_.begin() + at);
  if (on && at < 0) {
    if (all_.size() >= MAX) all_.pop_back();
    all_.insert(all_.begin(), {path, title, sub});  // neueste zuerst
  }
  if (on != (at >= 0)) save();
  xSemaphoreGive(mtx_);
}

void Store::toggle(const char* path, const char* title, const char* sub) { set(path, title, sub, !has(path)); }

int Store::list(music::Item* out, int max, const char* prefix) {
  xSemaphoreTake(mtx_, portMAX_DELAY);
  int n = 0;
  for (auto& f : all_) {
    if (n >= max) break;
    if (prefix && !f.path.startsWith(prefix)) continue;
    music::Item& o = out[n++];
    o.kind = f.path.startsWith("radio:") ? music::K_TRACK : music::K_LIST;  // Sender sind direkt spielbar
    o.dur = 0;
    snprintf(o.id, sizeof(o.id), "%s", f.path.c_str());
    snprintf(o.title, sizeof(o.title), "%s", f.title.c_str());
    snprintf(o.sub, sizeof(o.sub), "%s", f.sub.c_str());
  }
  xSemaphoreGive(mtx_);
  return n;
}

static Store fav("fav");

void begin() { fav.begin(); }
bool has(const char* path) { return fav.has(path); }
void toggle(const char* path, const char* title, const char* sub) { fav.toggle(path, title, sub); }
int list(music::Item* out, int max, const char* prefix) { return fav.list(out, max, prefix); }

}  // namespace favs
