// Startseite: Taschenuhr mit kleiner Sekunde bei der 6.
// Das Zifferblatt wird einmal in einen Puffer gezeichnet, die Zeiger bei jedem Bild darüber.
#include <math.h>
#include <time.h>
#include "../clockwork.h"
#include "../music.h"
#include "../net.h"
#include "../audio.h"
#include "../radio.h"
#include "../settings.h"
#include "../weather.h"
#include "../faces/dc.h"
#include "ui.h"

using namespace ui;

static const int SUB_Y = CY + 105, SUB_R = 50;  // kleine Sekunde
static uint16_t* dialBuf = nullptr;
static bool dialReady = false;

static lv_obj_t* hands;
static lv_obj_t* dateL;
static lv_obj_t* compL;
static lv_obj_t* batL;
static lv_obj_t* netL;
static int lastSec = -1;

static lv_point_precise_t polar(float cx, float cy, float deg, float r) {
  float a = deg * (float)M_PI / 180.0f;
  return {cx + sinf(a) * r, cy - cosf(a) * r};
}

static void drawDial(lv_obj_t* canvas) {
  lv_canvas_fill_bg(canvas, C_BG(), LV_OPA_COVER);
  lv_layer_t layer;
  lv_canvas_init_layer(canvas, &layer);

  lv_draw_arc_dsc_t arc;
  lv_draw_arc_dsc_init(&arc);
  arc.center = {CX, CY};
  arc.radius = 229;
  arc.start_angle = 0;
  arc.end_angle = 360;
  arc.width = 3;
  arc.color = C_BRASS();
  lv_draw_arc(&layer, &arc);

  lv_draw_line_dsc_t ln;
  lv_draw_line_dsc_init(&ln);
  for (int i = 0; i < 60; i++) {
    bool hour = i % 5 == 0;
    ln.p1 = polar(CX, CY, i * 6, hour ? 194 : 207);
    ln.p2 = polar(CX, CY, i * 6, 217);
    ln.width = hour ? 5 : 2;
    ln.color = hour ? C_BRASS() : lv_color_hex(0x5A564F);
    ln.round_start = ln.round_end = hour;
    lv_draw_line(&layer, &ln);
  }

  // Ziffern (ohne die 6, dort sitzt die kleine Sekunde)
  lv_draw_label_dsc_t lb;
  lv_draw_label_dsc_init(&lb);
  lb.font = &font_serif_44;
  lb.color = C_TEXT();
  // LVGL zeichnet erst in lv_canvas_finish_layer(): die Texte müssen bis dahin gültig bleiben
  static const char* NUM[] = {"", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12"};
  for (int h = 1; h <= 12; h++) {
    if (h == 6) continue;
    const char* t = NUM[h];
    lv_point_t sz;
    lv_text_get_size(&sz, t, lb.font, 0, 0, 1000, LV_TEXT_FLAG_NONE);
    lv_point_precise_t p = polar(CX, CY, h * 30, 162);
    lv_area_t a = {(int32_t)(p.x - sz.x / 2), (int32_t)(p.y - sz.y / 2), (int32_t)(p.x + sz.x / 2 + 1),
                   (int32_t)(p.y + sz.y / 2 + 1)};
    lb.text = t;
    lv_draw_label(&layer, &lb, &a);
  }

  // kleine Sekunde
  arc.center = {CX, SUB_Y};
  arc.radius = SUB_R;
  arc.width = 1;
  arc.color = lv_color_hex(0x5A564F);
  lv_draw_arc(&layer, &arc);
  for (int i = 0; i < 60; i += 5) {
    ln.p1 = polar(CX, SUB_Y, i * 6, i % 15 == 0 ? SUB_R - 11 : SUB_R - 7);
    ln.p2 = polar(CX, SUB_Y, i * 6, SUB_R - 2);
    ln.width = i % 15 == 0 ? 3 : 2;
    ln.color = i % 15 == 0 ? C_BRASS() : lv_color_hex(0x7A766E);
    ln.round_start = ln.round_end = 1;
    lv_draw_line(&layer, &ln);
  }
  lv_canvas_finish_layer(canvas, &layer);
}

// Blattzeiger („feuille“): zwei schlanke Dreiecke, am breitesten bei einem Viertel der Länge
static void leaf(lv_layer_t* L, float deg, float len, float tail, float w, lv_color_t c) {
  lv_draw_triangle_dsc_t t;
  lv_draw_triangle_dsc_init(&t);
  t.bg_color = c;
  t.bg_opa = LV_OPA_COVER;
  lv_point_precise_t tip = polar(CX, CY, deg, len);
  lv_point_precise_t back = polar(CX, CY, deg + 180, tail);
  lv_point_precise_t mid = polar(CX, CY, deg, len * 0.24f);
  lv_point_precise_t l = polar(mid.x, mid.y, deg - 90, w / 2);
  lv_point_precise_t r = polar(mid.x, mid.y, deg + 90, w / 2);
  t.p[0] = back; t.p[1] = l; t.p[2] = tip;
  lv_draw_triangle(L, &t);
  t.p[1] = r;
  lv_draw_triangle(L, &t);
}

static void dot(lv_layer_t* L, float x, float y, int r, lv_color_t c) {
  lv_draw_rect_dsc_t d;
  lv_draw_rect_dsc_init(&d);
  d.radius = LV_RADIUS_CIRCLE;
  d.bg_color = c;
  lv_area_t a = {(int32_t)x - r, (int32_t)y - r, (int32_t)x + r, (int32_t)y + r};
  lv_draw_rect(L, &d, &a);
}

static void drawHands(lv_event_t* e) {
  lv_layer_t* L = lv_event_get_layer(e);
  bool valid = timeValid();
  struct timeval tv;
  gettimeofday(&tv, nullptr);
  struct tm t;
  localtime_r(&tv.tv_sec, &t);
  float sec = valid ? t.tm_sec + tv.tv_usec / 1e6f : 0;
  float min = valid ? t.tm_min + sec / 60 : 8;
  float hr = valid ? (t.tm_hour % 12) + min / 60 : 10;

  // kleine Sekunde: feiner Zeiger mit Gegengewicht
  lv_draw_line_dsc_t ln;
  lv_draw_line_dsc_init(&ln);
  ln.color = C_RED();
  ln.width = 2;
  ln.round_start = ln.round_end = 1;
  ln.p1 = polar(CX, SUB_Y, sec * 6 + 180, 12);
  ln.p2 = polar(CX, SUB_Y, sec * 6, SUB_R - 6);
  lv_draw_line(L, &ln);
  dot(L, CX, SUB_Y, 4, C_RED());

  leaf(L, hr * 30, 112, 22, 16, C_BRASS());
  leaf(L, min * 6, 176, 26, 12, C_BRASS());
  dot(L, CX, CY, 10, C_BRASS());
  dot(L, CX, CY, 3, lv_color_hex(0x3A2F1C));
}

static void updateTexts() {
  static const char* WD[] = {"So", "Mo", "Di", "Mi", "Do", "Fr", "Sa"};
  static const char* MO[] = {"Jan", "Feb", "Mär", "Apr", "Mai", "Jun", "Jul", "Aug", "Sep", "Okt", "Nov", "Dez"};
  char b[80];
  if (timeValid()) {
    time_t now = time(nullptr);
    struct tm t;
    localtime_r(&now, &t);
    snprintf(b, sizeof(b), "%s · %d. %s", WD[t.tm_wday], t.tm_mday, MO[t.tm_mon]);
  } else {
    snprintf(b, sizeof(b), "Uhrzeit unbekannt");
  }
  lv_label_set_text(dateL, b);

  // Komplikation: laufender Timer, sonst Musik
  music::State m = music::state();
  if (cw::timerRunning()) {
    char tt[16];
    fmtTime(tt, sizeof(tt), cw::timerLeftMs(), false);
    snprintf(b, sizeof(b), I_HOURGLASS " %s", tt);
    lv_obj_set_style_text_color(compL, C_TIMER(), 0);
  } else if (cw::swRunning()) {
    char tt[16];
    fmtTime(tt, sizeof(tt), cw::swMs(), false);
    snprintf(b, sizeof(b), I_STOPWATCH " %s", tt);
    lv_obj_set_style_text_color(compL, C_WATCH(), 0);
  } else if (m.active) {
    snprintf(b, sizeof(b), "%s %s", m.playing ? I_MUSIC : I_PAUSE, m.title);
    lv_obj_set_style_text_color(compL, C_MUSIC(), 0);
  } else {
    b[0] = 0;
  }
  lv_label_set_text(compL, b);

  int bat = hwBattery();
  if (bat < 0) snprintf(b, sizeof(b), "%s", hwUsb() ? I_BOLT : "");
  else snprintf(b, sizeof(b), "%s%d%%", hwCharging() ? I_BOLT : "", bat);
  lv_label_set_text(batL, b);
  lv_obj_set_style_text_color(batL, bat >= 0 && bat < 15 && !hwCharging() ? C_RED() : C_DIM(), 0);
  lv_label_set_text(netL, netOnline() ? I_WIFI : I_WIFI_OFF);
}

static void buildPocket(lv_obj_t* scr) {
  if (!dialBuf) dialBuf = (uint16_t*)heap_caps_malloc(W * H * 2, MALLOC_CAP_SPIRAM);
  lv_obj_t* canvas = lv_canvas_create(scr);
  lv_canvas_set_buffer(canvas, dialBuf, W, H, LV_COLOR_FORMAT_RGB565);
  lv_obj_set_pos(canvas, 0, 0);
  if (!dialReady) { drawDial(canvas); dialReady = true; }

  dateL = label(scr, &font_text_20, C_DIM(), "");
  lv_obj_align(dateL, LV_ALIGN_TOP_MID, 0, 112);
  compL = label(scr, &font_text_20, C_MUSIC(), "");
  lv_label_set_long_mode(compL, LV_LABEL_LONG_DOT);
  lv_obj_set_width(compL, 220);
  lv_obj_set_style_text_align(compL, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(compL, LV_ALIGN_TOP_MID, 0, 146);
  batL = label(scr, &font_text_20, C_DIM(), "");
  lv_obj_align(batL, LV_ALIGN_CENTER, -96, 0);
  netL = label(scr, &font_text_20, C_DIM(), "");
  lv_obj_align(netL, LV_ALIGN_CENTER, 96, 0);

  hands = lv_obj_create(scr);
  lv_obj_remove_style_all(hands);
  lv_obj_set_size(hands, W, H);
  lv_obj_add_flag(hands, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(hands, drawHands, LV_EVENT_DRAW_MAIN, nullptr);
  // Antippen öffnet die Apps, die Komplikation führt direkt zur Musik bzw. zum Timer
  lv_obj_add_event_cb(hands, [](lv_event_t*) {
    if (bezelMoved()) return;  // war Drehen am Rand
    lv_point_t p;
    lv_indev_get_point(lv_indev_active(), &p);
    if (p.y > 130 && p.y < 190 && abs(p.x - CX) < 120 && lv_label_get_text(compL)[0]) {
      if (cw::timerRunning()) push(&PAGE_TIMER);
      else if (cw::swRunning()) push(&PAGE_STOPWATCH);
      else push(&PAGE_MUSIC);
    }
  }, LV_EVENT_SHORT_CLICKED, nullptr);
  lv_obj_add_event_cb(hands, [](lv_event_t*) { if (!bezelMoved()) push(&PAGE_FACES); }, LV_EVENT_LONG_PRESSED, nullptr);
  bezel(hands, []() { return music::state().active; });
  lastSec = -1;
  updateTexts();
}

static void updatePocket() {
  struct timeval tv;
  gettimeofday(&tv, nullptr);
  int s = tv.tv_sec;
  if (s != lastSec) {
    lastSec = s;
    lv_obj_invalidate(hands);  // Stunden- und Minutenzeiger wandern mit
    updateTexts();
  } else if (timeValid()) {
    lv_area_t a = {CX - SUB_R, SUB_Y - SUB_R, CX + SUB_R, SUB_Y + SUB_R};
    lv_obj_invalidate_area(hands, &a);  // kleine Sekunde läuft flüssig
  }
}


// ================================================================== Zifferblätter aus FaceLab

static const fc::Face* const FACES[] = {nullptr, &fc::FACE_SUN, &fc::FACE_STERNE, &fc::FACE_KTEXT, &fc::FACE_GLITCH};
static const int NFACES = sizeof(FACES) / sizeof(FACES[0]);
static int shown = -1;
static lv_obj_t* faceObj = nullptr;
static uint32_t wakeAt = 0, lastDraw = 0;
static int wakeN = 0;
static fc::Data fd;

// Abstand zur UTC in Minuten (mit Sommerzeit)
static int tzMinutes(time_t now) {
  struct tm lt, gt;
  localtime_r(&now, &lt);
  gmtime_r(&now, &gt);
  gt.tm_isdst = lt.tm_isdst;
  return (int)((now - mktime(&gt)) / 60);
}

static void fillData(int face) {
  static const char* DOW[] = {"SO", "MO", "DI", "MI", "DO", "FR", "SA"};
  static const char* MON[] = {"JAN", "FEB", "MÄR", "APR", "MAI", "JUN", "JUL", "AUG", "SEP", "OKT", "NOV", "DEZ"};
  struct timeval tv;
  gettimeofday(&tv, nullptr);
  struct tm t;
  localtime_r(&tv.tv_sec, &t);
  fc::Data& d = fd;
  d.timeOk = timeValid();
  d.h = t.tm_hour; d.m = t.tm_min; d.s = t.tm_sec; d.ms = tv.tv_usec / 1000;
  d.mins = d.h * 60 + d.m;
  d.day = t.tm_mday; d.mon = t.tm_mon + 1; d.dow = t.tm_wday; d.doy = t.tm_yday + 1; d.year = t.tm_year + 1900;
  char wk[4];
  strftime(wk, sizeof(wk), "%V", &t);
  d.wk = atoi(wk);
  d.dowStr = DOW[d.dow];
  d.monStr = MON[t.tm_mon];
  d.unix = tv.tv_sec;
  d.lat = weather::lat();
  d.lon = weather::lon();
  d.locOk = weather::located();
  d.tz = tzMinutes(tv.tv_sec);
  d.moon = fc::moonPhase((double)tv.tv_sec);
  d.batt = hwBattery();
  d.charging = hwCharging();
  weather::Now w = weather::now();
  d.weatherOk = w.ok;
  d.temp = w.temp;
  d.wcode = w.code;
  d.rain = w.rain;
  radio::Peer ps[12];
  d.peers = radio::peers(ps, 12);
  d.music = music::state().playing;
  d.level = audio::outLevel();
  d.accent = 0xffb03a;
  d.dcol = 0xffffff;
  d.wakeN = wakeN;
  char temp[16], bat[16];
  if (w.ok) snprintf(temp, sizeof(temp), "%d°", (int)lroundf(w.temp)); else snprintf(temp, sizeof(temp), "--");
  if (d.batt >= 0) snprintf(bat, sizeof(bat), "%d%%", d.batt); else snprintf(bat, sizeof(bat), "USB");
  auto slot = [&](int i, const char* l, const char* v) {
    snprintf(d.slots[i].lbl, sizeof(d.slots[i].lbl), "%s", l);
    snprintf(d.slots[i].val, sizeof(d.slots[i].val), "%s", v);
  };
  char rain[8], peers[8];
  snprintf(rain, sizeof(rain), w.ok ? "%d%%" : "--", w.rain);
  snprintf(peers, sizeof(peers), "%d", d.peers);
  if (FACES[face] == &fc::FACE_KTEXT) slot(0, w.ok ? weather::text(w.code) : "", w.ok ? temp : "");
  else if (FACES[face] == &fc::FACE_GLITCH) { slot(0, "TEMP", temp); slot(1, "AKKU", bat); slot(2, "FUNK", peers); }
  else { slot(0, "TEMP", temp); slot(1, "AKKU", bat); slot(2, "REGEN", rain); }
}

// Zwischenspeicher für aufwendige Designs: nach dem Intro einmal pro Minute in einen Puffer zeichnen
// (teilt sich den Puffer mit dem Zifferblatt der Taschenuhr, es ist ja immer nur eins sichtbar)
static lv_obj_t* cacheCanvas = nullptr;
static bool cached = false;
static int cacheMin = -1;

static void renderCache() {
  lv_canvas_fill_bg(cacheCanvas, lv_color_black(), LV_OPA_COVER);
  lv_layer_t layer;
  lv_canvas_init_layer(cacheCanvas, &layer);
  fc::Dc dc;
  dc.L = &layer;
  fc::Anim an = {1.0f, false, 0, 0};
  FACES[shown]->draw(dc, fd, an);
  lv_canvas_finish_layer(cacheCanvas, &layer);
  cached = true;
  cacheMin = fd.mins;
  lv_obj_remove_flag(cacheCanvas, LV_OBJ_FLAG_HIDDEN);
  lv_obj_invalidate(faceObj);
}

static void drawFace(lv_event_t* e) {
  if (shown <= 0 || cached) return;
  fc::Dc dc;
  dc.L = lv_event_get_layer(e);
  fc::Anim an;
  an.t = millis() - wakeAt;
  an.intro = fc::clamp(an.t / 1700.0f, 0, 1);
  an.live = true;
  an.ms = fd.ms;
  FACES[shown]->draw(dc, fd, an);
}

static void buildFace(lv_obj_t* scr, int f) {
  cacheCanvas = nullptr;
  cached = false;
  cacheMin = -1;
  if (FACES[f]->cache) {
    if (!dialBuf) dialBuf = (uint16_t*)heap_caps_malloc(W * H * 2, MALLOC_CAP_SPIRAM);
    dialReady = false;  // der Puffer gehört jetzt dem Zwischenspeicher
    cacheCanvas = lv_canvas_create(scr);
    lv_canvas_set_buffer(cacheCanvas, dialBuf, W, H, LV_COLOR_FORMAT_RGB565);
    lv_obj_add_flag(cacheCanvas, LV_OBJ_FLAG_HIDDEN);
  }
  faceObj = lv_obj_create(scr);
  lv_obj_remove_style_all(faceObj);
  lv_obj_set_size(faceObj, W, H);
  lv_obj_add_flag(faceObj, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(faceObj, drawFace, LV_EVENT_DRAW_MAIN, nullptr);
  lv_obj_add_event_cb(faceObj, [](lv_event_t*) { if (!bezelMoved()) push(&PAGE_FACES); }, LV_EVENT_LONG_PRESSED, nullptr);
  bezel(faceObj, []() { return music::state().active; });
  fillData(f);
}

static void build(lv_obj_t* scr) {
  int f = cfg.face < NFACES ? cfg.face : 0;
  shown = f;
  wakeAt = millis();
  wakeN++;
  faceObj = nullptr;
  if (f == 0) buildPocket(scr);
  else buildFace(scr, f);
}

static void update() {
  int f = cfg.face < NFACES ? cfg.face : 0;
  if (f != shown) {  // anderes Zifferblatt gewählt
    lv_obj_t* scr = topScreen();
    lv_obj_clean(scr);
    build(scr);
    return;
  }
  if (f == 0) { updatePocket(); return; }
  // Intro flüssig, danach so oft wie das Design es braucht
  uint32_t now = millis();
  bool intro = now - wakeAt < 1800;
  if (cacheCanvas && !intro) {  // ruhendes Bild aus dem Zwischenspeicher, einmal pro Minute neu
    fillData(f);
    if (!cached || fd.mins != cacheMin) renderCache();
    return;
  }
  int fps = FACES[f]->fps;
  if (FACES[f] == &fc::FACE_GLITCH && !music::state().playing) fps = 3;  // ohne Musik ruhig
  uint32_t every = intro ? 0 : 1000 / fps;
  if (now - lastDraw >= every) {
    lastDraw = now;
    fillData(f);
    lv_obj_invalidate(faceObj);
  }
}

// Aufwachen: Intro neu abspielen (wie beim Handgelenk-Heben)
static void resume() {
  wakeAt = millis();
  wakeN++;
  if (cacheCanvas) { cached = false; lv_obj_add_flag(cacheCanvas, LV_OBJ_FLAG_HIDDEN); }
}

const Page PAGE_WATCH = {build, update, nullptr, resume, true};

// ================================================================== Zifferblatt wählen

static void facesBuild(lv_obj_t* scr) {
  lv_obj_t* l = roundList(scr);
  lv_obj_t* t = label(l, &font_text_24, C_BRASS(), "Zifferblatt");
  lv_obj_set_style_text_align(t, LV_TEXT_ALIGN_CENTER, 0);
  for (int i = 0; i < NFACES; i++) {
    const char* name = i ? FACES[i]->name : "Taschenuhr";
    const char* sub = i ? FACES[i]->blurb : "Analog mit kleiner Sekunde";
    lv_obj_t* r = listRow(l, cfg.face == i ? I_CHECK : I_WATCH, cfg.face == i ? C_BRASS() : C_DIM(), name, sub);
    lv_obj_add_event_cb(r, [](lv_event_t* e) {
      cfg.face = (int)(intptr_t)lv_event_get_user_data(e);
      settingsSave();
      play(1);
      home();
    }, LV_EVENT_CLICKED, (void*)(intptr_t)i);
  }
  lv_obj_t* h = label(l, &font_text_20, C_DIM(), "Auf der Uhr lange drücken, um zu wechseln");
  lv_label_set_long_mode(h, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_align(h, LV_TEXT_ALIGN_CENTER, 0);
  listFit(l);
}

const Page PAGE_FACES = {facesBuild, nullptr, nullptr, nullptr, false};
