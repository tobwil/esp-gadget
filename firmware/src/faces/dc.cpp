#include "dc.h"
#include <math.h>
#include <string.h>
#include <stdio.h>
#include <algorithm>
using std::max;
using std::min;
#include "strokefont.h"
#include "../fonts/fonts.h"

namespace fc {

static inline lv_color_t C(uint32_t c) { return lv_color_hex(c & 0xFFFFFF); }

// ------------------------------------------------------------------ Grundformen

void Dc::drawLine(float x1, float y1, float x2, float y2, bool round) {
  lv_draw_line_dsc_t d;
  lv_draw_line_dsc_init(&d);
  d.color = C(col);
  d.width = fmaxf(1, pen * K);
  d.p1 = {X(x1), X(y1)};
  d.p2 = {X(x2), X(y2)};
  // LVGL teilt bei schrägen Linien durch die auf ganze Pixel abgeschnittene Länge: Linien unter einem Pixel als Punkt
  if ((int32_t)d.p1.x == (int32_t)d.p2.x && (int32_t)d.p1.y == (int32_t)d.p2.y) {
    if (round) fillCircle(x1, y1, pen / 2);
    return;
  }
  d.round_start = d.round_end = round;
  // Waagerechte/senkrechte Linien zeichnet LVGL ohne runde Enden: Enden selbst mit Kreisen abrunden
  bool straight = d.p1.y == d.p2.y || d.p1.x == d.p2.x;
  if (round && straight) d.round_start = d.round_end = 0;
  lv_draw_line(L, &d);
  if (round && straight) {
    fillCircle(x1, y1, pen / 2);
    fillCircle(x2, y2, pen / 2);
  }
}

void Dc::fillCircle(float x, float y, float r) {
  float rr = r * K;
  if (rr < 0.5f) return;
  lv_draw_rect_dsc_t d;
  lv_draw_rect_dsc_init(&d);
  d.bg_color = C(col);
  d.radius = LV_RADIUS_CIRCLE;
  float cx = X(x), cy = X(y);
  lv_area_t a = {(int32_t)lroundf(cx - rr), (int32_t)lroundf(cy - rr), (int32_t)lroundf(cx + rr) - 1,
                 (int32_t)lroundf(cy + rr) - 1};
  if (a.x2 < a.x1) a.x2 = a.x1;
  if (a.y2 < a.y1) a.y2 = a.y1;
  lv_draw_rect(L, &d, &a);
}

void Dc::arc(float cx, float cy, float r, float a0, float a1, bool round) {
  if (a1 - a0 < 0.4f) return;
  lv_draw_arc_dsc_t d;
  lv_draw_arc_dsc_init(&d);
  d.color = C(col);
  d.width = fmaxf(1, pen * K);
  d.center = {(int32_t)lroundf(X(cx)), (int32_t)lroundf(X(cy))};
  d.radius = (r + pen / 2) * K;
  if (a1 - a0 >= 359.9f) { d.start_angle = 0; d.end_angle = 360; }
  else { d.start_angle = a0 - 90; d.end_angle = a1 - 90; }
  d.rounded = round;
  lv_draw_arc(L, &d);
}

void Dc::drawCircle(float x, float y, float r) { arc(x, y, r, 0, 360); }

void Dc::fillRect(float x, float y, float w, float h) {
  if (w <= 0 || h <= 0) return;
  lv_draw_rect_dsc_t d;
  lv_draw_rect_dsc_init(&d);
  d.bg_color = C(col);
  lv_area_t a = {(int32_t)floorf(X(x)), (int32_t)floorf(X(y)), (int32_t)ceilf(X(x + w)) - 1, (int32_t)ceilf(X(y + h)) - 1};
  if (a.x2 < a.x1) a.x2 = a.x1;
  if (a.y2 < a.y1) a.y2 = a.y1;
  lv_draw_rect(L, &d, &a);
}

void Dc::fillTri(float x1, float y1, float x2, float y2, float x3, float y3) {
  lv_draw_triangle_dsc_t d;
  lv_draw_triangle_dsc_init(&d);
  d.bg_color = C(col);
  d.bg_opa = LV_OPA_COVER;
  d.p[0] = {X(x1), X(y1)};
  d.p[1] = {X(x2), X(y2)};
  d.p[2] = {X(x3), X(y3)};
  lv_draw_triangle(L, &d);
}

void Dc::fillPoly(const float* p, int n) {
  for (int i = 1; i + 1 < n; i++) fillTri(p[0], p[1], p[2 * i], p[2 * i + 1], p[2 * i + 2], p[2 * i + 3]);
}

void Dc::fillStrip(const float* a, const float* b, int n) {
  for (int i = 0; i + 1 < n; i++) {
    fillTri(a[2 * i], a[2 * i + 1], a[2 * i + 2], a[2 * i + 3], b[2 * i + 2], b[2 * i + 3]);
    fillTri(a[2 * i], a[2 * i + 1], b[2 * i + 2], b[2 * i + 3], b[2 * i], b[2 * i + 1]);
  }
}

void Dc::fillEllipse(float x, float y, float rx, float ry) {
  float p[2 * 28];
  for (int i = 0; i < 28; i++) {
    float t = i * 6.2831853f / 28;
    p[2 * i] = x + rx * cosf(t);
    p[2 * i + 1] = y + ry * sinf(t);
  }
  fillPoly(p, 28);
}

float Dc::textW(const char* s, const lv_font_t* f) {
  lv_point_t sz;
  lv_text_get_size(&sz, s, f, 0, 0, 2000, LV_TEXT_FLAG_NONE);
  return sz.x / K;
}

float Dc::text(const char* s, float x, float y, const lv_font_t* f, int just) {
  lv_point_t sz;
  lv_text_get_size(&sz, s, f, 0, 0, 2000, LV_TEXT_FLAG_NONE);
  float sx = X(x);
  if (just == 1) sx -= sz.x / 2.0f;
  else if (just == 2) sx -= sz.x;
  // Oberkante der Großbuchstaben = Zeilenoberkante + (Zeilenhöhe − Grundlinie − Versalhöhe)
  float cap = f->line_height - f->base_line - f->line_height * 0.56f;
  float sy = X(y) - cap;
  lv_draw_label_dsc_t d;
  lv_draw_label_dsc_init(&d);
  d.font = f;
  d.color = C(col);
  d.text = s;
  d.text_local = 1;  // LVGL zeichnet später: Text kopieren
  lv_area_t a = {(int32_t)sx, (int32_t)sy, (int32_t)sx + sz.x + 2, (int32_t)sy + sz.y};
  lv_draw_label(L, &d, &a);
  return sz.x / K;
}

void Dc::clip(float x, float y, float w, float h) {
  if (!clipped) clipSave = L->_clip_area;
  clipped = true;
  lv_area_t a = {(int32_t)floorf(X(x)), (int32_t)floorf(X(y)), (int32_t)ceilf(X(x + w)) - 1, (int32_t)ceilf(X(y + h)) - 1};
  lv_area_t r = {max(a.x1, clipSave.x1), max(a.y1, clipSave.y1), min(a.x2, clipSave.x2), min(a.y2, clipSave.y2)};
  L->_clip_area = r;  // leer, wenn x2 < x1 – LVGL zeichnet dann nichts
}

void Dc::noClip() {
  if (clipped) L->_clip_area = clipSave;
  clipped = false;
}

void battery(Dc& dc, const Data& d, float y, uint32_t col) {
  char b[24];
  if (d.batt < 0) snprintf(b, sizeof(b), I_BOLT " USB");
  else snprintf(b, sizeof(b), "%s %d %%", d.charging ? I_CHARGING : d.batt < 15 ? I_BATTERY_LOW : I_BATTERY, d.batt);
  dc.setColor(d.batt >= 0 && d.batt < 15 && !d.charging ? 0xff453a : col);
  dc.text(b, 227, y, &font_text_20, 1);
}

void tick(Dc& dc, float cx, float cy, float r1, float r2, float deg) {
  dc.drawLine(ptX(cx, r1, deg), ptY(cy, r1, deg), ptX(cx, r2, deg), ptY(cy, r2, deg));
}

// ------------------------------------------------------------------ Farben und Mathe

float clamp(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }

uint32_t mix(uint32_t a, uint32_t b, float t) {
  t = clamp(t, 0, 1);
  int r = ((a >> 16) & 255) + (int)((((int)((b >> 16) & 255)) - (int)((a >> 16) & 255)) * t);
  int g = ((a >> 8) & 255) + (int)((((int)((b >> 8) & 255)) - (int)((a >> 8) & 255)) * t);
  int bl = (a & 255) + (int)((((int)(b & 255)) - (int)(a & 255)) * t);
  return ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)bl;
}

