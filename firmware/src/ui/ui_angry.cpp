// Wutmodus (im Kindermodus, nur über die Weboberfläche): auf der Uhr gibt es dann nur noch das Wut-Glas.
//   1. Rote Wut-Funken sitzen in der Uhr. Wer sie ganz fest schüttelt, schleudert sie über den Rand hinaus –
//      nur mit richtig Kraft, ein paar pro Sekunde. Der rote Ring zeigt, wie viel Wut noch drin ist.
//   2. Ist alles draußen, wird es blau: Ruhe-Glitzer sinkt langsam, ein Kreis atmet vor (4 s ein, 6 s aus).
//   3. „Gut gemacht“. Kommt die Wut zurück, einfach wieder schütteln.
#include <math.h>
#include "../audio.h"
#include "../imu.h"
#include "../kids.h"
#include "ui.h"

using namespace ui;

namespace {

constexpr float R = 200;          // Glas-Radius
constexpr int NSPARK = 100, NCALM = 70;
constexpr float EJECT_V = 480;    // so schnell muss ein Funke am Rand sein, um rauszufliegen
constexpr float EJECT_RATE = 9;   // höchstens so viele pro Sekunde

struct P { float x, y, vx, vy, fade; uint8_t alive, size, col; };
P spark[NSPARK], calm[NCALM];

enum Phase { IDLE, SHAKE, ALLOUT, BREATHE, DONE };
Phase phase = IDLE;
uint32_t phaseAt = 0, lastShakeAt = 0, lastStep = 0, lastTick = 0;
float glx = 0, gly = 0, glz = 1;  // langsam gefilterte Schwerkraft
float sh = 0;                     // Schüttel-Stärke (g, geglättet)
float tokens = 0;
int inside = NSPARK;
lv_obj_t *root, *msgL, *heart;
lv_timer_t* timer = nullptr;
uint32_t rs = 1;

const uint32_t HOT[] = {0xff3b30, 0xff6a1a, 0xffb020, 0xff2d55, 0xff8a65};
const uint32_t COOL[] = {0x5ac8fa, 0x7d6bff, 0xbfa8ff, 0xffffff, 0x64d2ff, 0x9ff0e0};

float rnd() { rs = rs * 1103515245u + 12345u; return ((rs >> 8) & 0xFFFF) / 65535.0f; }

void fillSparks(int n) {
  int k = 0;
  for (auto& p : spark) {
    if (p.alive || k >= n) continue;
    float a = rnd() * 6.283f, d = sqrtf(rnd()) * (R - 30);
    p = {cosf(a) * d, sinf(a) * d, 0, 0, 0, 1, (uint8_t)(6 + rnd() * 5), (uint8_t)(rnd() * 5)};
    k++;
  }
  inside = 0;
  for (auto& p : spark) inside += p.alive;
}

void fillCalm() {
  for (auto& p : calm) {
    float a = rnd() * 6.283f, d = sqrtf(rnd()) * (R - 20);
    p = {cosf(a) * d, sinf(a) * d, (rnd() - 0.5f) * 500, (rnd() - 0.5f) * 500, 0, 1, (uint8_t)(3 + rnd() * 4), (uint8_t)(rnd() * 6)};
  }
}

void setMsg(const char* t) { if (strcmp(lv_label_get_text(msgL), t)) lv_label_set_text(msgL, t); }

void go(Phase p) {
  phase = p;
  phaseAt = millis();
  hide(heart, p != DONE);
  if (p == IDLE) { for (auto& s : spark) s.alive = 0; fillSparks(NSPARK); }
  if (p == ALLOUT) { audio::play(audio::FX_CHIME); kids::angryDone(); }
  if (p == BREATHE) fillCalm();
  if (p == DONE) audio::play(audio::FX_OK);
}

// ------------------------------------------------------------------ Bewegung

void moveSparks(float dt, float hx, float hy) {
  for (auto& p : spark) {
    if (!p.alive) {
      if (p.fade > 0) {  // fliegt gerade hinaus
        p.x += p.vx * dt;
        p.y += p.vy * dt;
        p.fade -= dt * 2.2f;
      }
      continue;
    }
    // Trägheit: bewegt sich die Uhr nach rechts, schwappt der Inhalt nach links
    p.vx += (glx * 300 - hx * 9000) * dt;
    p.vy += (gly * 300 - hy * 9000) * dt;
    if (sh > 0.5f) { p.vx += (rnd() - 0.5f) * sh * 260; p.vy += (rnd() - 0.5f) * sh * 260; }
    float damp = 1 - 1.6f * dt;
    p.vx *= damp;
    p.vy *= damp;
    p.x += p.vx * dt;
    p.y += p.vy * dt;
    float d = hypotf(p.x, p.y);
    if (d > R - p.size) {
      float nx = p.x / d, ny = p.y / d, vn = p.vx * nx + p.vy * ny;
      if (vn > EJECT_V && tokens >= 1) {  // raus damit
        tokens -= 1;
        p.alive = 0;
        p.fade = 1;
        p.vx *= 1.3f;
        p.vy *= 1.3f;
        inside--;
        audio::play(audio::FX_TICK);
        continue;
      }
      p.x = nx * (R - p.size);
      p.y = ny * (R - p.size);
      if (vn > 0) { p.vx -= 1.5f * vn * nx; p.vy -= 1.5f * vn * ny; }
      if (vn > 250 && millis() - lastTick > 70) { lastTick = millis(); audio::play(audio::FX_TAP); }  // rasselt
    }
  }
}

void moveCalm(float dt) {
  for (auto& p : calm) {
    p.vx += glx * 90 * dt;  // sinkt langsam nach unten (echtes Unten)
    p.vy += gly * 90 * dt;
    float damp = 1 - 2.4f * dt;
    p.vx *= damp;
    p.vy *= damp;
    p.x += p.vx * dt;
    p.y += p.vy * dt;
    float d = hypotf(p.x, p.y);
    if (d > R - p.size) {
      float nx = p.x / d, ny = p.y / d, vn = p.vx * nx + p.vy * ny;
      p.x = nx * (R - p.size);
      p.y = ny * (R - p.size);
      if (vn > 0) { p.vx -= 1.3f * vn * nx; p.vy -= 1.3f * vn * ny; }
    }
  }
}

float breath(uint32_t t) {  // 0 … 1: 4 s einatmen, 6 s ausatmen
  float s = (t % 10000) / 1000.0f;
  float k = s < 4 ? s / 4 : 1 - (s - 4) / 6;
  return k * k * (3 - 2 * k);
}

void step(lv_timer_t*) {
  uint32_t now = millis();
  float dt = fminf(0.05f, (now - lastStep) / 1000.0f);
  lastStep = now;

  float ax = glx, ay = gly, az = glz;
  if (angryTestShake > 0) {  // Test über USB: kräftiges Schütteln vortäuschen
    angryTestShake -= dt;
    float a = now * 0.03f;
    ax = glx + sinf(a) * 2.6f;
    ay = gly + cosf(a * 1.3f) * 2.2f;
  } else {
    imu::accel(ax, ay, az);
  }
  glx += (ax - glx) * 0.08f;
  gly += (ay - gly) * 0.08f;
  glz += (az - glz) * 0.08f;
  float hx = ax - glx, hy = ay - gly, hz = az - glz;
  float mag = sqrtf(hx * hx + hy * hy + hz * hz);
  sh = fmaxf(mag, sh * 0.88f);
  if (sh > 0.6f) { lastShakeAt = now; hwActivity(); }
  tokens = fminf(3, tokens + dt * EJECT_RATE * fminf(1, sh / 1.2f));

  uint32_t inPhase = now - phaseAt;
  switch (phase) {
    case IDLE:
      setMsg("Wütend?\nSchüttel mich ganz fest!");
      if (sh > 0.9f) go(SHAKE);
      break;
    case SHAKE:
      if (inside <= 0) { go(ALLOUT); break; }
      if (now - lastShakeAt > 2500) setMsg(inside < NSPARK / 4 ? "Fast geschafft!\nNoch ein bisschen!" : "Weiter schütteln!\nRaus mit der Wut!");
      else setMsg(sh > 1.6f ? "Jaaa! Raus damit!" : inside < NSPARK / 4 ? "Fast alles raus!" : "Fester!");
      break;
    case ALLOUT:
      setMsg("Alles raus!");
      if (inPhase > 2200) go(BREATHE);
      break;
    case BREATHE:
      if (inPhase > 1500 && sh > 1.4f && now - phaseAt > 3000) {  // doch noch Wut drin
        fillSparks(30);
        go(SHAKE);
        break;
      }
      if (inPhase < 3000) setMsg("Jetzt ganz ruhig …");
      else setMsg(((inPhase - 3000) % 10000) < 4000 ? "Einatmen …" : "Ausatmen …");
      if (inPhase > 3000 + 4 * 10000) go(DONE);
      break;
    case DONE:
      setMsg("Gut gemacht!\nDie Wut ist draußen.");
      if (inPhase > 60000) go(IDLE);
      else if (inPhase > 3000 && sh > 1.4f) { fillSparks(NSPARK); go(SHAKE); }
      break;
  }
  moveSparks(dt, phase == IDLE && sh < 0.9f ? 0 : hx, phase == IDLE && sh < 0.9f ? 0 : hy);
  if (phase == BREATHE || phase == DONE) moveCalm(dt);
  if (hwScreenOn()) lv_obj_invalidate(root);
}

// ------------------------------------------------------------------ Zeichnen

void circle(lv_layer_t* L, float x, float y, float r, lv_color_t c, lv_opa_t opa) {
  lv_draw_rect_dsc_t d;
  lv_draw_rect_dsc_init(&d);
  d.radius = LV_RADIUS_CIRCLE;
  d.bg_color = c;
  d.bg_opa = opa;
  lv_area_t a = {(int32_t)(CX + x - r), (int32_t)(CY + y - r), (int32_t)(CX + x + r), (int32_t)(CY + y + r)};
  lv_draw_rect(L, &d, &a);
}

void draw(lv_event_t* e) {
  lv_layer_t* L = lv_event_get_layer(e);
  bool cool = phase == BREATHE || phase == DONE;
  float wut = (float)inside / NSPARK;
  uint32_t now = millis();
  // Glas: rot glühend, solange Wut drin ist, danach ruhiges Blau
  lv_color_t bg = cool ? lv_color_hex(0x0a1f3a) : lv_color_mix(lv_color_hex(0x5a0a0a), lv_color_hex(0x140808), (uint8_t)(255 * wut));
  circle(L, 0, 0, R + 4, bg, LV_OPA_COVER);
  // Ring: rot = so viel Wut ist noch drin
  lv_draw_arc_dsc_t arc;
  lv_draw_arc_dsc_init(&arc);
  arc.center = {CX, CY};
  arc.radius = 224;
  arc.width = 12;
  arc.rounded = 1;
  arc.start_angle = 0;
  arc.end_angle = 360;
  arc.color = lv_color_hex(cool ? 0x123a5a : 0x2a1010);
  lv_draw_arc(L, &arc);
  if (!cool && inside > 0) {
    arc.start_angle = 270;
    arc.end_angle = 270 + (int)(360 * wut);
    arc.color = lv_color_hex(0xff3b30);
    lv_draw_arc(L, &arc);
  }
  if (phase == BREATHE && now - phaseAt > 3000) {  // Atemkreis
    float r = 60 + 90 * breath(now - phaseAt - 3000);
    circle(L, 0, 0, r, lv_color_hex(0x64d2ff), 40);
    arc.radius = (int)r;
    arc.width = 4;
    arc.start_angle = 0;
    arc.end_angle = 360;
    arc.color = lv_color_hex(0x9fe6ff);
    lv_draw_arc(L, &arc);
  }
  if (cool)
    for (auto& p : calm) circle(L, p.x, p.y, p.size, lv_color_hex(COOL[p.col % 6]), 220);
  for (auto& p : spark) {
    if (p.alive) {
      // pulsiert leise, solange sie drin sind
      float k = phase == IDLE ? 0.85f + 0.15f * sinf(now * 0.004f + p.x) : 1;
      circle(L, p.x, p.y, p.size * k, lv_color_hex(HOT[p.col % 5]), LV_OPA_COVER);
    } else if (p.fade > 0) {
      circle(L, p.x, p.y, p.size * (1 + (1 - p.fade)), lv_color_hex(HOT[p.col % 5]), (lv_opa_t)(255 * p.fade));
    }
  }
}

}  // namespace

