// KI-Assistent: eine Figur aus kleinen Pixeln. In Ruhe bilden sie das „KI/“ von KI/sruptiv, beim Zuhören eine
// Kugel, die mit der Stimme atmet, beim Nachdenken einen kreisenden Ring, beim Sprechen pulsiert die Kugel.
// Bedienung: irgendwo halten (oder BOOT halten) und sprechen, loslassen = fertig.
#include <math.h>
#include "../assist.h"
#include "../music.h"
#include "ui.h"

using namespace ui;

namespace {

constexpr int N = 230;          // Pixel (das Logo braucht gut 180)
constexpr float PX = 9;         // Kantenlänge eines Pixels
struct Px { float x, y, vx, vy, tx, ty; uint8_t shade; };
Px* px = nullptr;  // im PSRAM
lv_obj_t *root, *nameL, *textL, *hintL;
lv_timer_t* timer = nullptr;
uint32_t lastStep = 0;
bool musicWasPlaying = false;
int nLogo = 0;
float *logoX = nullptr, *logoY = nullptr;

// „KI/“ als Raster: K (Balken + zwei Schrägen), I (Balken), dünner Schrägstrich tiefer
float segDist(float px_, float py, float ax, float ay, float bx, float by) {
  float dx = bx - ax, dy = by - ay, l = dx * dx + dy * dy;
  float t = l ? fmaxf(0, fminf(1, ((px_ - ax) * dx + (py - ay) * dy) / l)) : 0;
  float cx = ax + t * dx - px_, cy = ay + t * dy - py;
  return sqrtf(cx * cx + cy * cy);
}

void buildLogo() {
  nLogo = 0;
  const float C = 10.5f;  // Rasterabstand
  for (int gy = 0; gy < 18 && nLogo < N; gy++)
    for (int gx = 0; gx < 24 && nLogo < N; gx++) {
      float x = gx + 0.5f, y = gy + 0.5f;
      bool k = x < 3.0f;                                               // K: Balken
      k |= segDist(x, y, 3.0f, 8.0f, 9.4f, 0.6f) < 1.45f;              // K: obere Schräge
      k |= segDist(x, y, 4.8f, 6.4f, 9.6f, 13.4f) < 1.45f;             // K: untere Schräge
      bool on = k && y < 14;                                           // K und I enden bündig auf der Grundlinie
      on |= x > 12.0f && x < 15.0f && y < 14;                          // I
      on |= segDist(x, y, 16.4f, 17.4f, 21.6f, 4.0f) < 0.72f;          // / – lang und dünn, reicht unter die Grundlinie
      if (!on) continue;
      logoX[nLogo] = (x - 11.5f) * C;
      logoY[nLogo] = (y - 7.5f) * C - 10;
      nLogo++;
    }
}

// Ziele je nach Zustand
void targets(assist::Phase p, float t, float lvl) {
  if (p == assist::OFF || p == assist::READY || p == assist::FAILED) {
    for (int i = 0; i < N; i++) {
      if (i < nLogo) { px[i].tx = logoX[i]; px[i].ty = logoY[i]; }
      else {  // übrige Pixel: als feiner Staub außen herum
        float a = i * 2.39996f + t * 0.05f, r = 150 + (i % 7) * 6;
        px[i].tx = cosf(a) * r;
        px[i].ty = sinf(a) * r;
      }
    }
  } else if (p == assist::LISTENING || p == assist::SPEAKING) {
    float R = 62 + fminf(1, lvl * 2.2f) * 58 + (p == assist::SPEAKING ? 6 * sinf(t * 9) : 0);
    for (int i = 0; i < N; i++) {  // Sonnenblumen-Muster füllt eine Kugel gleichmäßig
      float r = R * sqrtf((i + 0.5f) / N), a = i * 2.39996f + t * (p == assist::SPEAKING ? 0.6f : 0.25f);
      px[i].tx = cosf(a) * r;
      px[i].ty = sinf(a) * r - 10;
    }
  } else {  // Verbinden, Nachdenken, Werkzeug: kreisender Ring
    for (int i = 0; i < N; i++) {
      float a = (float)i / N * 6.2832f + t * (p == assist::TOOL ? 2.4f : 1.4f);
      float r = 96 + 10 * sinf(a * 3 + t * 3);
      px[i].tx = cosf(a) * r;
      px[i].ty = sinf(a) * r - 10;
    }
  }
}

lv_color_t colorFor(assist::Phase p, uint8_t shade) {
  uint32_t base = p == assist::LISTENING ? 0x5EE6FF : p == assist::TOOL ? 0xFFB547 : p == assist::FAILED ? 0xFF6B5F : 0xFFFFFF;
  return lv_color_mix(lv_color_hex(base), lv_color_hex(0x000000), shade);  // shade = Anteil der Grundfarbe
}

void step(lv_timer_t*) {
  uint32_t now = millis();
  float dt = fminf(0.05f, (now - lastStep) / 1000.0f), t = now / 1000.0f;
  lastStep = now;
  assist::Phase p = assist::phase();
  targets(p, t, assist::level());
  for (int i = 0; i < N; i++) {  // Feder zum Ziel
    Px& q = px[i];
    q.vx += ((q.tx - q.x) * 38 - q.vx * 9) * dt;
    q.vy += ((q.ty - q.y) * 38 - q.vy * 9) * dt;
    q.x += q.vx * dt;
    q.y += q.vy * dt;
  }
  // Texte
  const char* hint = p == assist::CONNECTING ? "Verbinde …" : p == assist::LISTENING ? "Ich höre zu …"
                     : p == assist::THINKING ? "Denke nach …" : p == assist::TOOL ? "Arbeite …"
                     : p == assist::FAILED ? "" : p == assist::SPEAKING ? "" : "Halten und sprechen";
  if (strcmp(lv_label_get_text(hintL), hint)) lv_label_set_text(hintL, hint);
  String s = assist::status();
  if (!s.length()) s = p == assist::LISTENING ? assist::heard() : assist::said();
  // nur das Ende zeigen (zwei, drei Zeilen)
  if (s.length() > 110) { int k = s.indexOf(' ', s.length() - 110); s = "… " + s.substring(k < 0 ? s.length() - 110 : k + 1); }
  if (strcmp(lv_label_get_text(textL), s.c_str())) lv_label_set_text(textL, s.c_str());
  lv_obj_invalidate(root);
}

void draw(lv_event_t* e) {
  lv_layer_t* L = lv_event_get_layer(e);
  assist::Phase p = assist::phase();
  lv_draw_rect_dsc_t d;
  lv_draw_rect_dsc_init(&d);
  d.radius = 2;
  for (int i = 0; i < N; i++) {
    const Px& q = px[i];
    bool dust = (p == assist::OFF || p == assist::READY || p == assist::FAILED) && i >= nLogo;
    float s = dust ? 3 : PX;
    d.bg_color = colorFor(p, dust ? 70 : q.shade);
    lv_area_t a = {(int32_t)(CX + q.x - s / 2), (int32_t)(CY + q.y - s / 2), (int32_t)(CX + q.x + s / 2), (int32_t)(CY + q.y + s / 2)};
    lv_draw_rect(L, &d, &a);
  }
}

}  // namespace

