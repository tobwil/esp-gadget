// Zifferblatt im Kindermodus: eine kleine Pixel-Geschichte durch den Tag.
// Aufstehen, Frühstück, Kita, Mittagessen, Mittagsruhe, Spielen, Abendessen, Baden, Gute-Nacht-Geschichte, Schlafen –
// jede Szene als Pixelbild in einer von fünf Welten, mit Tier-Begleiter und in der Spielzeit dem Wochen-Abenteuer
// (gezeichnet in kidart.cpp). Der Ring zeigt den ganzen Tag (Aufstehen oben, im Uhrzeigersinn bis zur Bettzeit),
// was schon vorbei ist, ist dunkler; der helle Punkt ist „jetzt“.
// Gezeichnet wird in ein Raster von 116 × 116 Pixeln (die Szene selbst in 2 × 2-Blöcken), dann ×4 vergrößert.
#include <math.h>
#include <time.h>
#include "../kidart.h"
#include "../kids.h"
#include "../routines.h"
#include "../music.h"
#include "../net.h"
#include "ui.h"

using namespace ui;

namespace {

constexpr int S = 116, Z = 4, OUT = S * Z;  // Raster, Vergrößerung, Kantenlänge auf dem Bildschirm
using kidart::mix;
using kidart::rgb;
constexpr uint16_t BLACK = 0, WHITE = 0xFFFF;

uint16_t* px = nullptr;   // Raster
uint16_t* big = nullptr;  // vergrößert (Puffer der Leinwand)
lv_obj_t *canvas, *actL, *nextL, *touch;
int lastFrame = -1, lastMin = -1;

void put(int x, int y, uint16_t c) { if ((unsigned)x < S && (unsigned)y < S) px[y * S + x] = c; }
void fill(int x, int y, int w, int h, uint16_t c) {
  for (int j = max(0, y); j < min(S, y + h); j++)
    for (int i = max(0, x); i < min(S, x + w); i++) px[j * S + i] = c;
}
// ------------------------------------------------------------------ Uhrzeit, Akku, Ring

const uint16_t DIG[] = {0x7B6F, 0x2C97, 0x73E7, 0x73CF, 0x5BC9, 0x79CF, 0x79EF, 0x7292, 0x7BEF, 0x7BCF};

void glyph(int x, int y, char ch, int sc, uint16_t c) {
  uint16_t bits;
  int w = 3;
  if (ch >= '0' && ch <= '9') bits = DIG[ch - '0'];
  else if (ch == '%') bits = 0x52A5;
  else if (ch == ':') { w = 1; bits = 0; for (int j : {1, 3}) fill(x, y + j * sc, sc, sc, c); return; }
  else return;
  for (int j = 0; j < 5; j++)
    for (int i = 0; i < w; i++)
      if (bits & (1 << (14 - (j * 3 + i)))) fill(x + i * sc, y + j * sc, sc, sc, c);
}

int textW(const char* t, int sc) {
  int w = 0;
  for (const char* p = t; *p; p++) w += (*p == ':' ? 1 : 3) * sc + sc;
  return w ? w - sc : 0;
}

void text(int x, int y, const char* t, int sc, uint16_t c, bool outline) {
  if (outline)
    for (int oy = -1; oy <= 1; oy++)
      for (int ox = -1; ox <= 1; ox++)
        if (ox || oy) text(x + ox, y + oy, t, sc, BLACK, false);
  for (const char* p = t; *p; p++) {
    glyph(x, y, *p, sc, c);
    x += (*p == ':' ? 1 : 3) * sc + sc;
  }
}

uint16_t actColor(kids::Act a) {
  switch (a) {
    case kids::WAKE: return rgb(0xffd23f);
    case kids::BREAKFAST: case kids::LUNCH: case kids::DINNER: return rgb(0xff9a3c);
    case kids::DAYCARE: return rgb(0xf05a4a);
    case kids::NAP: return rgb(0xa08cff);
    case kids::PLAY: return rgb(0x4cd060);
    case kids::BATH: return rgb(0x48c9f0);
    case kids::STORY: return rgb(0x8a6bff);
    default: return rgb(0x34507a);
  }
}

void ring(const kids::Slot* sl, int n, int now, bool awake) {
  int w = sl[0].start, b = sl[n - 1].start;
  float span = b - w;
  const float C = 57.5f;
  for (int y = 0; y < S; y++)
    for (int x = 0; x < S; x++) {
      float dx = x + 0.5f - C, dy = y + 0.5f - C, d2 = dx * dx + dy * dy;
      if (d2 > 57.5f * 57.5f) { px[y * S + x] = BLACK; continue; }
      if (d2 < 52.5f * 52.5f) continue;
      if (d2 < 53.5f * 53.5f) { px[y * S + x] = BLACK; continue; }
      float a = atan2f(dx, -dy);
      if (a < 0) a += 2 * (float)M_PI;
      float m = w + a / (2 * (float)M_PI) * span;
      int k = 0;
      while (k + 1 < n - 1 && sl[k + 1].start <= m) k++;
      // schmale Lücke zwischen den Abschnitten
      bool gap = false;
      for (int i = 0; i < n; i++) {
        float da = fabsf(m - sl[i].start) / span * 2 * (float)M_PI * 55.5f;
        if (da < 0.9f || (i == n - 1 && a > 2 * (float)M_PI - 0.9f / 55.5f)) gap = true;
      }
      uint16_t c = gap ? BLACK : actColor(sl[k].act);
      if (!gap && (!awake || m < now)) c = mix(c, BLACK, 165);
      px[y * S + x] = c;
    }
  if (awake) {  // „jetzt“
    float a = (now - w) / span * 2 * (float)M_PI;
    float mx = C + sinf(a) * 55.5f, my = C - cosf(a) * 55.5f;
    for (int y = (int)my - 3; y <= (int)my + 3; y++)
      for (int x = (int)mx - 3; x <= (int)mx + 3; x++) {
        float d = hypotf(x + 0.5f - mx, y + 0.5f - my);
        if (d < 2.6f) put(x, y, rgb(0xfff6c8));
        else if (d < 3.6f) put(x, y, BLACK);
      }
  }
}

void battery(int cx, int y) {
  int bat = hwBattery();
  char t[8];
  if (bat >= 0) snprintf(t, sizeof(t), "%d%%", bat);
  else t[0] = 0;
  int w = 11 + (t[0] ? 2 + textW(t, 1) : 0), x = cx - w / 2;
  fill(x - 1, y - 1, 13, 7, BLACK);
  fill(x, y, 10, 5, WHITE);
  fill(x + 10, y + 1, 1, 3, WHITE);
  fill(x + 1, y + 1, 8, 3, BLACK);
  int lvl = bat < 0 ? 8 : max(1, (bat * 8 + 50) / 100);
  uint16_t c = hwCharging() || bat < 0 ? rgb(0x48c9f0) : bat < 20 ? rgb(0xff453a) : bat < 40 ? rgb(0xffd23f) : rgb(0x4cd060);
  fill(x + 1, y + 1, lvl, 3, c);
  if (t[0]) text(x + 13, y, t, 1, WHITE, true);
}

void note(int x, int y) {
  static const char* const N[] = {"..###", "..#.#", "..#.#", "###..", "###.."};
  for (int j = 0; j < 5; j++)
    for (int i = 0; i < 5; i++)
      if (N[j][i] == '#') put(x + i, y + j, rgb(0xff5f57));
}

void render(int f) {
  struct timeval tv;
  gettimeofday(&tv, nullptr);
  struct tm t;
  localtime_r(&tv.tv_sec, &t);
  bool valid = timeValid();
  if (kidFaceTestMin >= 0) { valid = true; t.tm_hour = kidFaceTestMin / 60; t.tm_min = kidFaceTestMin % 60; t.tm_wday = 3; }
  int now = valid ? t.tm_hour * 60 + t.tm_min : kids::cfg().wake + 120;
  bool weekend = valid && (t.tm_wday == 0 || t.tm_wday == 6);
  kids::Slot sl[kids::SLOTS_MAX];
  int n = kids::schedule(sl, weekend);
  bool awake = now >= sl[0].start && now < sl[n - 1].start;
  int k = n - 1;  // SLEEP
  if (awake) { k = 0; while (k + 1 < n && sl[k + 1].start <= now) k++; }
  kids::Act act = sl[k].act;

  const kids::Cfg& kc = kids::cfg();
  int day = valid ? (t.tm_wday + 6) % 7 : 0;  // 0 = Montag
  int week = valid ? (t.tm_yday + 7 - day) / 7 : 0;
  kidart::Scene sc{};
  sc.act = act;
  sc.weekend = weekend;
  sc.world = (kidart::World)(kc.world < kidart::WORLDS ? kc.world : week % kidart::WORLDS);  // sonst jede Woche eine andere
  sc.pet = (kidart::Pet)kc.pet;
  int stars = routines::starsTotal();
  sc.petLevel = stars >= 60 ? 3 : stars >= 20 ? 2 : 1;
  sc.petHappy = routines::starsToday() >= 3;
  sc.adventure = kc.adventure;
  sc.day = day;
  int from = sl[k].start, to = k + 1 < n ? sl[k + 1].start : from + 60;
  sc.progress = awake && to > from ? (float)(now - from) / (to - from) : 0;
  sc.frame = f;
  kidart::draw(px, sc);
  bool adv = awake && kidart::showsAdventure(sc);
  fill(0, 96, S, S - 96, BLACK);  // Bühne für die Beschriftung
  ring(sl, n, now, awake);

  char b[8];
  if (valid) snprintf(b, sizeof(b), "%d:%02d", t.tm_hour, t.tm_min);
  else snprintf(b, sizeof(b), "--:--");
  int tw = textW(b, 2);
  if (valid) text(58 - tw / 2, 12, b, 2, WHITE, true);
  battery(58, 25);
  if (music::state().playing) note(58 + tw / 2 + 4, 13 + f % 2);

  // Beschriftung: was gerade dran ist und bis wann
  const char* name = adv ? kidart::chapter(sc.world, day) : awake ? kids::slotName(sl[k], weekend) : kids::actName(act, weekend);
  if (strcmp(lv_label_get_text(actL), name)) {
    lv_obj_set_style_text_font(actL, strlen(name) > 14 ? &font_text_20 : &font_text_24, 0);
    lv_label_set_text(actL, name);
  }
  lv_obj_set_style_text_color(actL, lv_color_hex(0xffffff), 0);
  int until = awake ? (k + 1 < n ? sl[k + 1].start : sl[n - 1].start) : sl[0].start;
  char nb[40];
  if (adv) snprintf(nb, sizeof(nb), "Kapitel %d · bis %d:%02d", day + 1, until / 60, until % 60);
  else snprintf(nb, sizeof(nb), "bis %d:%02d", until / 60, until % 60);
  if (!valid) nb[0] = 0;
  if (strcmp(lv_label_get_text(nextL), nb)) lv_label_set_text(nextL, nb);

  // ×4 vergrößern
  for (int y = 0; y < S; y++) {
    uint16_t* row = big + y * Z * OUT;
    const uint16_t* src = px + y * S;
    for (int x = 0; x < S; x++) {
      uint16_t c = src[x];
      uint16_t* d = row + x * Z;
      d[0] = d[1] = d[2] = d[3] = c;
    }
    for (int r = 1; r < Z; r++) memcpy(row + r * OUT, row, OUT * 2);
  }
  lv_obj_invalidate(canvas);
}

}  // namespace