float angryTestShake = 0;

void angryBuild(lv_obj_t* scr) {
  rs = millis() | 1;
  root = lv_obj_create(scr);
  lv_obj_remove_style_all(root);
  lv_obj_set_size(root, W, H);
  lv_obj_add_flag(root, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(root, draw, LV_EVENT_DRAW_MAIN, nullptr);
  // Antippen rührt ein wenig um (ohne die Wut rauszuschleudern)
  lv_obj_add_event_cb(root, [](lv_event_t*) {
    for (auto& p : spark) if (p.alive) { p.vx += (rnd() - 0.5f) * 300; p.vy += (rnd() - 0.5f) * 300; }
    for (auto& p : calm) { p.vx += (rnd() - 0.5f) * 200; p.vy += (rnd() - 0.5f) * 200; }
  }, LV_EVENT_PRESSED, nullptr);
  lv_obj_add_event_cb(root, [](lv_event_t*) {
    if (timer) { lv_timer_delete(timer); timer = nullptr; }
  }, LV_EVENT_DELETE, nullptr);
  heart = label(scr, &font_icon_64, lv_color_hex(0xff6b8a), I_HEART);
  lv_obj_align(heart, LV_ALIGN_CENTER, 0, -40);
  msgL = label(scr, &font_text_32, C_TEXT(), "");
  lv_obj_set_style_text_align(msgL, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(msgL, LV_ALIGN_CENTER, 0, 70);
  lv_obj_set_style_bg_color(msgL, C_BG(), 0);  // dunkles Schild, lesbar über den Funken
  lv_obj_set_style_bg_opa(msgL, LV_OPA_50, 0);
  lv_obj_set_style_radius(msgL, 18, 0);
  lv_obj_set_style_pad_hor(msgL, 16, 0);
  lv_obj_set_style_pad_ver(msgL, 6, 0);
  imu::sleep(false);
  lastStep = millis();
  sh = 0;
  glx = gly = 0;
  glz = 1;
  go(IDLE);
  if (timer) lv_timer_delete(timer);
  timer = lv_timer_create(step, 30, nullptr);
}
