// Zifferblatt im Kindermodus: eine kleine Pixel-Geschichte durch den Tag.
// Aufstehen, Frühstück, Kita, Mittagessen, Mittagsruhe, Spielen, Abendessen, Baden, Gute-Nacht-Geschichte, Schlafen –
// jede Szene als Pixelbild. Der Ring zeigt den ganzen Tag (Aufstehen oben, im Uhrzeigersinn bis zur Bettzeit),
// was schon vorbei ist, ist dunkler; der helle Punkt ist „jetzt“.
// Gezeichnet wird in ein Raster von 116 × 116 Pixeln (die Szene selbst in 2 × 2-Blöcken), dann ×4 vergrößert.
#include <math.h>
#include <time.h>
#include "../kids.h"
#include "../routines.h"
#include "../music.h"
#include "../net.h"
#include "ui.h"

using namespace ui;

namespace {

constexpr int S = 116, Z = 4, OUT = S * Z;  // Raster, Vergrößerung, Kantenlänge auf dem Bildschirm
constexpr uint16_t rgb(uint32_t c) { return ((c >> 8) & 0xF800) | ((c >> 5) & 0x07E0) | ((c >> 3) & 0x1F); }
constexpr uint16_t BLACK = 0, WHITE = 0xFFFF;

uint16_t* px = nullptr;   // Raster
uint16_t* big = nullptr;  // vergrößert (Puffer der Leinwand)
lv_obj_t *canvas, *actL, *nextL, *touch;
int lastFrame = -1, lastMin = -1;

uint16_t mix(uint16_t a, uint16_t b, int t) {  // t = 0 … 255 Anteil von b
  int r = ((a >> 11) * (255 - t) + (b >> 11) * t) / 255;
  int g = (((a >> 5) & 63) * (255 - t) + ((b >> 5) & 63) * t) / 255;
  int bl = ((a & 31) * (255 - t) + (b & 31) * t) / 255;
  return (r << 11) | (g << 5) | bl;
}

void put(int x, int y, uint16_t c) { if ((unsigned)x < S && (unsigned)y < S) px[y * S + x] = c; }
void fill(int x, int y, int w, int h, uint16_t c) {
  for (int j = max(0, y); j < min(S, y + h); j++)
    for (int i = max(0, x); i < min(S, x + w); i++) px[j * S + i] = c;
}
// Szene: Blöcke aus 2 × 2 Pixeln (58 × 58 Blöcke)
void cell(int x, int y, uint16_t c) { fill(2 * x, 2 * y, 2, 2, c); }
void box(int x, int y, int w, int h, uint16_t c) { fill(2 * x, 2 * y, 2 * w, 2 * h, c); }
void disc(float cx, float cy, float r, uint16_t c) {
  for (int y = (int)floorf(cy - r); y <= (int)ceilf(cy + r); y++)
    for (int x = (int)floorf(cx - r); x <= (int)ceilf(cx + r); x++) {
      float dx = x + 0.5f - cx, dy = y + 0.5f - cy;
      if (dx * dx + dy * dy <= r * r) cell(x, y, c);
    }
}

struct Pal { char k; uint16_t c; };
void sprite(int x, int y, const char* const* rows, int n, const Pal* pal, int np, int maxRows = 99) {
  for (int j = 0; j < n && j < maxRows; j++)
    for (int i = 0; rows[j][i]; i++) {
      char ch = rows[j][i];
      if (ch == '.') continue;
      for (int k = 0; k < np; k++)
        if (pal[k].k == ch) { cell(x + i, y + j, pal[k].c); break; }
    }
}

// ------------------------------------------------------------------ das Kind

// ein Mädchen: lange Haare mit Schleife, Kleid (12 Blöcke breit, 16 hoch)
const char* const KID[] = {
    "....hhhh....",
    "...hhhhhhqq.",
    "..hhhhhhhhqq",
    ".hhhsssshhh.",
    "hhssessesshh",
    "hhspsssspshh",
    ".hhssmmsshh.",
    "..hhsssshh..",
    "..rrrrrrrr..",
    ".rrrrrrrrrr.",
    ".s.rrrrrr.s.",
    "...rrrrrr...",
    "..rrrrrrrr..",
    ".rrwrrwrrwr.",
    "...ss..ss...",
    "..kkk..kkk..",
};
enum { EYES_OPEN = 0, EYES_SHUT = 1 };

void kid(int x, int y, int eyes, int rows = 16, bool bare = false) {
  const Pal pal[] = {{'h', rgb(0x7a4522)}, {'s', rgb(0xf6c9a4)}, {'p', rgb(0xf3a08e)},
                     {'e', eyes == EYES_SHUT ? rgb(0xa86e50) : rgb(0x2b1a10)}, {'m', rgb(0xc9504a)},
                     {'r', bare ? rgb(0xf6c9a4) : rgb(0xf0609e)}, {'w', bare ? rgb(0xf6c9a4) : WHITE},
                     {'q', rgb(0xffd23f)}, {'k', rgb(0x7a3a6a)}};
  sprite(x - 1, y, KID, 16, pal, 9, rows);  // Mitte wie beim 10 breiten Raster
}

bool blink(int f) { return f % 9 == 0; }

void zzz(int x, int y, int f) {
  // drei „Z“, die nach oben steigen
  static const char* const ZB[] = {"ZZZZ", "..Z.", ".Z..", "ZZZZ"};
  static const char* const ZS[] = {"ZZZ", ".Z.", "ZZZ"};
  const Pal pal[] = {{'Z', rgb(0xdfe7ff)}};
  int k = f % 3;
  sprite(x, y - k, ZS, 3, pal, 1);
  if (k > 0) sprite(x + 3, y - 3 - k, ZB, 4, pal, 1);
}

// ------------------------------------------------------------------ Bauteile

enum Sky { MORNING, NOON, EVENING, NIGHT };

void skyFill(int x, int y, int w, int h, Sky s) {
  uint16_t top = s == MORNING ? rgb(0x9fdcff) : s == NOON ? rgb(0x66c4ff) : s == EVENING ? rgb(0xff9a62) : rgb(0x0d1640);
  uint16_t low = s == MORNING ? rgb(0xffe3a8) : s == NOON ? rgb(0xbfe8ff) : s == EVENING ? rgb(0xffd27a) : rgb(0x1e2b6b);
  for (int j = 0; j < h; j++) box(x, y + j, w, 1, mix(top, low, h > 1 ? j * 255 / (h - 1) : 0));
}

void stars(int x, int y, int w, int h, int f) {
  static const uint8_t P[][2] = {{1, 1}, {7, 2}, {4, 5}, {10, 4}, {2, 8}, {9, 8}, {6, 9}};
  for (int i = 0; i < 7; i++)
    if (P[i][0] < w && P[i][1] < h) cell(x + P[i][0], y + P[i][1], (i + f) % 3 ? rgb(0xfff6c8) : rgb(0x6f7bb8));
}

void window(int x, int y, int w, int h, Sky s, int f, bool curtains = false) {
  uint16_t frame = s == NIGHT ? rgb(0x8890b8) : WHITE;
  box(x - 1, y - 1, w + 2, h + 2, frame);
  skyFill(x, y, w, h, s);
  if (s == NIGHT) {
    stars(x, y, w, h, f);
    disc(x + w - 3.5f, y + 3.5f, 2.2f, rgb(0xfff2b0));  // Mond
    disc(x + w - 2.6f, y + 2.8f, 1.8f, rgb(0x0d1640));
  } else {
    float sy = s == NOON ? y + 2.5f : y + h - 3.0f;
    disc(x + 3.5f, sy, 2.2f, s == EVENING ? rgb(0xff6a3d) : rgb(0xffd23f));
  }
  box(x + w / 2, y, 1, h, frame);  // Fensterkreuz
  box(x, y + h / 2, w, 1, frame);
  if (curtains) {
    uint16_t c = rgb(0x8e6bbf);
    box(x - 1, y - 2, 4, h + 3, c);
    box(x + w - 3, y - 2, 4, h + 3, c);
    box(x + 2, y - 2, w - 4, 2, c);
  }
}

void wallpaper(uint16_t wall, int bottom) {
  box(0, 0, 58, bottom, wall);
  uint16_t dotc = mix(wall, WHITE, 40);
  for (int y = 3; y < bottom - 1; y += 5)
    for (int x = (y / 5) % 2 * 3; x < 58; x += 6) cell(x, y, dotc);
}

void woodFloor(int y, bool dark) {
  uint16_t c = dark ? rgb(0x4b3a3e) : rgb(0xc98d5c), line = mix(c, BLACK, 50);
  box(0, y, 58, 48 - y, c);
  box(0, y, 58, 1, mix(c, BLACK, 80));
  for (int j = y + 3; j < 48; j += 3) box(0, j, 58, 1, line);
  for (int j = y + 1, k = 0; j < 48; j += 3, k++)
    for (int x = (k % 2) * 5; x < 58; x += 10) box(x, j, 1, 2, line);
}

// Bett rechts: Kopfteil, Matratze, Decke. who: 0 leer, 1 schläft, 2 sitzt mit Buch
void bed(int who, int f, bool dark) {
  uint16_t wood = dark ? rgb(0x6b4a33) : rgb(0x9b6a3f), blanket = dark ? rgb(0x3f6fae) : rgb(0x5aa9e6);
  box(47, 26, 3, 19, wood);       // Kopfteil
  box(47, 25, 3, 1, mix(wood, WHITE, 60));
  box(16, 39, 32, 4, wood);       // Rahmen
  box(17, 43, 2, 2, wood);        // Füße
  box(45, 43, 2, 2, wood);
  box(37, 31, 10, 5, dark ? rgb(0xb8bfd6) : WHITE);  // Kopfkissen
  if (who == 1) {                 // schläft: nur der Kopf schaut heraus
    kid(37, 27, EYES_SHUT, 8);
    zzz(42, 22, f);
  } else if (who == 2) {          // sitzt und liest
    kid(36, 22, blink(f) ? EYES_SHUT : EYES_OPEN, 13);
    box(36, 31, 7, 4, rgb(0xd94c4c));  // Buch
    box(37, 31, 2, 3, WHITE);
    box(40, 31, 2, 3, WHITE);
    box(39, 31, 1, 4, rgb(0xa83232));
  }
  // Decke mit Karos
  uint16_t b2 = mix(blanket, WHITE, 70);
  int top = who == 0 ? 37 : 35;
  box(16, top, who == 0 ? 20 : 31, 39 - top, blanket);
  if (who == 0) box(36, 37, 11, 2, mix(blanket, WHITE, 40));  // zurückgeschlagen
  for (int y = top + 1; y < 39; y += 2)
    for (int x = 17 + (y % 4 == 0); x < (who == 0 ? 36 : 47); x += 3) cell(x, y, b2);
}

void glow(int cx, int cy, int r, uint32_t col, int strength) {  // in Pixeln
  uint16_t c = rgb(col);
  for (int y = max(0, cy - r); y < min(S, cy + r); y++)
    for (int x = max(0, cx - r); x < min(S, cx + r); x++) {
      float d = sqrtf((x - cx) * (x - cx) + (y - cy) * (y - cy));
      if (d < r) px[y * S + x] = mix(px[y * S + x], c, (int)(strength * (1 - d / r)));
    }
}

// ------------------------------------------------------------------ Szenen

void sceneBedroom(kids::Act a, int f) {
  bool night = a == kids::SLEEP || a == kids::STORY, nap = a == kids::NAP;
  wallpaper(night ? (a == kids::STORY ? rgb(0x3d3558) : rgb(0x1f2547)) : nap ? rgb(0xdcc79e) : rgb(0xf7e2b5), 40);
  woodFloor(40, night);
  window(8, 15, 12, 11, night ? NIGHT : MORNING, f, nap);
  if (a == kids::STORY) {  // Nachttischlampe
    box(7, 36, 7, 4, rgb(0x7a5537));
    box(10, 32, 1, 4, rgb(0x5a4030));
    box(8, 29, 5, 3, rgb(0xffd46b));
  }
  if (a == kids::WAKE) {
    bed(0, f, false);
    kid(24, 26, blink(f) ? EYES_SHUT : EYES_OPEN);
    // Zahnbürste vor dem Mund, putzt hoch und runter
    int o = f % 2;
    box(34, 31 + o, 1, 5, rgb(0x2fa3e0));
    box(33, 31 + o, 1, 2, WHITE);
  } else {
    bed(a == kids::STORY ? 2 : 1, f, night);
  }
  if (a == kids::STORY) glow(21, 61, 34, 0xffc86b, 110);
}

void sceneKitchen(kids::Act a, int f) {
  Sky s = a == kids::BREAKFAST ? MORNING : a == kids::LUNCH ? NOON : EVENING;
  wallpaper(a == kids::DINNER ? rgb(0xeab889) : rgb(0xfde9c9), 42);
  for (int x = 0; x < 58; x++) cell(x, 28, (x / 2) % 2 ? rgb(0x6fb6e8) : WHITE);  // Kachelborte
  window(8, 14, 12, 10, s, f);
  for (int y = 42; y < 48; y++)
    for (int x = 0; x < 58; x++) cell(x, y, ((x / 2) + (y / 2)) % 2 ? rgb(0xd9d9d9) : rgb(0xb9b9b9));
  kid(24, 22, blink(f) ? EYES_SHUT : EYES_OPEN, 14);
  // Tisch mit karierter Decke
  box(13, 34, 34, 2, rgb(0xa45a32));
  for (int y = 36; y < 40; y++)
    for (int x = 13; x < 47; x++) cell(x, y, ((x / 2) + (y / 2)) % 2 ? rgb(0xe8453c) : WHITE);
  box(15, 40, 2, 4, rgb(0x7a4026));
  box(43, 40, 2, 4, rgb(0x7a4026));
  if (a == kids::BREAKFAST) {  // Müsli und Milch
    box(25, 32, 6, 2, rgb(0x3c78d8));
    box(26, 31, 4, 1, rgb(0xf3c34a));
    box(34, 29, 2, 5, WHITE);
  } else if (a == kids::LUNCH) {  // Teller mit Kartoffeln, Möhren, Erbsen
    box(23, 33, 10, 1, WHITE);
    box(24, 32, 3, 1, rgb(0xf2d06b));
    box(27, 32, 2, 1, rgb(0xff8a2a));
    box(29, 32, 2, 1, rgb(0x5cc04a));
    box(34, 30, 1, 4, rgb(0xb0b0b0));
  } else {  // Brot und Tee
    box(24, 31, 5, 3, rgb(0xd9a066));
    box(25, 32, 3, 1, rgb(0xf3d29a));
    box(32, 31, 3, 3, rgb(0xf4f4f4));
    box(35, 32, 1, 1, rgb(0xf4f4f4));
  }
  // Dampf vom Mittagessen bzw. Tee
  if (a != kids::BREAKFAST) {
    int sx = a == kids::DINNER ? 34 : 21;
    for (int k = 0; k < 3; k++) cell(sx + ((k + f) % 2), 29 - 2 * k, rgb(0xfffaf0));
  }
}

void cloud(int x, int y) {
  box(x + 1, y, 4, 1, WHITE);
  box(x, y + 1, 7, 2, WHITE);
  box(x + 5, y, 2, 1, WHITE);
}

void outdoorBase(int f) {
  skyFill(0, 0, 58, 40, NOON);
  int drift = (f / 2) % 70;
  cloud(48 - drift, 17);
  cloud((70 + 20 - drift) % 70 - 10, 24);
  disc(11.5f, 19.5f, 3.2f, rgb(0xffd23f));
  box(0, 39, 58, 9, rgb(0x5cbf4a));
  box(0, 39, 58, 1, rgb(0x7fd668));
  for (int x = 2; x < 58; x += 5) cell(x, 42 + x % 3, rgb(0x46a03a));
}

void sceneDaycare(int f) {
  outdoorBase(f);
  // Haus mit rotem Dach
  uint16_t roof = rgb(0xd9534f);
  for (int j = 0; j < 6; j++) box(39 - j * 2 + 1, 17 + j, 4 * j + 2, 1, roof);
  box(30, 23, 20, 17, rgb(0xffdf8a));
  box(38, 32, 5, 8, rgb(0x8a5a33));
  cell(42, 36, rgb(0xffd23f));
  box(32, 26, 4, 4, rgb(0x9fdcff));
  box(45, 26, 4, 4, rgb(0x9fdcff));
  // Kind mit Ranzen, wippt beim Gehen
  int b = f % 2;
  box(13, 33 + b, 12, 5, rgb(0xffb020));
  kid(14, 25 + b, blink(f) ? EYES_SHUT : EYES_OPEN);
  box(16, 33 + b, 1, 3, rgb(0x8a5a10));
  box(21, 33 + b, 1, 3, rgb(0x8a5a10));
}

void scenePlay(int f) {
  outdoorBase(f);
  // Baum
  box(44, 29, 3, 11, rgb(0x8a5a33));
  disc(45.5f, 24.5f, 7.2f, rgb(0x3fa34d));
  disc(43.0f, 22.0f, 2.5f, rgb(0x5cc45f));
  cell(48, 26, rgb(0xe74c3c));
  cell(42, 27, rgb(0xe74c3c));
  // Kind und hüpfender Ball
  static const int8_t H[] = {0, 3, 5, 6, 5, 3};
  int h = H[f % 6];
  kid(14, 24, blink(f) ? EYES_SHUT : EYES_OPEN);
  disc(31.0f, 37.0f - h, 2.7f, rgb(0xf05a4a));
  box(30, 36 - h, 2, 1, WHITE);
  box(29, 41, 4, 1, rgb(0x46a03a));  // Schatten
}

void sceneBath(int f) {
  for (int y = 0; y < 42; y++)
    for (int x = 0; x < 58; x++) cell(x, y, ((x / 3) + (y / 3)) % 2 ? rgb(0xbfe6f5) : rgb(0xa6d8ee));
  box(0, 42, 58, 6, rgb(0xe2e2e2));
  kid(24, 21, blink(f) ? EYES_SHUT : EYES_OPEN, 12, true);
  // Wanne
  box(10, 31, 38, 2, rgb(0xffffff));
  box(11, 33, 36, 8, rgb(0xf4f4f4));
  box(11, 40, 36, 1, rgb(0xcfcfcf));
  box(13, 41, 2, 2, rgb(0xb0b0b0));
  box(43, 41, 2, 2, rgb(0xb0b0b0));
  // Schaum und aufsteigende Blasen
  for (int x = 12; x < 46; x += 3) disc(x + 0.5f, 31.0f, 1.6f, WHITE);
  for (int k = 0; k < 3; k++) {
    int y = 28 - ((f + k * 2) % 6) * 2;
    disc(17.5f + k * 9 + (k == 1) * 14, y + 0.5f, 0.9f, rgb(0xeefaff));
  }
  // Quietscheente
  box(38, 28, 4, 3, rgb(0xffd23f));
  box(40, 26, 2, 2, rgb(0xffd23f));
  cell(42, 27, rgb(0xff8a2a));
  cell(40, 26, rgb(0x2b1a10));
}

void scene(kids::Act a, bool weekend, int f) {
  switch (a) {
    case kids::BREAKFAST: case kids::LUNCH: case kids::DINNER: sceneKitchen(a, f); break;
    case kids::DAYCARE: if (weekend) scenePlay(f); else sceneDaycare(f); break;
    case kids::PLAY: scenePlay(f); break;
    case kids::BATH: sceneBath(f); break;
    default: sceneBedroom(a, f); break;
  }
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

  fill(0, 0, S, S, BLACK);
  scene(act, weekend, f);
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
  const char* name = awake ? kids::slotName(sl[k], weekend) : kids::actName(act, weekend);
  if (strcmp(lv_label_get_text(actL), name)) {
    lv_obj_set_style_text_font(actL, strlen(name) > 14 ? &font_text_20 : &font_text_24, 0);
    lv_label_set_text(actL, name);
  }
  lv_obj_set_style_text_color(actL, lv_color_hex(0xffffff), 0);
  int until = awake ? (k + 1 < n ? sl[k + 1].start : sl[n - 1].start) : sl[0].start;
  char nb[24];
  snprintf(nb, sizeof(nb), "bis %d:%02d", until / 60, until % 60);
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