float ease(float t) { t = clamp(t, 0, 1); float u = 1 - t; return 1 - u * u * u; }
float easeInOut(float t) {
  t = clamp(t, 0, 1);
  return t < 0.5f ? 4 * t * t * t : 1 - powf(-2 * t + 2, 3) / 2;
}
float spring(float t) {
  t = clamp(t, 0, 1);
  const float c1 = 1.70158f, c3 = c1 + 1;
  return 1 + c3 * powf(t - 1, 3) + c1 * powf(t - 1, 2);
}
float stag(float p, int i, float step, float dur) { return clamp((p - i * step) / dur, 0, 1); }

uint32_t rnd(uint32_t seed) {
  uint32_t s = seed % 2147483647u;
  return (uint32_t)(((uint64_t)(s == 0 ? 1 : s) * 16807u) % 2147483647u);
}

float noise(float x) {
  return (sinf(x * 1.7f) + sinf(x * 3.1f + 1.3f) * 0.5f + sinf(x * 5.3f + 4.1f) * 0.25f) / 1.75f;
}

// ------------------------------------------------------------------ Strichschrift

static const int GAP = 22;  // Abstand zwischen Zeichen in Viertel-Einheiten

// UTF-8 → Latin-1 in Großbuchstaben (ß wird zu SS, wie toUpperCase() in JavaScript)
static int upperCodes(const char* s, uint8_t* out, int max) {
  int n = 0;
  const uint8_t* p = (const uint8_t*)s;
  while (*p && n < max - 1) {
    uint32_t c = *p++;
    if (c >= 0xC0 && (*p & 0xC0) == 0x80) {
      c = ((c & 0x1F) << 6) | (*p++ & 0x3F);
      while ((*p & 0xC0) == 0x80) p++;  // längere Folgen: kein Latin-1
      if (c > 255) c = '?';
    }
    if (c >= 'a' && c <= 'z') c -= 32;
    else if (c == 0xE4 || c == 0xF6 || c == 0xFC) c -= 32;  // ä ö ü
    else if (c == 0xDF) { out[n++] = 'S'; c = 'S'; }
    out[n++] = c;
  }
  return n;
}

