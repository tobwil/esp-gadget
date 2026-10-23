#include "kidart.h"
#include <math.h>
#include <string.h>
#include <algorithm>

namespace kidart {

using kids::Act;
using std::max;
using std::min;

namespace {

constexpr uint16_t BLACK = 0, WHITE = 0xFFFF;

uint16_t* px = nullptr;
World W = HOME;          // Welt der gerade gezeichneten Szene
const Scene* cur = nullptr;

void put(int x, int y, uint16_t c) { if ((unsigned)x < S && (unsigned)y < S) px[y * S + x] = c; }
void fill(int x, int y, int w, int h, uint16_t c) {
  for (int j = max(0, y); j < min(S, y + h); j++)
    for (int i = max(0, x); i < min(S, x + w); i++) px[j * S + i] = c;
}
// Szene: Blöcke aus 2 × 2 Pixeln (58 × 48 Blöcke)
void cell(int x, int y, uint16_t c) { fill(2 * x, 2 * y, 2, 2, c); }
void box(int x, int y, int w, int h, uint16_t c) { fill(2 * x, 2 * y, 2 * w, 2 * h, c); }
uint16_t at(int x, int y) { return (unsigned)(2 * x) < S && (unsigned)(2 * y) < S ? px[2 * y * S + 2 * x] : BLACK; }
void tint(int x, int y, uint16_t c, int t) { if ((unsigned)(2 * x) < S && (unsigned)(2 * y) < S) cell(x, y, mix(at(x, y), c, t)); }
void disc(float cx, float cy, float r, uint16_t c) {
  for (int y = (int)floorf(cy - r); y <= (int)ceilf(cy + r); y++)
    for (int x = (int)floorf(cx - r); x <= (int)ceilf(cx + r); x++) {
      float dx = x + 0.5f - cx, dy = y + 0.5f - cy;
      if (dx * dx + dy * dy <= r * r) cell(x, y, c);
    }
}
void ringBlocks(float cx, float cy, float r0, float r1, uint16_t c, bool topOnly = false) {
  for (int y = (int)floorf(cy - r1); y <= (int)ceilf(cy + r1); y++)
    for (int x = (int)floorf(cx - r1); x <= (int)ceilf(cx + r1); x++) {
      float dx = x + 0.5f - cx, dy = y + 0.5f - cy, d = sqrtf(dx * dx + dy * dy);
      if (d >= r0 && d <= r1 && (!topOnly || dy <= 0)) cell(x, y, c);
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

void glow(int cx, int cy, int r, uint32_t col, int strength) {  // in Pixeln
  uint16_t c = rgb(col);
  for (int y = max(0, cy - r); y < min(S, cy + r); y++)
    for (int x = max(0, cx - r); x < min(S, cx + r); x++) {
      float d = sqrtf((float)((x - cx) * (x - cx) + (y - cy) * (y - cy)));
      if (d < r) px[y * S + x] = mix(px[y * S + x], c, (int)(strength * (1 - d / r)));
    }
}

bool blink(int f) { return f % 9 == 0; }

// ------------------------------------------------------------------ das Kind und seine Kleidung je Welt

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
// Unterwasser: Meerjungfrauen-Schwanz statt Rock und Beinen (ersetzt die Zeilen 10 … 15)
const char* const TAIL[] = {
    ".s.tttttt.s.",
    "...tttttt...",
    "....tttt....",
    ".....tt.....",
    "...ffttff...",
    "..ff....ff..",
};
enum { EYES_OPEN = 0, EYES_SHUT = 1 };

struct Outfit { uint32_t dress, trim, shoes, bow; };
Outfit outfit() {
  switch (W) {
    case SPACE: return {0xe9eef6, 0x3d7be0, 0x6b7280, 0xffd23f};  // Raumanzug
    case FARM: return {0x3f73c4, 0xe8453c, 0x6b4226, 0xffd23f};   // Latzhose
    case SEA: return {0xb38cf0, 0xffffff, 0x1fb5a8, 0xff8fab};    // Muschel-Top, Schwanz in shoes
    case FAIRY: return {0xc39bff, 0xfff1a8, 0xff8fc8, 0x7a4522};  // Kleid, Krone statt Schleife
    default: return {0xf0609e, 0xffffff, 0x7a3a6a, 0xffd23f};
  }
}

void hat(int x, int y) {  // Strohhut (Bauernhof)
  uint16_t st = rgb(0xe8c35a), band = rgb(0xe8453c);
  box(x + 2, y - 1, 7, 2, st);
  box(x + 2, y + 1, 7, 1, band);
  box(x - 1, y + 2, 13, 1, st);
}

void crown(int x, int y) {  // Krone (Märchenland)
  uint16_t g = rgb(0xffd23f);
  box(x + 3, y - 1, 5, 2, g);
  cell(x + 3, y - 2, g);
  cell(x + 5, y - 2, g);
  cell(x + 7, y - 2, g);
  cell(x + 5, y - 1, rgb(0xe8453c));
}

void helmet(int x, int y) {  // Glashelm (Weltraum, draußen)
  float cx = x + 5.0f, cy = y + 4.5f;
  for (int j = (int)(cy - 7); j <= (int)(cy + 7); j++)
    for (int i = (int)(cx - 7); i <= (int)(cx + 7); i++) {
      float dx = i + 0.5f - cx, dy = j + 0.5f - cy, d = sqrtf(dx * dx + dy * dy);
      if (d > 6.0f && d <= 7.0f) cell(i, j, rgb(0xdfe9ff));
      else if (d <= 6.0f && dx < -2 && dy < -2 && d > 4.5f) tint(i, j, WHITE, 120);  // Glanz
    }
}

void kid(int x, int y, int eyes, int rows = 16, bool bare = false, bool outdoors = false) {
  Outfit o = outfit();
  bool tail = W == SEA && !bare;
  const Pal pal[] = {{'h', rgb(0x7a4522)}, {'s', rgb(0xf6c9a4)}, {'p', rgb(0xf3a08e)},
                     {'e', eyes == EYES_SHUT ? rgb(0xa86e50) : rgb(0x2b1a10)}, {'m', rgb(0xc9504a)},
                     {'r', bare ? rgb(0xf6c9a4) : rgb(o.dress)}, {'w', bare ? rgb(0xf6c9a4) : rgb(o.trim)},
                     {'q', rgb(o.bow)}, {'k', rgb(o.shoes)}, {'t', rgb(o.shoes)}, {'f', mix(rgb(o.shoes), WHITE, 90)}};
  sprite(x - 1, y, KID, tail ? 10 : 16, pal, 11, rows);
  if (tail && rows > 10) sprite(x - 1, y + 10, TAIL, 6, pal, 11, rows - 10);
  if (W == FARM && rows >= 8 && !bare) hat(x - 1, y);
  if (W == FAIRY && rows >= 8) crown(x - 1, y);
  if (W == SPACE && outdoors) helmet(x - 1, y);
}

void zzz(int x, int y, int f) {
  // drei „Z“, die nach oben steigen
  static const char* const ZB[] = {"ZZZZ", "..Z.", ".Z..", "ZZZZ"};
  static const char* const ZS[] = {"ZZZ", ".Z.", "ZZZ"};
  const Pal pal[] = {{'Z', rgb(0xdfe7ff)}};
  int k = f % 3;
  sprite(x, y - k, ZS, 3, pal, 1);
  if (k > 0) sprite(x + 3, y - 3 - k, ZB, 4, pal, 1);
}

// ------------------------------------------------------------------ der Tier-Begleiter

const char* const CAT_PX[] = {"o.....o", "oo...oo", "ooooooo", "oeoooeo", "owwnwwo", ".ooooo.", ".d...d."};
const char* const BUNNY_PX[] = {".w...w.", ".wp.pw.", ".wp.pw.", ".wwwww.", "wewwwew", "wwwnwww", ".wwwww.", ".w...w."};
const char* const DOG_PX[] = {"dbbbbbd", "dbbbbbd", "dbebebd", ".bbnbb.", ".bbtbb.", ".bbbbb.", ".b...b."};
const char* const DRAGON_PX[] = {".y...y.", ".ggggg.", "ggegegg", "aglllga", "agllla.", ".ggggg.", ".g...g."};

enum PetPose { SIT, SLEEPING };

void heart(int x, int y, uint16_t c) {
  cell(x, y, c);
  cell(x + 2, y, c);
  box(x, y + 1, 3, 1, c);
  cell(x + 1, y + 2, c);
}

// Tier mit 7 Blöcken Breite; (x, y) = links oben
void pet(int x, int y, PetPose pose) {
  if (!cur || cur->pet == NO_PET) return;
  int f = cur->frame;
  bool sleeping = pose == SLEEPING;
  uint16_t eye = rgb(0x2b1a10);
  const char* const* rows = CAT_PX;
  int n = 7;
  uint16_t body = 0, dark = 0;
  switch (cur->pet) {
    case BUNNY: {
      rows = BUNNY_PX;
      n = 8;
      if (!sleeping && f % 4 == 0) y -= 1;  // hoppelt
      body = rgb(0xf4f1ee);
      dark = rgb(0xd8d2cc);
      const Pal pal[] = {{'w', body}, {'p', rgb(0xf3a0b5)}, {'e', sleeping ? dark : eye}, {'n', rgb(0xff8fa0)}};
      sprite(x, y - 1, rows, n, pal, 4);
      y -= 1;
      break;
    }
    case DOG: {
      rows = DOG_PX;
      body = rgb(0xc08a52);
      dark = rgb(0x7a4f2a);
      const Pal pal[] = {{'b', body}, {'d', dark}, {'e', sleeping ? dark : eye}, {'n', eye},
                         {'t', sleeping ? body : rgb(0xff7a8a)}};
      sprite(x, y, rows, n, pal, 5);
      if (!sleeping) cell(x + 7, y + 3 + (f % 2), dark);  // wedelt
      break;
    }
    case DRAGON: {
      rows = DRAGON_PX;
      body = rgb(0x4cc46a);
      dark = rgb(0x2e8a46);
      uint16_t wing = rgb(0xa06cd5);
      const Pal pal[] = {{'g', body}, {'l', rgb(0xc8f5b0)}, {'y', rgb(0xffd23f)}, {'e', sleeping ? dark : eye},
                         {'a', (f % 2 && !sleeping) ? body : wing}};
      sprite(x, y, rows, n, pal, 5);
      if (!sleeping && f % 2) { cell(x - 1, y + 2, wing); cell(x + 7, y + 2, wing); }  // Flügelschlag
      break;
    }
    default: {
      body = rgb(0xe8902e);
      dark = rgb(0xb5651a);
      const Pal pal[] = {{'o', body}, {'d', dark}, {'e', sleeping ? dark : eye}, {'w', rgb(0xfff1e0)},
                         {'n', rgb(0xff8fa0)}};
      sprite(x, y, rows, n, pal, 5);
      if (!sleeping) { cell(x + 7, y + 4 - (f % 2), dark); cell(x + 8, y + 3 - (f % 2), dark); }  // Schwanz
      break;
    }
  }
  if (cur->petLevel >= 2) {  // Halstuch
    uint16_t red = rgb(0xe8453c);
    box(x + 1, y + 5, 5, 1, red);
    cell(x + 4, y + 6, red);
  }
  if (cur->petLevel >= 3) {  // Krone
    uint16_t g = rgb(0xffd23f);
    box(x + 2, y - 1, 3, 1, g);
    cell(x + 2, y - 2, g);
    cell(x + 4, y - 2, g);
  }
  if (sleeping) zzz(x + 6, y - 1, f);
  else if (cur->petHappy) heart(x + 6, y - 4 - (f % 3), rgb(0xff5c8a));
}

// ------------------------------------------------------------------ Himmel, Fenster, Wände, Böden

enum Sky { MORNING, NOON, EVENING, NIGHT };

void skyFill(int x, int y, int w, int h, Sky s) {
  uint16_t top = s == MORNING ? rgb(0x9fdcff) : s == NOON ? rgb(0x66c4ff) : s == EVENING ? rgb(0xff9a62) : rgb(0x0d1640);
  uint16_t low = s == MORNING ? rgb(0xffe3a8) : s == NOON ? rgb(0xbfe8ff) : s == EVENING ? rgb(0xffd27a) : rgb(0x1e2b6b);
  for (int j = 0; j < h; j++) box(x, y + j, w, 1, mix(top, low, h > 1 ? j * 255 / (h - 1) : 0));
}

void stars(int x, int y, int w, int h, int f) {
  static const uint8_t P[][2] = {{1, 1}, {7, 2}, {4, 5}, {10, 4}, {2, 8}, {9, 8}, {6, 9}, {13, 1}, {15, 6}, {12, 10}};
  for (int i = 0; i < 10; i++)
    if (P[i][0] < w && P[i][1] < h) cell(x + P[i][0], y + P[i][1], (i + f) % 3 ? rgb(0xfff6c8) : rgb(0x6f7bb8));
}

void starfield(int x0, int y0, int w, int h, int f) {  // über eine größere Fläche
  for (int y = y0; y < y0 + h; y += 11)
    for (int x = x0; x < x0 + w; x += 16) stars(x, y, std::min(16, x0 + w - x), std::min(11, y0 + h - y), f + x + y);
}

void moon(float x, float y, uint16_t bg) {
  disc(x, y, 2.2f, rgb(0xfff2b0));
  disc(x + 0.9f, y - 0.7f, 1.8f, bg);
}

void fish(int x, int y, uint16_t c, bool left) {
  box(x, y, 3, 2, c);
  cell(left ? x + 3 : x - 1, y, c);
  cell(left ? x + 3 : x - 1, y + 1, c);
  cell(left ? x : x + 2, y, BLACK);
}

void cloud(int x, int y, uint16_t c = WHITE) {
  box(x + 1, y, 4, 1, c);
  box(x, y + 1, 7, 2, c);
  box(x + 5, y, 2, 1, c);
}

// Fenster bzw. Bullauge mit dem Blick nach draußen
void view(int x, int y, int w, int h, Sky s, int f, bool curtains = false) {
  bool night = s == NIGHT;
  switch (W) {
    case SPACE: {  // Bullauge mit Erde oder Mond
      float cx = x + w / 2.0f, cy = y + h / 2.0f, r = std::min(w, h) / 2.0f + 0.5f;
      disc(cx, cy, r + 1.2f, rgb(0x8f99ad));
      disc(cx, cy, r, rgb(0x050816));
      for (int j = y; j < y + h; j++)
        for (int i = x; i < x + w; i++)
          if ((i * 7 + j * 3 + f / 3) % 17 == 0 && hypotf(i + 0.5f - cx, j + 0.5f - cy) < r - 0.5f) cell(i, j, rgb(0xfff6c8));
      if (night) moon(cx + 1.5f, cy - 1.0f, rgb(0x050816));
      else {
        disc(cx + 1.0f, cy + 1.0f, r * 0.55f, rgb(0x2f7fe0));
        disc(cx + 0.2f, cy + 0.6f, r * 0.22f, rgb(0x4cc46a));
        cell((int)(cx + 2), (int)(cy + 2), rgb(0x4cc46a));
      }
      break;
    }
    case SEA: {  // rundes Bullauge mit Fischen
      float cx = x + w / 2.0f, cy = y + h / 2.0f, r = std::min(w, h) / 2.0f + 0.5f;
      disc(cx, cy, r + 1.2f, rgb(0xc7963f));
      disc(cx, cy, r, night ? rgb(0x0a2342) : rgb(0x5fd0f0));
      int fx = (int)(cx - r) + (f % (int)(2 * r + 4)) - 2;
      if (hypotf(fx + 1.5f - cx, cy - 1 - cy) < r - 1) fish(fx, (int)cy - 1, night ? rgb(0x4a6fa5) : rgb(0xff9a3c), false);
      if (!night) cell((int)cx - 2, (int)cy + 2 - (f % 3), rgb(0xe6fbff));
      break;
    }
    case FAIRY: {  // Bogenfenster mit Schlossturm
      uint16_t frame = night ? rgb(0x9a86c8) : rgb(0xfff4ff);
      box(x - 1, y, w + 2, h + 1, frame);
      disc(x + w / 2.0f, y + 0.5f, w / 2.0f + 1, frame);
      skyFill(x, y, w, h, s);
      for (int j = 0; j < w / 2 + 1; j++)
        for (int i = 0; i < w; i++) {
          float dx = i + 0.5f - w / 2.0f, dy = -j - 0.5f;
          if (dx * dx + dy * dy <= (w / 2.0f) * (w / 2.0f)) cell(x + i, y - j, mix(night ? rgb(0x0d1640) : rgb(0x9fdcff), WHITE, 0));
        }
      if (night) { stars(x, y - 3, w, h + 3, f); moon(x + w - 3.5f, y + 1.5f, rgb(0x0d1640)); }
      // Turm in der Ferne
      uint16_t tw = night ? rgb(0x3b3366) : rgb(0xd7b8f0);
      box(x + 3, y + h - 6, 3, 6, tw);
      cell(x + 4, y + h - 8, night ? rgb(0x6b5aa8) : rgb(0xff8fc8));
      box(x + 3, y + h - 7, 3, 1, night ? rgb(0x6b5aa8) : rgb(0xff8fc8));
      box(x + w / 2, y - w / 2, 1, h + w / 2, frame);
      break;
    }
    default: {  // Fenster (Zuhause, Bauernhof mit Hügeln)
      uint16_t frame = night ? rgb(0x8890b8) : WHITE;
      box(x - 1, y - 1, w + 2, h + 2, frame);
      skyFill(x, y, w, h, s);
      if (night) {
        stars(x, y, w, h, f);
        moon(x + w - 3.5f, y + 3.5f, rgb(0x0d1640));
      } else {
        float sy = s == NOON ? y + 2.5f : y + h - 3.0f;
        disc(x + 3.5f, sy, 2.2f, s == EVENING ? rgb(0xff6a3d) : rgb(0xffd23f));
      }
      if (W == FARM) {  // Felder und ein Baum
        uint16_t g = night ? rgb(0x1f3b2a) : rgb(0x6cc057);
        for (int i = 0; i < w; i++) box(x + i, y + h - 3 + (i % 5 == 0), 1, 3 - (i % 5 == 0), g);
        if (!night) cell(x + w - 3, y + h - 4, rgb(0x2f8a3c));
      }
      box(x + w / 2, y, 1, h, frame);  // Fensterkreuz
      box(x, y + h / 2, w, 1, frame);
      break;
    }
  }
  if (curtains) {
    uint16_t c = W == FAIRY ? rgb(0xff8fc8) : W == SPACE ? rgb(0x2f8fd8) : rgb(0x8e6bbf);
    box(x - 1, y - 2, 4, h + 3, c);
    box(x + w - 3, y - 2, 4, h + 3, c);
  }
}

void wall(int bottom, bool night, bool nap, bool story, int f) {
  switch (W) {
    case SPACE: {  // Metallplatten mit Nieten und Leuchtstreifen
      uint16_t c = night ? rgb(0x161d33) : nap ? rgb(0x33405c) : rgb(0x3a4a6b);
      box(0, 0, 58, bottom, c);
      uint16_t l = mix(c, BLACK, 70), r = mix(c, WHITE, 60);
      for (int x = 0; x < 58; x += 10) box(x, 0, 1, bottom, l);
      for (int x = 4; x < 58; x += 10)
        for (int y = 4; y < bottom - 6; y += 8) cell(x, y, r);
      box(0, bottom - 5, 58, 1, night ? rgb(0x1c4a7a) : rgb(0x2f8fd8));
      if (!night) cell((f * 3) % 58, bottom - 5, rgb(0x9fe6ff));
      break;
    }
    case FARM: {  // Holzbretter
      uint16_t c = night ? rgb(0x4a3426) : nap ? rgb(0xbb8a55) : rgb(0xd49c62);
      box(0, 0, 58, bottom, c);
      uint16_t l = mix(c, BLACK, 60);
      for (int x = 3; x < 58; x += 5) box(x, 0, 1, bottom, l);
      for (int x = 1; x < 58; x += 10) cell(x, (x * 7) % (bottom - 4) + 2, l);
      break;
    }
    case SEA: {  // Wasser mit Lichtstrahlen
      uint16_t top = night ? rgb(0x0b2d52) : rgb(0x4cc3e0), low = night ? rgb(0x061a33) : rgb(0x1f86b8);
      for (int j = 0; j < bottom; j++) box(0, j, 58, 1, mix(top, low, j * 255 / std::max(1, bottom - 1)));
      if (!night)
        for (int k = 0; k < 3; k++)
          for (int j = 0; j < bottom; j++) tint(8 + k * 17 + j / 3, j, WHITE, 26);
      for (int k = 0; k < 4; k++) cell(5 + k * 14, bottom - 3 - ((f + k * 5) % (bottom - 4)), rgb(0xcff6ff));
      break;
    }
    case FAIRY: {  // Steinmauer in Rosa und Lila
      uint16_t c = night ? rgb(0x3a2850) : story ? rgb(0x4a3366) : nap ? rgb(0xe2c6e8) : rgb(0xf3def7);
      box(0, 0, 58, bottom, c);
      uint16_t l = mix(c, night ? WHITE : BLACK, 30);
      for (int y = 0; y < bottom; y += 4) {
        box(0, y, 58, 1, l);
        for (int x = (y / 4) % 2 * 4; x < 58; x += 8) box(x, y, 1, 4, l);
      }
      break;
    }
    default: {  // Tapete mit Pünktchen
      uint16_t c = night ? (story ? rgb(0x3d3558) : rgb(0x1f2547)) : nap ? rgb(0xdcc79e) : rgb(0xf7e2b5);
      box(0, 0, 58, bottom, c);
      uint16_t dotc = mix(c, WHITE, 40);
      for (int y = 3; y < bottom - 1; y += 5)
        for (int x = (y / 5) % 2 * 3; x < 58; x += 6) cell(x, y, dotc);
      break;
    }
  }
}

void floorAt(int y, bool dark) {
  switch (W) {
    case SPACE: {
      uint16_t c = dark ? rgb(0x2a3142) : rgb(0x5d6984), l = mix(c, BLACK, 60);
      box(0, y, 58, 48 - y, c);
      for (int j = y; j < 48; j += 3) box(0, j, 58, 1, l);
      for (int x = 0; x < 58; x += 4) box(x, y, 1, 48 - y, l);
      break;
    }
    case FARM: {  // Stroh
      uint16_t c = dark ? rgb(0x6b5a2e) : rgb(0xe0c066), l = mix(c, BLACK, 50), h = mix(c, WHITE, 60);
      box(0, y, 58, 48 - y, c);
      for (int j = y + 1; j < 48; j += 2)
        for (int x = (j * 5) % 7; x < 58; x += 7) box(x, j, 2, 1, (x + j) % 3 ? l : h);
      break;
    }
    case SEA: {  // Sand mit Muscheln und Seestern
      uint16_t c = dark ? rgb(0x5a5236) : rgb(0xead59c), l = mix(c, BLACK, 40);
      box(0, y, 58, 48 - y, c);
      for (int j = y + 1; j < 48; j += 2)
        for (int x = (j * 3) % 5; x < 58; x += 9) cell(x, j, l);
      uint16_t star = dark ? rgb(0x8a4a4a) : rgb(0xff7a5c);
      cell(6, y + 3, star); cell(5, y + 4, star); cell(7, y + 4, star); cell(6, y + 4, star); cell(6, y + 5, star);
      box(50, y + 4, 2, 1, dark ? rgb(0x8a7f6a) : rgb(0xfff4ec));
      break;
    }
    case FAIRY: {  // Teppich mit Goldkante
      uint16_t c = dark ? rgb(0x5c2747) : rgb(0xc8467a), g = dark ? rgb(0x9a7a2a) : rgb(0xffd23f);
      box(0, y, 58, 48 - y, c);
      box(0, y, 58, 1, g);
      for (int j = y + 3; j < 48; j += 3)
        for (int x = (j % 2) * 3; x < 58; x += 6) cell(x, j, mix(c, WHITE, 50));
      break;
    }
    default: {  // Holzdielen
      uint16_t c = dark ? rgb(0x4b3a3e) : rgb(0xc98d5c), line = mix(c, BLACK, 50);
      box(0, y, 58, 48 - y, c);
      box(0, y, 58, 1, mix(c, BLACK, 80));
      for (int j = y + 3; j < 48; j += 3) box(0, j, 58, 1, line);
      for (int j = y + 1, k = 0; j < 48; j += 3, k++)
        for (int x = (k % 2) * 5; x < 58; x += 10) box(x, j, 1, 2, line);
      break;
    }
  }
}

// ------------------------------------------------------------------ Bett

// Bett rechts: Kopfteil, Matratze, Decke. who: 0 leer, 1 schläft, 2 sitzt mit Buch
void bed(int who, int f, bool dark) {
  uint32_t woodC = 0x9b6a3f, blanketC = 0x5aa9e6;
  switch (W) {
    case SPACE: woodC = 0xb8c2d6; blanketC = 0x3d7be0; break;
    case FARM: woodC = 0x8b5a2b; blanketC = 0xe8453c; break;
    case SEA: woodC = 0xf3a5b5; blanketC = 0x2fb39a; break;
    case FAIRY: woodC = 0xf0a8d0; blanketC = 0xb48cff; break;
    default: break;
  }
  uint16_t wood = dark ? mix(rgb(woodC), BLACK, 90) : rgb(woodC), blanket = dark ? mix(rgb(blanketC), BLACK, 70) : rgb(blanketC);
  if (W == SEA) {  // Kopfteil als große Muschel
    disc(48.5f, 33.0f, 8.5f, wood);
    for (int k = -3; k <= 3; k++)
      for (int r = 2; r < 8; r++) cell(48 + (int)(k * r / 4.0f), 33 - r + abs(k) / 2, mix(wood, WHITE, 60));
  } else if (W == FAIRY) {  // Himmelbett
    box(15, 16, 2, 27, wood);
    box(48, 16, 2, 27, wood);
    box(15, 15, 35, 2, wood);
    uint16_t drape = dark ? rgb(0x7a4a8a) : rgb(0xffc4e6);
    for (int x = 15; x < 50; x++) cell(x, 17 + (x % 4 == 0), drape);
  }
  box(47, 26, 3, 19, wood);  // Kopfteil
  box(47, 25, 3, 1, mix(wood, WHITE, 60));
  if (W == SPACE) cell(48, 28, (f % 2) ? rgb(0x4cd060) : rgb(0xff453a));  // Lämpchen
  box(16, 39, 32, 4, wood);  // Rahmen
  box(17, 43, 2, 2, wood);   // Füße
  box(45, 43, 2, 2, wood);
  box(37, 31, 10, 5, dark ? rgb(0xb8bfd6) : WHITE);  // Kopfkissen
  if (who == 1) {  // schläft: nur der Kopf schaut heraus
    kid(37, 27, EYES_SHUT, 8);
    zzz(42, 22, f);
  } else if (who == 2) {  // sitzt und liest
    kid(36, 22, blink(f) ? EYES_SHUT : EYES_OPEN, 13);
    box(36, 31, 7, 4, rgb(0xd94c4c));  // Buch
    box(37, 31, 2, 3, WHITE);
    box(40, 31, 2, 3, WHITE);
    box(39, 31, 1, 4, rgb(0xa83232));
  }
  // Decke mit Muster
  uint16_t b2 = mix(blanket, WHITE, 70);
  int top = who == 0 ? 37 : 35;
  box(16, top, who == 0 ? 20 : 31, 39 - top, blanket);
  if (who == 0) box(36, 37, 11, 2, mix(blanket, WHITE, 40));  // zurückgeschlagen
  for (int y = top + 1; y < 39; y += 2)
    for (int x = 17 + (y % 4 == 0); x < (who == 0 ? 36 : 47); x += 3) {
      if (W == FARM) box(x, y, 2, 1, (x / 3 + y) % 2 ? rgb(0xf2c14e) : rgb(0x5aa9e6));  // Flickendecke
      else if (W == FAIRY && (x + y) % 2) cell(x, y, rgb(0xfff1a8));                     // Sternchen
      else cell(x, y, b2);
    }
}

// ------------------------------------------------------------------ Szenen

void sceneBedroom(Act a, int f) {
  bool night = a == kids::SLEEP || a == kids::STORY, nap = a == kids::NAP;
  wall(40, night, nap, a == kids::STORY, f);
  floorAt(40, night);
  view(8, 15, 12, 11, night ? NIGHT : MORNING, f, nap);
  if (a == kids::STORY) {  // Nachttischlampe
    box(7, 36, 7, 4, rgb(0x7a5537));
    box(10, 32, 1, 4, rgb(0x5a4030));
    box(8, 29, 5, 3, W == SEA ? rgb(0x9fe6ff) : rgb(0xffd46b));
  }
  if (a == kids::WAKE) {
    bed(0, f, false);
    kid(24, 26, blink(f) ? EYES_SHUT : EYES_OPEN);
    // Zahnbürste vor dem Mund, putzt hoch und runter
    int o = f % 2;
    box(34, 31 + o, 1, 5, rgb(0x2fa3e0));
    box(33, 31 + o, 1, 2, WHITE);
    pet(9, 33, SIT);
  } else {
    bed(a == kids::STORY ? 2 : 1, f, night);
    pet(18, 30, SLEEPING);
  }
  if (a == kids::STORY) glow(21, 61, 34, W == SEA ? 0x9fe6ff : 0xffc86b, 110);
}

void sceneKitchen(Act a, int f) {
  Sky s = a == kids::BREAKFAST ? MORNING : a == kids::LUNCH ? NOON : EVENING;
  wall(42, false, false, false, f);
  if (W == HOME || W == FARM)
    for (int x = 0; x < 58; x++) cell(x, 28, (x / 2) % 2 ? (W == FARM ? rgb(0x5cbf4a) : rgb(0x6fb6e8)) : WHITE);  // Borte
  if (W == FAIRY)  // Wimpelkette
    for (int x = 2; x < 58; x += 4) { box(x, 7, 3, 1, (x / 4) % 2 ? rgb(0xff8fc8) : rgb(0xffd23f)); cell(x + 1, 8, (x / 4) % 2 ? rgb(0xff8fc8) : rgb(0xffd23f)); }
  view(8, 14, 12, 10, s, f);
  if (W == HOME)
    for (int y = 42; y < 48; y++)
      for (int x = 0; x < 58; x++) cell(x, y, ((x / 2) + (y / 2)) % 2 ? rgb(0xd9d9d9) : rgb(0xb9b9b9));
  else floorAt(42, false);
  if (W == SEA) kid(24, 22 - (f % 4 == 0), blink(f) ? EYES_SHUT : EYES_OPEN, 14);  // schwebt leicht
  else kid(24, 22, blink(f) ? EYES_SHUT : EYES_OPEN, 14);
  // Tisch mit Decke in der Farbe der Welt
  uint32_t top = 0xa45a32, c1 = 0xe8453c, c2 = 0xffffff, leg = 0x7a4026;
  switch (W) {
    case SPACE: top = 0x8f99ad; c1 = 0xb8c2d6; c2 = 0xd8dee9; leg = 0x5d6984; break;
    case FARM: top = 0x8b5a2b; c1 = 0x5cbf4a; c2 = 0xffffff; leg = 0x6b4226; break;
    case SEA: top = 0xd8664a; c1 = 0xff8a6a; c2 = 0xffc0a8; leg = 0xc7563c; break;
    case FAIRY: top = 0xd4a017; c1 = 0xffd23f; c2 = 0xfff8e1; leg = 0xb8860b; break;
    default: break;
  }
  box(13, 34, 34, 2, rgb(top));
  for (int y = 36; y < 40; y++)
    for (int x = 13; x < 47; x++) cell(x, y, ((x / 2) + (y / 2)) % 2 ? rgb(c1) : rgb(c2));
  box(15, 40, 2, 4, rgb(leg));
  box(43, 40, 2, 4, rgb(leg));
  if (a == kids::BREAKFAST) {  // Müsli und Milch
    box(25, 32, 6, 2, rgb(0x3c78d8));
    box(26, 31, 4, 1, rgb(0xf3c34a));
    box(34, 29, 2, 5, WHITE);
    if (W == FARM) { box(38, 29, 3, 5, rgb(0xf4f4f4)); box(38, 28, 2, 1, rgb(0x8b5a2b)); }  // Milchkanne
  } else if (a == kids::LUNCH) {  // Teller mit Kartoffeln, Möhren, Erbsen
    box(23, 33, 10, 1, W == SEA ? rgb(0xffd6e0) : WHITE);
    box(24, 32, 3, 1, rgb(0xf2d06b));
    box(27, 32, 2, 1, rgb(0xff8a2a));
    box(29, 32, 2, 1, rgb(0x5cc04a));
    box(34, 30, 1, 4, rgb(0xb0b0b0));
  } else if (W == FAIRY) {  // Torte mit Kerze
    box(24, 30, 8, 4, rgb(0xffb3d1));
    box(24, 30, 8, 1, WHITE);
    box(27, 28, 1, 2, WHITE);
    cell(27, 27, (f % 2) ? rgb(0xffd23f) : rgb(0xff8a2a));
  } else {  // Brot und Tee
    box(24, 31, 5, 3, rgb(0xd9a066));
    box(25, 32, 3, 1, rgb(0xf3d29a));
    box(32, 31, 3, 3, rgb(0xf4f4f4));
    box(35, 32, 1, 1, rgb(0xf4f4f4));
  }
  if (W == SPACE || W == SEA)  // schwebende Tropfen bzw. Blasen
    for (int k = 0; k < 3; k++) cell(20 + k * 7, 26 - ((f + k * 2) % 5), W == SPACE ? rgb(0x9fe6ff) : rgb(0xe6fbff));
  else if (a != kids::BREAKFAST) {  // Dampf
    int sx = a == kids::DINNER ? 34 : 21;
    for (int k = 0; k < 3; k++) cell(sx + ((k + f) % 2), 29 - 2 * k, rgb(0xfffaf0));
  }
  pet(3, 35, SIT);
}

// Draußen: Himmel und Boden der Welt
void outdoor(int f) {
  switch (W) {
    case SPACE: {
      box(0, 0, 58, 40, rgb(0x070b1f));
      starfield(0, 0, 58, 38, f);
      disc(11.5f, 18.5f, 4.6f, rgb(0x2f7fe0));
      disc(10.5f, 17.5f, 1.8f, rgb(0x4cc46a));
      disc(13.0f, 20.0f, 1.2f, rgb(0x4cc46a));
      box(0, 39, 58, 9, rgb(0xa3a7b3));
      box(0, 39, 58, 1, rgb(0xc8ccd6));
      disc(6.0f, 44.0f, 1.8f, rgb(0x868b99));
      disc(52.0f, 42.5f, 1.4f, rgb(0x868b99));
      disc(28.0f, 46.0f, 1.2f, rgb(0x868b99));
      break;
    }
    case SEA: {
      for (int j = 0; j < 40; j++) box(0, j, 58, 1, mix(rgb(0x5fd0f0), rgb(0x1a7fb0), j * 255 / 39));
      for (int k = 0; k < 3; k++)
        for (int j = 0; j < 40; j++) tint(4 + k * 18 + j / 3, j, WHITE, 28);
      floorAt(40, false);
      uint16_t weed = rgb(0x2f9e5a);
      for (int k = 0; k < 4; k++) {
        int x = 3 + k * 15;
        for (int j = 0; j < 9; j++) cell(x + ((j + f + k) / 3) % 2, 39 - j, weed);
      }
      for (int k = 0; k < 4; k++) cell(10 + k * 13, 36 - ((f + k * 3) % 30), rgb(0xe6fbff));
      break;
    }
    case FAIRY: {
      for (int j = 0; j < 40; j++) box(0, j, 58, 1, mix(rgb(0xc9b6ff), rgb(0xffd3ec), j * 255 / 39));
      static const uint32_t RB[] = {0xff6b6b, 0xffb347, 0xffe66d, 0x7ed957, 0x5ec8ff, 0xa78bfa};
      for (int k = 0; k < 6; k++) ringBlocks(16.0f, 40.0f, 13.0f - k, 14.0f - k, rgb(RB[k]), true);
      cloud(36 - (f / 2) % 40 + 20, 14, rgb(0xfff4ff));
      box(0, 39, 58, 9, rgb(0x8be08a));
      box(0, 39, 58, 1, rgb(0xb4f0a8));
      for (int x = 3; x < 58; x += 6) cell(x, 42 + x % 3, (x / 6) % 2 ? rgb(0xff8fc8) : rgb(0xfff1a8));
      break;
    }
    default: {  // Zuhause und Bauernhof: blauer Himmel, Wiese
      skyFill(0, 0, 58, 40, NOON);
      int drift = (f / 2) % 70;
      cloud(48 - drift, 17);
      cloud((70 + 20 - drift) % 70 - 10, 24);
      disc(11.5f, 19.5f, 3.2f, rgb(0xffd23f));
      box(0, 39, 58, 9, rgb(0x5cbf4a));
      box(0, 39, 58, 1, rgb(0x7fd668));
      for (int x = 2; x < 58; x += 5) cell(x, 42 + x % 3, rgb(0x46a03a));
      if (W == FARM) {  // Zaun
        uint16_t fc = rgb(0xf4e3c4);
        box(0, 34, 58, 1, fc);
        box(0, 37, 58, 1, fc);
        for (int x = 1; x < 58; x += 6) box(x, 33, 1, 6, fc);
      }
      break;
    }
  }
}

void building(int f) {  // Kita bzw. Schule der Welt, rechts
  switch (W) {
    case SPACE: {  // Raumstation als Kuppel mit Antenne
      for (int j = 0; j < 11; j++)
        for (int i = -11; i <= 11; i++)
          if (i * i + j * j <= 121) cell(40 + i, 39 - j, rgb(0xd8dee9));
      for (int i = -11; i <= 11; i++) cell(40 + i, 39, rgb(0x8f99ad));
      for (int k = 0; k < 3; k++) box(33 + k * 6, 32, 3, 3, rgb(0x5ec8ff));
      box(40, 22, 1, 7, rgb(0x8f99ad));
      cell(40, 21, (f % 2) ? rgb(0xff453a) : rgb(0xffd23f));
      break;
    }
    case FARM: {  // rote Scheune
      uint16_t red = rgb(0xc0392b), roof = rgb(0x7a2a20);
      for (int j = 0; j < 7; j++) box(40 - j * 2, 16 + j, 4 * j + 1, 1, roof);
      box(30, 23, 21, 17, red);
      box(37, 30, 7, 10, WHITE);
      box(38, 31, 5, 8, red);
      for (int k = 0; k < 5; k++) { cell(38 + k, 31 + k * 2, WHITE); cell(42 - k, 31 + k * 2, WHITE); }
      box(39, 25, 3, 3, rgb(0xf2c14e));
      break;
    }
    case SEA: {  // Korallenschloss
      uint16_t c = rgb(0xff8fab), d = rgb(0xe0607e);
      box(32, 26, 17, 14, c);
      box(30, 20, 5, 20, c);
      box(46, 20, 5, 20, c);
      for (int x = 30; x < 51; x += 2) cell(x, x < 35 || x > 45 ? 19 : 25, c);
      box(38, 32, 5, 8, d);
      box(31, 23, 3, 2, rgb(0x5fd0f0));
      box(47, 23, 3, 2, rgb(0x5fd0f0));
      cell(40, 34, rgb(0xffd23f));
      break;
    }
    case FAIRY: {  // Schloss mit Türmen und Fahnen
      uint16_t c = rgb(0xe6d0ff), roof = rgb(0xff6fae);
      box(33, 26, 15, 14, c);
      for (int k = 0; k < 2; k++) {
        int x = k ? 47 : 29;
        box(x, 20, 5, 20, c);
        for (int j = 0; j < 4; j++) box(x + j / 2, 16 + j, 5 - j, 1, roof);
        box(x + 2, 12, 1, 4, rgb(0x8a6bbf));
        box(x + 3, 12 + (f % 2), 2, 1, rgb(0xffd23f));
      }
      for (int x = 33; x < 48; x += 2) cell(x, 25, c);
      box(38, 32, 5, 8, rgb(0xb48cff));
      disc(40.5f, 32.0f, 2.5f, rgb(0xb48cff));
      break;
    }
    default: {  // Haus mit rotem Dach
      uint16_t roof = rgb(0xd9534f);
      for (int j = 0; j < 6; j++) box(39 - j * 2 + 1, 17 + j, 4 * j + 2, 1, roof);
      box(30, 23, 20, 17, rgb(0xffdf8a));
      box(38, 32, 5, 8, rgb(0x8a5a33));
      cell(42, 36, rgb(0xffd23f));
      box(32, 26, 4, 4, rgb(0x9fdcff));
      box(45, 26, 4, 4, rgb(0x9fdcff));
      break;
    }
  }
}

void sceneDaycare(int f) {
  outdoor(f);
  building(f);
  // Kind mit Ranzen, wippt beim Gehen
  int b = f % 2;
  box(13, 33 + b, 12, 5, rgb(0xffb020));
  kid(14, 25 + b, blink(f) ? EYES_SHUT : EYES_OPEN, 16, false, true);
  box(16, 33 + b, 1, 3, rgb(0x8a5a10));
  box(21, 33 + b, 1, 3, rgb(0x8a5a10));
  pet(3, 33 + (f + 1) % 2, SIT);
}

void unicorn(int x, int y, int f) {  // 14 × 11 Blöcke
  static const char* const U[] = {
      "..........y...",
      ".........yy...",
      "........wwwm..",
      ".......wweww..",
      "......wwwwwmm.",
      "wwwwwwwwwwm...",
      "mwwwwwwwwww...",
      "m.wwwwwwwww...",
      "m.ww....ww....",
      "..ww....ww....",
      "..gg....gg....",
  };
  const Pal pal[] = {{'w', WHITE}, {'e', rgb(0x2b1a10)}, {'y', rgb(0xffd23f)}, {'g', rgb(0xb48cff)},
                     {'m', (f % 2) ? rgb(0xff8fc8) : rgb(0x8fd0ff)}};
  sprite(x, y, U, 11, pal, 5);
}

void scenePlay(int f) {
  outdoor(f);
  static const int8_t H[] = {0, 3, 5, 6, 5, 3};
  int h = H[f % 6];
  switch (W) {
    case SPACE: {  // Saturn, hohe Sprünge mit Helm
      disc(45.0f, 14.0f, 3.5f, rgb(0xf2c14e));
      for (int i = -7; i <= 7; i++) cell(45 + i, 14 + (i > 2 ? 1 : i < -2 ? -1 : 0), rgb(0xd9a74a));
      kid(14, 22 - h, blink(f) ? EYES_SHUT : EYES_OPEN, 16, false, true);
      box(15, 39, 9, 1, rgb(0x868b99));  // Schatten
      disc(34.0f, 37.0f - h / 2, 2.0f, rgb(0xc8ccd6));
      break;
    }
    case FARM: {  // Heuhaufen und hüpfendes Küken
      for (int j = 0; j < 8; j++) box(42 - j, 32 + j, 8 + 2 * j, 1, j % 2 ? rgb(0xe8c35a) : rgb(0xd9ad3c));
      cell(44, 33, rgb(0xc69a2c));
      kid(14, 24, blink(f) ? EYES_SHUT : EYES_OPEN);
      int cy = 37 - h / 2;
      box(30, cy, 3, 3, rgb(0xffd23f));
      cell(33, cy + 1, rgb(0xff8a2a));
      cell(31, cy, rgb(0x2b1a10));
      break;
    }
    case SEA: {  // schwimmt im Riff, Fische ziehen vorbei
      for (int k = 0; k < 3; k++) disc(44.0f + k * 3, 37.0f - k, 3.0f - k * 0.5f, k == 1 ? rgb(0xff8fab) : rgb(0xff7a5c));
      kid(16, 22 + (f % 4 < 2 ? 0 : 1), blink(f) ? EYES_SHUT : EYES_OPEN);
      fish(58 - (f * 2) % 70, 16, rgb(0xffd23f), true);
      fish(-8 + (f * 3 + 30) % 70, 28, rgb(0xff6b6b), false);
      break;
    }
    case FAIRY: {  // Einhorn und Schmetterling
      unicorn(38, 28, f);
      kid(14, 24, blink(f) ? EYES_SHUT : EYES_OPEN);
      int bx = 30 + (f % 6) - 3, by = 26 + H[f % 6] / 2;
      cell(bx, by, rgb(0x8a6bbf));
      cell(bx - 1, by - (f % 2), rgb(0xff8fc8));
      cell(bx + 1, by - (f % 2), rgb(0xff8fc8));
      break;
    }
    default: {  // Baum und hüpfender Ball
      box(44, 29, 3, 11, rgb(0x8a5a33));
      disc(45.5f, 24.5f, 7.2f, rgb(0x3fa34d));
      disc(43.0f, 22.0f, 2.5f, rgb(0x5cc45f));
      cell(48, 26, rgb(0xe74c3c));
      cell(42, 27, rgb(0xe74c3c));
      kid(14, 24, blink(f) ? EYES_SHUT : EYES_OPEN);
      disc(33.0f, 37.0f - h, 2.7f, rgb(0xf05a4a));
      box(32, 36 - h, 2, 1, WHITE);
      box(31, 41, 4, 1, rgb(0x46a03a));
      break;
    }
  }
  pet(24, 33, SIT);
}

void sceneBath(int f) {
  uint32_t t1 = 0xbfe6f5, t2 = 0xa6d8ee, tub = 0xffffff, tubSide = 0xf4f4f4;
  switch (W) {
    case SPACE: t1 = 0x5d6984; t2 = 0x4a5570; tub = 0xd8dee9; tubSide = 0xb8c2d6; break;
    case FARM: t1 = 0xd49c62; t2 = 0xc48c52; tub = 0x9b6a3f; tubSide = 0x8b5a2b; break;
    case SEA: break;
    case FAIRY: t1 = 0xf8e1ff; t2 = 0xefcff7; tub = 0xffb3d1; tubSide = 0xff9cc4; break;
    default: break;
  }
  if (W == SEA) wall(42, false, false, false, f);
  else
    for (int y = 0; y < 42; y++)
      for (int x = 0; x < 58; x++) cell(x, y, ((x / 3) + (y / 3)) % 2 ? rgb(t1) : rgb(t2));
  if (W == SEA) floorAt(42, false);
  else box(0, 42, 58, 6, W == FARM ? rgb(0xe0c066) : rgb(0xe2e2e2));
  kid(24, 21, blink(f) ? EYES_SHUT : EYES_OPEN, 12, true);
  if (W == SEA) {  // Riesenmuschel als Wanne
    for (int j = 0; j < 10; j++) box(10 + j / 2, 31 + j, 38 - j, 1, j % 2 ? rgb(0xffb3c6) : rgb(0xff9cb5));
  } else {
    box(10, 31, 38, 2, rgb(tub));
    box(11, 33, 36, 8, rgb(tubSide));
    box(11, 40, 36, 1, mix(rgb(tubSide), BLACK, 40));
    if (W == FARM) for (int x = 14; x < 46; x += 5) box(x, 33, 1, 8, mix(rgb(tub), BLACK, 60));  // Dauben
    uint16_t foot = W == FAIRY ? rgb(0xffd23f) : rgb(0xb0b0b0);
    box(13, 41, 2, 2, foot);
    box(43, 41, 2, 2, foot);
  }
  // Schaum und aufsteigende Blasen
  for (int x = 12; x < 46; x += 3) disc(x + 0.5f, 31.0f, 1.6f, WHITE);
  for (int k = 0; k < 3; k++) {
    int y = 28 - ((f + k * 2) % 6) * 2;
    disc(17.5f + k * 9 + (k == 1) * 14, y + 0.5f, 0.9f, W == FAIRY && k == 1 ? rgb(0xfff1a8) : rgb(0xeefaff));
  }
  if (W == SPACE) {  // schwebende Wassertropfen
    for (int k = 0; k < 4; k++) disc(14.0f + k * 9, 22.0f - ((f + k) % 4), 0.8f, rgb(0x9fe6ff));
  } else if (W == SEA) {  // Kugelfisch
    disc(40.0f, 27.0f + (f % 2), 2.4f, rgb(0xffd23f));
    cell(41, 26 + (f % 2), BLACK);
  } else {  // Quietscheente (auf dem Bauernhof eine echte)
    uint16_t c = W == FARM ? WHITE : W == FAIRY ? rgb(0xff8fc8) : rgb(0xffd23f);
    box(38, 28, 4, 3, c);
    box(40, 26, 2, 2, c);
    cell(42, 27, rgb(0xff8a2a));
    cell(40, 26, rgb(0x2b1a10));
  }
  pet(2, 35, SIT);
}

// ------------------------------------------------------------------ Wochen-Abenteuer

// Weg durch sieben Stationen (Start + Montag … Sonntag), in Blöcken, innerhalb des Rings und unter der Uhrzeit
const int8_t PATH[8][2] = {{9, 34}, {15, 42}, {25, 40}, {35, 44}, {45, 38}, {48, 28}, {39, 23}, {27, 26}};

const char* const CHAPTERS[WORLDS][7] = {
    {"Durch den Wald", "Über die Brücke", "Die Blumenwiese", "Auf den Hügel", "Am Bach entlang", "Zum See", "Picknick am See"},
    {"Start der Rakete", "Landung auf dem Mond", "Der rote Mars", "Durch die Asteroiden", "Die Saturnringe", "Dem Kometen nach", "Heimkehr zur Erde"},
    {"Der Hühnerstall", "Die Kuhweide", "Zur Windmühle", "Der Apfelgarten", "Über den Bach", "Die Schafweide", "Das Erntefest"},
    {"Der Korallengarten", "Die Schildkrötenbucht", "Das alte Wrack", "Der Seetangwald", "Das Tiefseeleuchten", "Die Delfinlagune", "Der Muschelschatz"},
    {"Der Zauberwald", "Das Pilzdorf", "Die Regenbogenbrücke", "Der Drachenberg", "Der Feensee", "Das Wolkenschloss", "Das Königsfest"},
};

void mapBackground(int f) {
  switch (W) {
    case SPACE: box(0, 0, 58, 48, rgb(0x070b1f)); starfield(0, 0, 58, 48, f); break;
    case SEA:
      for (int j = 0; j < 48; j++) box(0, j, 58, 1, mix(rgb(0x4cc3e0), rgb(0x1a6fa0), j * 255 / 47));
      for (int k = 0; k < 5; k++) cell(8 + k * 11, 30 - ((f + k * 4) % 18), rgb(0xe6fbff));
      break;
    case FARM:
      box(0, 0, 58, 48, rgb(0x8bd36f));
      for (int y = 18; y < 48; y += 6)
        for (int x = (y / 6) % 2 * 7; x < 58; x += 14) box(x, y, 12, 5, ((x + y) / 7) % 3 == 0 ? rgb(0xf2d06b) : rgb(0x7cc966));
      break;
    case FAIRY:
      for (int j = 0; j < 48; j++) box(0, j, 58, 1, mix(rgb(0xd9c8ff), rgb(0xb4f0a8), j * 255 / 47));
      cloud(6, 18, rgb(0xfff4ff));
      cloud(44, 14, rgb(0xfff4ff));
      break;
    default:
      box(0, 0, 58, 48, rgb(0x8bd36f));
      for (int k = 0; k < 9; k++) cell((k * 13) % 56 + 1, 20 + (k * 7) % 26, rgb(0x6cc057));
      break;
  }
}

// kleines Bild zur Station k (1 … 7) – worum es im Kapitel geht; (x, y) = Mitte
void stationIcon(int k, int x, int y, int f) {
  uint16_t white = WHITE;
  switch (W) {
    case SPACE:
      switch (k) {
        case 1: box(x - 1, y - 4, 3, 6, white); cell(x, y - 5, rgb(0xe8453c)); cell(x - 2, y + 1, rgb(0xe8453c)); cell(x + 2, y + 1, rgb(0xe8453c));
                if (f % 2) cell(x, y + 2, rgb(0xffb020)); break;  // Rakete
        case 2: disc(x + 0.5f, y + 0.5f, 2.6f, rgb(0xc8ccd6)); cell(x - 1, y, rgb(0x9aa0ad)); break;  // Mond
        case 3: disc(x + 0.5f, y + 0.5f, 2.6f, rgb(0xd9534f)); cell(x + 1, y - 1, rgb(0xb03a35)); break;  // Mars
        case 4: cell(x - 2, y - 1, rgb(0x8f99ad)); box(x, y, 2, 2, rgb(0x8f99ad)); cell(x + 2, y - 2, rgb(0x8f99ad)); cell(x - 1, y + 2, rgb(0x8f99ad)); break;
        case 5: disc(x + 0.5f, y + 0.5f, 2.2f, rgb(0xf2c14e)); for (int i = -4; i <= 4; i++) cell(x + i, y + (i > 1) - (i < -1), rgb(0xd9a74a)); break;
        case 6: disc(x + 0.5f, y + 0.5f, 1.4f, white); for (int i = 1; i < 5; i++) tint(x - i, y + i / 2, rgb(0x9fe6ff), 255 - i * 45); break;  // Komet
        default: disc(x + 0.5f, y + 0.5f, 3.4f, rgb(0x2f7fe0)); disc(x - 0.5f, y - 0.3f, 1.5f, rgb(0x4cc46a)); break;  // Erde
      }
      break;
    case FARM:
      switch (k) {
        case 1: box(x - 2, y - 1, 5, 3, rgb(0xb5651a)); for (int j = 0; j < 2; j++) box(x - 1 - j, y - 3 + j, 3 + 2 * j, 1, rgb(0x7a2a20)); cell(x + 3, y + 1, WHITE); break;  // Hühnerstall
        case 2: box(x - 2, y - 1, 5, 3, WHITE); cell(x - 1, y, BLACK); cell(x + 1, y - 1, BLACK); cell(x + 3, y - 1, WHITE); cell(x - 2, y + 2, WHITE); cell(x + 2, y + 2, WHITE); break;  // Kuh
        case 3: box(x, y - 2, 2, 5, rgb(0xd9c8a0)); for (int i = -3; i <= 3; i++) { cell(x + (f % 2 ? i : 0), y - 3 + (f % 2 ? 0 : i), rgb(0x8b5a2b)); } break;  // Windmühle
        case 4: box(x, y, 1, 3, rgb(0x8a5a33)); disc(x + 0.5f, y - 1.0f, 2.6f, rgb(0x3fa34d)); cell(x - 1, y - 1, rgb(0xe74c3c)); cell(x + 1, y - 2, rgb(0xe74c3c)); break;
        case 5: for (int i = -3; i <= 3; i++) box(x + i, y + (i + 4) % 2, 1, 1, rgb(0x5ec8ff)); break;  // Bach
        case 6: disc(x + 0.5f, y + 0.5f, 2.0f, rgb(0xf4f1ee)); cell(x + 2, y, rgb(0x3a3a3a)); cell(x - 1, y + 2, rgb(0x3a3a3a)); cell(x + 1, y + 2, rgb(0x3a3a3a)); break;  // Schaf
        default: box(x - 3, y - 1, 7, 4, rgb(0xc0392b)); for (int j = 0; j < 3; j++) box(x - j, y - 4 + j, 1 + 2 * j, 1, rgb(0x7a2a20)); box(x - 4, y - 6, 1, 3, rgb(0x6b4226)); cell(x - 3, y - 6, rgb(0xffd23f)); break;
      }
      break;
    case SEA:
      switch (k) {
        case 1: for (int i = -2; i <= 2; i++) box(x + i, y - abs(i) - 1, 1, abs(i) + 3, i % 2 ? rgb(0xff8fab) : rgb(0xff7a5c)); break;  // Korallen
        case 2: disc(x + 0.5f, y + 0.5f, 2.2f, rgb(0x3f9e5a)); cell(x + 3, y, rgb(0x7ccf8a)); cell(x - 2, y + 2, rgb(0x7ccf8a)); cell(x + 2, y + 2, rgb(0x7ccf8a)); break;  // Schildkröte
        case 3: box(x - 3, y, 7, 2, rgb(0x6b4226)); box(x, y - 4, 1, 4, rgb(0x6b4226)); box(x + 1, y - 4, 2, 2, rgb(0xd9d0c0)); break;  // Wrack
        case 4: for (int k2 = -1; k2 <= 1; k2++) for (int j = 0; j < 5; j++) cell(x + k2 * 2 + ((j + f) / 2) % 2, y + 2 - j, rgb(0x2f9e5a)); break;
        case 5: disc(x + 0.5f, y - 0.5f, 2.0f, (f % 2) ? rgb(0xc9a0ff) : rgb(0x9fe6ff)); for (int i = -1; i <= 1; i++) box(x + i, y + 1, 1, 2 + (i + f) % 2, rgb(0xc9a0ff)); break;  // Qualle
        case 6: box(x - 2, y, 5, 2, rgb(0x6fa8dc)); cell(x + 3, y, rgb(0x6fa8dc)); cell(x, y - 1, rgb(0x6fa8dc)); cell(x - 3, y + 1 - (f % 2), rgb(0x6fa8dc)); break;  // Delfin
        default: box(x - 3, y - 1, 7, 4, rgb(0x8b5a2b)); box(x - 3, y - 2, 7, 1, rgb(0xffd23f)); cell(x, y, rgb(0xffd23f)); break;  // Schatz
      }
      break;
    case FAIRY:
      switch (k) {
        case 1: box(x, y, 1, 3, rgb(0x6b4a8a)); disc(x + 0.5f, y - 1.0f, 2.6f, rgb(0x9b59d0)); cell(x - 1, y - 2, rgb(0xfff1a8)); break;  // Zauberbaum
        case 2: box(x, y, 1, 2, WHITE); box(x - 2, y - 1, 5, 1, rgb(0xe8453c)); box(x - 1, y - 2, 3, 1, rgb(0xe8453c)); cell(x - 1, y - 1, WHITE); break;  // Pilz
        case 3: { static const uint32_t RB[] = {0xff6b6b, 0xffe66d, 0x5ec8ff}; for (int c = 0; c < 3; c++) ringBlocks(x + 0.5f, y + 2.0f, 3.0f - c, 4.0f - c, rgb(RB[c]), true); break; }
        case 4: for (int j = 0; j < 4; j++) box(x - j, y - 3 + j, 2 * j + 1, 1, rgb(0x8a6bbf)); cell(x + 2, y - 3, rgb(0xff8a2a)); break;  // Drachenberg
        case 5: disc(x + 0.5f, y + 0.5f, 2.5f, rgb(0x8fd0ff)); if (f % 2) cell(x + 1, y - 1, rgb(0xfff1a8)); break;  // Feensee
        case 6: cloud(x - 3, y, rgb(0xfff4ff)); box(x - 1, y - 3, 3, 3, rgb(0xe6d0ff)); cell(x, y - 4, rgb(0xff6fae)); break;
        default: box(x - 3, y - 1, 7, 4, rgb(0xe6d0ff)); box(x - 3, y - 4, 2, 3, rgb(0xe6d0ff)); box(x + 2, y - 4, 2, 3, rgb(0xe6d0ff)); cell(x - 3, y - 5, rgb(0xff6fae)); cell(x + 3, y - 5, rgb(0xff6fae)); break;
      }
      break;
    default:
      switch (k) {
        case 1: for (int t = -1; t <= 1; t++) disc(x + t * 2.0f + 0.5f, y - (t == 0) + 0.5f, 1.6f, rgb(0x2f8a3c)); break;  // Wald
        case 2: box(x - 3, y, 7, 1, rgb(0x8a5a33)); cell(x - 3, y - 1, rgb(0x8a5a33)); cell(x + 3, y - 1, rgb(0x8a5a33)); box(x - 3, y + 1, 7, 1, rgb(0x5ec8ff)); break;  // Brücke
        case 3: for (int i = -2; i <= 2; i += 2) { cell(x + i, y, (i + 4) % 4 ? rgb(0xff8fc8) : rgb(0xffd23f)); cell(x + i, y + 1, rgb(0x3fa34d)); } break;  // Blumen
        case 4: for (int j = 0; j < 3; j++) box(x - 2 - j, y - 2 + j, 5 + 2 * j, 1, rgb(0x5aa64a)); break;  // Hügel
        case 5: for (int i = -3; i <= 3; i++) cell(x + i, y + (i + 4) % 2, rgb(0x5ec8ff)); break;  // Bach
        case 6: disc(x + 0.5f, y + 0.5f, 3.0f, rgb(0x5ec8ff)); cell(x - 1, y - 1, rgb(0xbfe8ff)); break;  // See
        default: for (int j = 0; j < 3; j++) for (int i = 0; i < 6; i++) cell(x - 3 + i, y - 1 + j, (i + j) % 2 ? rgb(0xe8453c) : WHITE); break;  // Picknick
      }
      break;
  }
}

void flag(int x, int y) {
  box(x, y - 4, 1, 5, rgb(0x6b4226));
  box(x + 1, y - 4, 2, 1, rgb(0xe8453c));
  cell(x + 1, y - 3, rgb(0xe8453c));
}

void sceneAdventure(int day, float prog, int f) {
  mapBackground(f);
  bool darkMap = W == SPACE || W == SEA;
  uint16_t done = rgb(0xffd23f), todo = darkMap ? rgb(0x7f93bb) : rgb(0xfff8e6), edge = darkMap ? rgb(0x050816) : rgb(0x4a7a3a);
  float hx = PATH[day][0], hy = PATH[day][1];  // wo die Figur gerade ist
  // erst alle Bilder der Stationen, dann der Weg darüber
  for (int k = 1; k <= 7; k++) stationIcon(k, PATH[k][0] + (k == 7 ? 0 : 0), PATH[k][1] - (k == 7 ? 1 : 4), f);
  for (int k = 0; k < 7; k++) {
    float x0 = PATH[k][0], y0 = PATH[k][1], x1 = PATH[k + 1][0], y1 = PATH[k + 1][1];
    float len = hypotf(x1 - x0, y1 - y0);
    for (float t = 1.0f; t < len - 0.5f; t += 2.0f) {
      float u = t / len;
      bool walked = k < day || (k == day && u <= prog);
      int x = (int)(x0 + (x1 - x0) * u), y = (int)(y0 + (y1 - y0) * u);
      cell(x, y + 1, edge);
      cell(x, y, walked ? done : todo);
    }
    if (k == day) { hx = x0 + (x1 - x0) * prog; hy = y0 + (y1 - y0) * prog; }
  }
  for (int k = 0; k < 7; k++) {  // Stationen: geschafft = Fähnchen, heute = blinkender Ring, später = Punkt
    int x = PATH[k][0], y = PATH[k][1];
    if (k == 0) disc(x + 0.5f, y + 0.5f, 1.5f, done);
    else if (k <= day) flag(x, y);
    else if (k == day + 1) { ringBlocks(x + 0.5f, y + 0.5f, 1.2f, (f % 2) ? 2.6f : 2.1f, done); cell(x, y, done); }
    else disc(x + 0.5f, y + 0.5f, 1.2f, todo);
  }
  if (day == 6 && prog >= 1) flag(PATH[7][0] + 4, PATH[7][1]);
  // die Figur (nur der Kopf) und das Tier daneben, auf dem Weg
  kid((int)hx - 4, (int)hy - 9, blink(f) ? EYES_SHUT : EYES_OPEN, 8);
  if (cur && cur->pet != NO_PET) {
    uint16_t c = cur->pet == BUNNY ? rgb(0xf4f1ee) : cur->pet == DOG ? rgb(0xc08a52) : cur->pet == DRAGON ? rgb(0x4cc46a) : rgb(0xe8902e);
    int px2 = (int)hx + 6, py2 = (int)hy - 3 - (f % 2);
    box(px2, py2, 3, 2, c);
    cell(px2, py2 - 1, c);
    cell(px2 + 2, py2 - 1, c);
    cell(px2 + 1, py2, BLACK);
  }
}

}  // namespace

uint16_t mix(uint16_t a, uint16_t b, int t) {
  int r = ((a >> 11) * (255 - t) + (b >> 11) * t) / 255;
  int g = (((a >> 5) & 63) * (255 - t) + ((b >> 5) & 63) * t) / 255;
  int bl = ((a & 31) * (255 - t) + (b & 31) * t) / 255;
  return (r << 11) | (g << 5) | bl;
}

bool showsAdventure(const Scene& s) {
  return s.adventure && (s.act == kids::PLAY || (s.act == kids::DAYCARE && s.weekend));
}

void draw(uint16_t* buf, const Scene& s) {
  px = buf;
  W = s.world < WORLDS ? s.world : HOME;
  cur = &s;
  fill(0, 0, S, S, BLACK);
  int f = s.frame;
  if (showsAdventure(s)) sceneAdventure(std::min(6, std::max(0, s.day)), std::min(1.0f, std::max(0.0f, s.progress)), f);
  else
    switch (s.act) {
      case kids::BREAKFAST: case kids::LUNCH: case kids::DINNER: sceneKitchen(s.act, f); break;
      case kids::DAYCARE: if (s.weekend) scenePlay(f); else sceneDaycare(f); break;
      case kids::PLAY: scenePlay(f); break;
      case kids::BATH: sceneBath(f); break;
      default: sceneBedroom(s.act, f); break;
    }
  cur = nullptr;
}

const char* worldName(World w) {
  static const char* const N[] = {"Zuhause", "Weltraum", "Bauernhof", "Unterwasser", "Märchenland"};
  return w < WORLDS ? N[w] : "";
}

const char* petName(Pet p) {
  static const char* const N[] = {"kein Tier", "Katze", "Hase", "Hund", "Drache"};
  return p < PETS ? N[p] : "";
}

const char* chapter(World w, int day) { return w < WORLDS && day >= 0 && day < 7 ? CHAPTERS[w][day] : ""; }

}  // namespace kidart