int kidFaceTestMin = -1;

void kidFaceBuild(lv_obj_t* scr) {
  if (!px) px = (uint16_t*)heap_caps_malloc(S * S * 2, MALLOC_CAP_SPIRAM);
  if (!big) big = (uint16_t*)heap_caps_malloc(OUT * OUT * 2, MALLOC_CAP_SPIRAM);
  canvas = lv_canvas_create(scr);
  lv_canvas_set_buffer(canvas, big, OUT, OUT, LV_COLOR_FORMAT_RGB565);
  lv_obj_set_pos(canvas, (W - OUT) / 2, (H - OUT) / 2);
  actL = label(scr, &font_text_24, C_TEXT(), "");
  lv_obj_align(actL, LV_ALIGN_TOP_MID, 0, 386);
  nextL = label(scr, &font_text_20, C_DIM(), "");
  lv_obj_align(nextL, LV_ALIGN_TOP_MID, 0, 416);
  // ganze Fläche: Antippen führt zu den Aufgaben (wenn gerade welche dran sind), sonst zur Musik, wenn etwas läuft
  touch = lv_obj_create(scr);
  lv_obj_remove_style_all(touch);
  lv_obj_set_size(touch, W, H);
  lv_obj_add_flag(touch, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(touch, [](lv_event_t*) {
    if (bezelMoved()) return;
    if (routines::active()) push(&PAGE_ROUTINE);
    else if (music::state().active) push(&PAGE_MUSIC);
  }, LV_EVENT_SHORT_CLICKED, nullptr);
  bezel(touch, []() { return music::state().active; });
  lastFrame = lastMin = -1;
  render(0);
}

void kidFaceUpdate() {
  int f = millis() / 600 + (kidFaceTestMin >= 0 ? kidFaceTestMin * 1000 : 0);
  if (f == lastFrame) return;
  lastFrame = f;
  render(f);
}