static int glyphOf(uint8_t c) { return (int)SF_MAP[c] - 1; }
static int advance(uint8_t c) { int g = glyphOf(c); return g < 0 ? 20 + GAP : SF_W[g] + GAP; }

float stW(const char* s, float h) {
  uint8_t t[64];
  int n = upperCodes(s, t, sizeof(t));
  int w = 0;
  for (int k = 0; k < n; k++) w += advance(t[k]);
  if (n) w -= GAP;
  return w * h / 80.0f;
}

static float glyphLen(int gi) {
  float len = 0;
  int px = -1, py = -1;
  for (int i = SF_OFFS[gi]; i < SF_OFFS[gi + 1]; i += 2) {
    if (SF_DATA[i] == 255) { px = -1; i -= 1; continue; }
    int gx = SF_DATA[i] & 127, gy = SF_DATA[i + 1];
    if (px >= 0) len += sqrtf((gx - px) * (gx - px) + (gy - py) * (gy - py));
    px = gx;
    py = gy;
  }
  return len;
}

// Kleine Strichschrift wird auf dem AMOLED klecksig: bis Versalhöhe 24 setzen wir Inter in Großbuchstaben.
// Die Schreib-Animation (prog) wird dann zu „Buchstabe für Buchstabe“.
static float stSmall(Dc& dc, const char* str, float x, float y, float h, int just, float prog) {
  char up[64];
  int j = 0;
  for (const uint8_t* p = (const uint8_t*)str; *p && j < 60; p++) {
    uint8_t c = *p;
    if (c >= 'a' && c <= 'z') c -= 32;
    else if (c == 0xC3 && (p[1] == 0xA4 || p[1] == 0xB6 || p[1] == 0xBC)) { up[j++] = c; c = p[1] - 32; p++; }  // äöü → ÄÖÜ
    up[j++] = c;
  }
  up[j] = 0;
  if (prog < 1) {  // anteilig zeigen, nie mitten in einem UTF-8-Zeichen abschneiden
    int n = (int)(j * clamp(prog, 0, 1));
    while (n > 0 && ((uint8_t)up[n] & 0xC0) == 0x80) n--;
    if (n <= 0) return 0;
    up[n] = 0;
  }
  const lv_font_t* f = h <= 17 ? &font_text_20 : &font_text_24;
  float capH = f->line_height * 0.56f / K;
  return dc.text(up, x, y - capH / 2, f, just);
}

