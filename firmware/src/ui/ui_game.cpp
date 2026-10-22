// Balance: Kugeln durch Neigen der Uhr in Löcher rollen (Bewegungssensor QMI8658).
//   „Ins Ziel“: eine Kugel ins goldene Loch, rote Löcher sind Fallen; mit jeder Stufe mehr Fallen
//   „Alle Kugeln“: drei Kugeln in drei Löcher, so schnell wie möglich
#include <Preferences.h>
#include <math.h>
#include "../audio.h"
#include "../imu.h"
#include "game_theme.h"
#include "ui.h"

using namespace ui;

static const float FIELD_R = gtheme::R;       // Spielfeld-Radius
static const float BALL_R = 15;        // Kugel-Radius
static const float GRAV = 1100;    // px/s² bei 1 g
static const int MAXB = 3, MAXH = 12;

struct Ball { float x, y, vx, vy; bool in; float sink; lv_obj_t* o; };
struct Hole { float x, y, r; bool goal, filled; lv_obj_t* o; };

static Ball balls[MAXB];
static Hole holes[MAXH];
static int nb = 0, nh = 0, level = 1, mode = 0;  // mode 0 = Menü, 1 = Ins Ziel, 2 = Alle Kugeln
static float startX, startY;
static uint32_t t0 = 0, endAt = 0, resetAt = 0, lastStep = 0;
static lv_obj_t *field, *hud, *menu, *result;
static lv_timer_t* timer = nullptr;
static int bestLevel = 0;
static uint32_t bestMs = 0;
static uint32_t seed = 1;
static uint16_t* bg = nullptr;  // Untergrund (PSRAM)
static int theme = 0, drawnTheme = -1;

static float frand() { seed = seed * 1103515245u + 12345u; return ((seed >> 8) & 0xFFFF) / 65535.0f; }