static void build(lv_obj_t* scr) {
  if (!px) {
    px = (Px*)heap_caps_malloc(sizeof(Px) * N, MALLOC_CAP_SPIRAM);
    logoX = (float*)heap_caps_malloc(sizeof(float) * N, MALLOC_CAP_SPIRAM);
    logoY = (float*)heap_caps_malloc(sizeof(float) * N, MALLOC_CAP_SPIRAM);
  }
  buildLogo();
  for (int i = 0; i < N; i++) {  // aus dem Nichts zusammensetzen
    float a = i * 2.39996f, r = 260;
    px[i] = {cosf(a) * r, sinf(a) * r, 0, 0, 0, 0, (uint8_t)(170 + (i * 37) % 86)};
  }
  root = lv_obj_create(scr);
  lv_obj_remove_style_all(root);
  lv_obj_set_size(root, W, H);
  lv_obj_add_flag(root, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(root, draw, LV_EVENT_DRAW_MAIN, nullptr);
  lv_obj_add_event_cb(root, [](lv_event_t*) { assist::talk(true); play(3); }, LV_EVENT_PRESSED, nullptr);
  for (lv_event_code_t c : {LV_EVENT_RELEASED, LV_EVENT_PRESS_LOST})
    lv_obj_add_event_cb(root, [](lv_event_t*) { if (assist::phase() == assist::LISTENING) play(4); assist::talk(false); }, c, nullptr);
  nameL = label(scr, &font_text_24, C_DIM(), assist::name().c_str());
  lv_obj_align(nameL, LV_ALIGN_TOP_MID, 0, 34);
  hintL = label(scr, &font_text_20, C_DIM(), "");
  lv_obj_align(hintL, LV_ALIGN_TOP_MID, 0, 70);
  textL = label(scr, &font_text_20, C_TEXT(), "");
  lv_label_set_long_mode(textL, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(textL, 320);
  lv_obj_set_style_text_align(textL, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(textL, LV_ALIGN_TOP_MID, 0, 330);
  // Musik pausieren, solange die Assistentin offen ist (das Mikrofon soll nur dich hören)
  musicWasPlaying = music::state().playing;
  if (musicWasPlaying) music::toggle();
  if (!assist::available()) lv_label_set_text(textL, "Gemini-Schlüssel fehlt (Weboberfläche)");
  assist::open();
  lastStep = millis();
  timer = lv_timer_create(step, 33, nullptr);
}

static void leave() {
  if (timer) { lv_timer_delete(timer); timer = nullptr; }
  assist::close();
  if (musicWasPlaying && !music::state().playing) music::toggle();
}

const Page PAGE_ASSIST = {build, nullptr, leave, nullptr, false};