float st(Dc& dc, const char* str, float x, float y, float h, float pen, int just, float prog, float slant) {
  if (h <= 24) return stSmall(dc, str, x, y, h, just, prog);
  uint8_t t[64];
  int n = upperCodes(str, t, sizeof(t));
  float s = h / 80.0f;
  float w = 0;
  for (int k = 0; k < n; k++) w += advance(t[k]);
  if (n) w -= GAP;
  w *= s;
  float ox = just == 1 ? x - w / 2 : just == 2 ? x - w : x;
  float oy = y - h / 2 - 24 * s;
  float budget = 1e9f;
  if (prog < 1) {
    float total = 0;
    for (int k = 0; k < n; k++) { int g = glyphOf(t[k]); if (g >= 0) total += glyphLen(g); }
    budget = fmaxf(0, prog) * total;
    if (budget <= 0) return w;
  }
  float savePen = dc.pen;
  dc.pen = pen;
  for (int k = 0; k < n && budget > 0; k++) {
    int gi = glyphOf(t[k]);
    if (gi < 0) { ox += (20 + GAP) * s; continue; }
    bool have = false, single = true;
    float px = 0, py = 0;
    int lgx = 0, lgy = 0;
    for (int i = SF_OFFS[gi]; i < SF_OFFS[gi + 1] && budget > 0; i += 2) {
      uint8_t xb = SF_DATA[i];
      if (xb == 255) {
        if (single && have) dc.fillCircle(px, py, pen * 0.72f);
        have = false;
        single = true;
        i -= 1;
        continue;
      }
      int gx = xb & 127, gy = SF_DATA[i + 1];
      float nx = ox + gx * s + (slant ? (80 - gy) * s * slant : 0);
      float ny = oy + gy * s;
      if (have) {
        single = false;
        float seg = sqrtf((gx - lgx) * (gx - lgx) + (gy - lgy) * (gy - lgy));
        float tx = nx, ty = ny;
        if (seg > budget) { float f = budget / seg; tx = px + (nx - px) * f; ty = py + (ny - py) * f; }
        budget -= seg;
        dc.drawLine(px, py, tx, ty, true);
      }
      px = nx;
      py = ny;
      lgx = gx;
      lgy = gy;
      have = true;
    }
    if (single && have && budget > 0) dc.fillCircle(px, py, pen * 0.72f);
    ox += (SF_W[gi] + GAP) * s;
  }
  dc.pen = savePen;
  return w;
}

// ------------------------------------------------------------------ Sonne und Mond

static const float RAD = 0.01745329f;

static void sunEq(int doy, float hourUtc, float& eq, float& de) {
  float g = (2 * (float)M_PI / 365) * (doy - 1 + (hourUtc - 12) / 24);
  eq = 229.18f * (0.000075f + 0.001868f * cosf(g) - 0.032077f * sinf(g) - 0.014615f * cosf(2 * g) - 0.040849f * sinf(2 * g));
  de = 0.006918f - 0.399912f * cosf(g) + 0.070257f * sinf(g) - 0.006758f * cosf(2 * g) + 0.000907f * sinf(2 * g) -
       0.002697f * cosf(3 * g) + 0.00148f * sinf(3 * g);
}

float sunElev(int doy, float min, float lat, float lon, int tz) {
  float eq, de;
  sunEq(doy, (min - tz) / 60, eq, de);
  float tst = min + eq + 4 * lon - tz;
  float ha = (tst / 4 - 180) * RAD, la = lat * RAD;
  float cz = sinf(la) * sinf(de) + cosf(la) * cosf(de) * cosf(ha);
  return 90 - acosf(clamp(cz, -1, 1)) / RAD;
}

float sunCross(int doy, float lat, float lon, int tz, float el, bool rising) {
  float eq, de;
  sunEq(doy, 12 - tz / 60.0f, eq, de);
  float la = lat * RAD;
  float c = (sinf(el * RAD) - sinf(la) * sinf(de)) / (cosf(la) * cosf(de));
  if (c < -1 || c > 1) return -1;
  float ha = acosf(c) / RAD;
  float utc = 720 - 4 * (lon + (rising ? ha : -ha)) - eq;
  return fmodf(fmodf(utc + tz, 1440) + 1440, 1440);
}

float sunNoon(int doy, float lon, int tz) {
  float eq, de;
  sunEq(doy, 12 - tz / 60.0f, eq, de);
  return fmodf(fmodf(720 - 4 * lon - eq + tz, 1440) + 1440, 1440);
}

float moonPhase(double unix) {
  double jd = unix / 86400.0 + 2440587.5;
  double age = fmod(fmod(jd - 2451550.1, 29.530588853) + 29.530588853, 29.530588853);
  return (float)(age / 29.530588853);
}

}  // namespace fc