static lv_obj_t* disc(lv_obj_t* parent, int r, lv_color_t bg, lv_color_t border, int bw) {
  lv_obj_t* o = lv_obj_create(parent);
  lv_obj_remove_style_all(o);
  lv_obj_set_size(o, 2 * r, 2 * r);
  lv_obj_set_style_radius(o, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(o, bg, 0);
  lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(o, border, 0);
  lv_obj_set_style_border_width(o, bw, 0);
  lv_obj_remove_flag(o, (lv_obj_flag_t)(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));
  return o;
}

static void place(lv_obj_t* o, float x, float y, float r) { lv_obj_set_pos(o, (int)(CX + x - r), (int)(CY + y - r)); }

static void loadBest() {
  Preferences p;
  p.begin("game", true);
  bestLevel = p.getUChar("lvl", 0);
  bestMs = p.getUInt("ms", 0);
  p.end();
}

static void saveBest() {
  Preferences p;
  p.begin("game", false);
  p.putUChar("lvl", bestLevel);
  p.putUInt("ms", bestMs);
  p.end();
}

// ------------------------------------------------------------------ Aufbau einer Runde

static void clearField(bool withTheme) {
  lv_obj_clean(field);
  nb = nh = 0;
  if (!withTheme) return;
  // Spielfeld: Schale mit dem Thema der Stufe
  if (!bg) bg = (uint16_t*)heap_caps_malloc(W * H * 2, MALLOC_CAP_SPIRAM);
  if (!bg) return;
  if (drawnTheme != theme) { gtheme::draw(bg, theme, 12345 + theme * 977); drawnTheme = theme; }
  lv_obj_t* c = lv_canvas_create(field);
  lv_canvas_set_buffer(c, bg, W, H, LV_COLOR_FORMAT_RGB565);
  lv_obj_set_pos(c, 0, 0);
}

static bool freeSpot(float x, float y, float r, float gap) {
  if (sqrtf(x * x + y * y) > FIELD_R - r - 10) return false;
  if (y - r < -FIELD_R + 52 && fabsf(x) < 120) return false;  // frei unter der Anzeige oben
  for (int i = 0; i < nh; i++)
    if (hypotf(holes[i].x - x, holes[i].y - y) < holes[i].r + r + gap) return false;
  return true;
}

static void addHole(float x, float y, float r, bool goal) {
  Hole& h = holes[nh++];
  h = {x, y, r, goal, false, nullptr};
  lv_color_t rim = goal ? lv_color_hex(0xffc83a) : mode == 2 ? lv_color_hex(gtheme::accent(theme)) : lv_color_hex(0xe0302a);
  h.o = disc(field, (int)r, lv_color_hex(0x000000), rim, goal || mode == 2 ? 4 : 3);
  lv_obj_set_style_bg_grad_color(h.o, lv_color_hex(0x221d17), 0);
  lv_obj_set_style_bg_grad_dir(h.o, LV_GRAD_DIR_VER, 0);
  place(h.o, x, y, r);
}

static void addBall(float x, float y) {
  Ball& b = balls[nb++];
  b = {x, y, 0, 0, false, 0, nullptr};
  b.o = disc(field, (int)BALL_R, lv_color_hex(0xf2efe8), lv_color_hex(0x2a2a2a), 2);
  lv_obj_set_style_bg_grad_color(b.o, lv_color_hex(0x8e8a82), 0);
  lv_obj_set_style_bg_grad_dir(b.o, LV_GRAD_DIR_VER, 0);
  lv_obj_t* shine = disc(b.o, 4, lv_color_white(), lv_color_white(), 0);
  lv_obj_set_pos(shine, 6, 5);
  place(b.o, x, y, BALL_R);
}

static void setHud() {
  char t[48];
  if (mode == 1) snprintf(t, sizeof(t), "Stufe %d · %s", level, gtheme::name(theme));
  else snprintf(t, sizeof(t), "%d von %d", [] { int k = 0; for (int i = 0; i < nb; i++) k += balls[i].in; return k; }(), nb);
  lv_label_set_text(hud, t);
}

static void newRound() {
  seed = millis() | 1;
  theme = mode == 1 ? (level - 1) % gtheme::COUNT : (int)(frand() * gtheme::COUNT) % gtheme::COUNT;
  clearField(true);
  if (mode == 1) {
    // Start und Ziel auf gegenüberliegenden Seiten, dazwischen Fallen
    float a = frand() * 6.2832f;
    startX = cosf(a) * 150;
    startY = sinf(a) * 150;
    float gr = fmaxf(19, 30 - level * 1.2f);
    addHole(-startX * 0.95f, -startY * 0.95f, gr, true);
    int traps = min(MAXH - 1, 2 + level);
    for (int i = 0, tries = 0; i < traps && tries < 400; tries++) {
      float x = (frand() * 2 - 1) * FIELD_R, y = (frand() * 2 - 1) * FIELD_R;
      if (!freeSpot(x, y, 24, 22) || hypotf(x - startX, y - startY) < 70) continue;
      addHole(x, y, 24, false);
      i++;
    }
    addBall(startX, startY);
  } else {
    float a0 = frand() * 6.2832f;
    for (int i = 0; i < 3; i++) addHole(cosf(a0 + i * 2.094f) * 140, sinf(a0 + i * 2.094f) * 140, 24, false);
    for (int i = 0; i < 3; i++) addBall(cosf(a0 + 1.047f + i * 2.094f) * 30, sinf(a0 + 1.047f + i * 2.094f) * 30);
  }
  t0 = millis();
  endAt = resetAt = 0;
  setHud();
  lv_obj_move_foreground(hud);
}

// ------------------------------------------------------------------ Ergebnis

static void showResult(const char* line1, const char* line2) {
  result = lv_obj_create(lv_screen_active());
  lv_obj_remove_style_all(result);
  lv_obj_set_size(result, W, H);
  lv_obj_set_style_bg_color(result, C_BG(), 0);
  lv_obj_set_style_bg_opa(result, LV_OPA_80, 0);
  lv_obj_t* a = label(result, &font_text_32, C_BRASS(), line1);
  lv_obj_align(a, LV_ALIGN_CENTER, 0, -60);
  lv_obj_t* b = label(result, &font_text_20, C_TEXT(), line2);
  lv_obj_set_style_text_align(b, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(b, LV_ALIGN_CENTER, 0, -10);
  lv_obj_t* again = pill(result, I_REPLAY "  Nochmal", C_BRASS(), C_BG());
  lv_obj_align(again, LV_ALIGN_CENTER, 0, 60);
  lv_obj_add_event_cb(again, [](lv_event_t*) {
    lv_obj_delete(result);
    result = nullptr;
    if (mode == 1) level = 1;
    newRound();
  }, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* m = pill(result, "Menü", C_SURF2(), C_TEXT());
  lv_obj_align(m, LV_ALIGN_CENTER, 0, 120);
  lv_obj_add_event_cb(m, [](lv_event_t*) {
    lv_obj_delete(result);
    result = nullptr;
    mode = 0;
    clearField(false);
    lv_obj_remove_flag(menu, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(hud, LV_OBJ_FLAG_HIDDEN);
  }, LV_EVENT_CLICKED, nullptr);
}

// ------------------------------------------------------------------ Physik

static void step(lv_timer_t*) {
  uint32_t now = millis();
  float dt = fminf(0.04f, (now - lastStep) / 1000.0f);
  lastStep = now;
  if (!mode || result) return;
  hwActivity();  // Display bleibt beim Spielen an
  float gx = 0, gy = 0;
  imu::gravity(gx, gy);

  if (resetAt && now > resetAt) {  // nach einem Fehler: Kugel zurück zum Start
    resetAt = 0;
    Ball& b = balls[0];
    b = {startX, startY, 0, 0, false, 0, b.o};
    lv_obj_set_style_transform_scale(b.o, 256, 0);
    lv_obj_set_style_opa(b.o, LV_OPA_COVER, 0);
  }
  if (endAt && now > endAt) {
    endAt = 0;
    if (mode == 1) { level++; if (level - 1 > bestLevel) { bestLevel = level - 1; saveBest(); } newRound(); }
    return;
  }

  for (int i = 0; i < nb; i++) {
    Ball& b = balls[i];
    if (b.in) {  // fällt ins Loch: kleiner werden
      if (b.sink < 1) {
        b.sink = fminf(1, b.sink + dt * 4);
        lv_obj_set_style_transform_pivot_x(b.o, (int)BALL_R, 0);
        lv_obj_set_style_transform_pivot_y(b.o, (int)BALL_R, 0);
        lv_obj_set_style_transform_scale(b.o, (int)(256 * (1 - 0.55f * b.sink)), 0);
        lv_obj_set_style_opa(b.o, (lv_opa_t)(255 * (1 - 0.6f * b.sink)), 0);
      }
      continue;
    }
    b.vx += gx * GRAV * dt;
    b.vy += gy * GRAV * dt;
    // Sog am Lochrand
    for (int k = 0; k < nh; k++) {
      Hole& h = holes[k];
      if (h.filled) continue;
      float dx = h.x - b.x, dy = h.y - b.y, d = hypotf(dx, dy);
      if (d < h.r && d > 0.1f) {
        float pull = 1600 * (1 - d / h.r);
        b.vx += dx / d * pull * dt;
        b.vy += dy / d * pull * dt;
      }
    }
    float damp = 1 - 1.1f * dt;  // Rollreibung
    b.vx *= damp;
    b.vy *= damp;
    b.x += b.vx * dt;
    b.y += b.vy * dt;
    // Rand: zurückprallen
    float d = hypotf(b.x, b.y);
    if (d > FIELD_R - BALL_R) {
      float nx = b.x / d, ny = b.y / d, vn = b.vx * nx + b.vy * ny;
      b.x = nx * (FIELD_R - BALL_R);
      b.y = ny * (FIELD_R - BALL_R);
      if (vn > 0) {
        if (vn > 220) audio::play(audio::FX_TICK);
        b.vx -= 1.45f * vn * nx;
        b.vy -= 1.45f * vn * ny;
      }
    }
  }
  // Kugeln stoßen sich
  for (int i = 0; i < nb; i++)
    for (int j = i + 1; j < nb; j++) {
      Ball &a = balls[i], &c = balls[j];
      if (a.in || c.in) continue;
      float dx = c.x - a.x, dy = c.y - a.y, d = hypotf(dx, dy);
      if (d >= 2 * BALL_R || d < 0.01f) continue;
      float nx = dx / d, ny = dy / d, over = (2 * BALL_R - d) / 2;
      a.x -= nx * over; a.y -= ny * over;
      c.x += nx * over; c.y += ny * over;
      float rel = (c.vx - a.vx) * nx + (c.vy - a.vy) * ny;
      if (rel < 0) {
        float imp = -0.9f * rel;
        a.vx -= imp * nx; a.vy -= imp * ny;
        c.vx += imp * nx; c.vy += imp * ny;
      }
    }
  // Löcher: langsam genug in der Mitte = drin
  for (int i = 0; i < nb; i++) {
    Ball& b = balls[i];
    if (b.in) continue;
    place(b.o, b.x, b.y, BALL_R);
    for (int k = 0; k < nh; k++) {
      Hole& h = holes[k];
      if (h.filled) continue;
      float d = hypotf(h.x - b.x, h.y - b.y);
      if (d > h.r - BALL_R * 0.6f || hypotf(b.vx, b.vy) > 320) continue;
      b.in = true;
      b.x = h.x;
      b.y = h.y;
      place(b.o, b.x, b.y, BALL_R);
      if (mode == 1) {
        if (h.goal) {
          audio::play(audio::FX_OK);
          endAt = now + 900;
        } else {
          audio::play(audio::FX_ERROR);
          resetAt = now + 700;
        }
      } else {
        h.filled = true;
        audio::play(audio::FX_TAP);
        setHud();
        int in = 0;
        for (int q = 0; q < nb; q++) in += balls[q].in;
        if (in == nb) {
          uint32_t ms = now - t0;
          bool rec = !bestMs || ms < bestMs;
          if (rec) { bestMs = ms; saveBest(); }
          char a[32], b2[64];
          snprintf(a, sizeof(a), "%u,%u s", (unsigned)(ms / 1000), (unsigned)(ms / 100 % 10));
          snprintf(b2, sizeof(b2), rec ? "Neuer Rekord!" : "Rekord: %u,%u s", (unsigned)(bestMs / 1000), (unsigned)(bestMs / 100 % 10));
          audio::play(audio::FX_CHIME);
          showResult(a, b2);
        }
      }
      break;
    }
  }
}

// ------------------------------------------------------------------ Seite

static void start(int m) {
  mode = m;
  level = 1;
  lv_obj_add_flag(menu, LV_OBJ_FLAG_HIDDEN);
  lv_obj_remove_flag(hud, LV_OBJ_FLAG_HIDDEN);
  imu::sleep(false);
  newRound();
  play(0);
}

static void build(lv_obj_t* scr) {
  loadBest();
  field = lv_obj_create(scr);
  lv_obj_remove_style_all(field);
  lv_obj_set_size(field, W, H);
  lv_obj_remove_flag(field, (lv_obj_flag_t)(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));
  hud = label(scr, &font_text_20, C_TEXT(), "");
  lv_obj_set_style_bg_color(hud, C_BG(), 0);  // dunkles Schild, damit es auf jedem Untergrund lesbar ist
  lv_obj_set_style_bg_opa(hud, LV_OPA_70, 0);
  lv_obj_set_style_radius(hud, 14, 0);
  lv_obj_set_style_pad_hor(hud, 12, 0);
  lv_obj_set_style_pad_ver(hud, 3, 0);
  lv_obj_align(hud, LV_ALIGN_TOP_MID, 0, 22);
  lv_obj_add_flag(hud, LV_OBJ_FLAG_HIDDEN);

  menu = lv_obj_create(scr);
  lv_obj_remove_style_all(menu);
  lv_obj_set_size(menu, W, H);
  lv_obj_t* t = title(menu, "Balance", C_BRASS());
  (void)t;
  lv_obj_t* h = label(menu, &font_text_20, C_DIM(), "Uhr neigen, Kugel rollen lassen");
  lv_obj_align(h, LV_ALIGN_TOP_MID, 0, 84);
  char b[64];
  lv_obj_t* a = pill(menu, I_FLAG "  Ins Ziel", C_BRASS(), C_BG());
  lv_obj_set_size(a, 240, 64);
  lv_obj_align(a, LV_ALIGN_CENTER, 0, -30);
  lv_obj_add_event_cb(a, [](lv_event_t*) { start(1); }, LV_EVENT_CLICKED, nullptr);
  snprintf(b, sizeof(b), bestLevel ? "Rekord: Stufe %d" : "Fallen ausweichen", bestLevel);
  lv_obj_t* ra = label(menu, &font_text_20, C_DIM(), b);
  lv_obj_align(ra, LV_ALIGN_CENTER, 0, 14);
  lv_obj_t* c = pill(menu, I_GROUP "  Alle Kugeln", C_SURF2(), C_TEXT());
  lv_obj_set_size(c, 240, 64);
  lv_obj_align(c, LV_ALIGN_CENTER, 0, 80);
  lv_obj_add_event_cb(c, [](lv_event_t*) { start(2); }, LV_EVENT_CLICKED, nullptr);
  if (bestMs) snprintf(b, sizeof(b), "Rekord: %u,%u s", (unsigned)(bestMs / 1000), (unsigned)(bestMs / 100 % 10));
  else snprintf(b, sizeof(b), "Drei Kugeln, drei Löcher");
  lv_obj_t* rc = label(menu, &font_text_20, C_DIM(), b);
  lv_obj_align(rc, LV_ALIGN_CENTER, 0, 124);

  mode = 0;
  result = nullptr;
  lastStep = millis();
  timer = lv_timer_create(step, 16, nullptr);
}

static void leave() {
  if (timer) { lv_timer_delete(timer); timer = nullptr; }
  imu::sleep(true);
  mode = 0;
  if (bg) { heap_caps_free(bg); bg = nullptr; drawnTheme = -1; }
}

// Für Bildschirmfotos: direkt eine Stufe zeigen
void gameTestLevel(int l) {
  if (!timer) return;
  if (!mode) start(1);
  level = max(1, l);
  newRound();
}

const Page PAGE_GAME = {build, nullptr, leave, nullptr, false};
