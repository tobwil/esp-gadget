// Kacheln wie bei Wear OS: vom Zifferblatt nach links wischen, dann seitlich blättern.
// Musik · Wetter · Timer · Funk – jeweils mit Direktbedienung; Antippen öffnet die ganze App.
// Auf der ersten Kachel nach rechts wischen führt zurück zum Zifferblatt.
#include <math.h>
#include "../audio.h"
#include "../clockwork.h"
#include "../music.h"
#include "../radio.h"
#include "../settings.h"
#include "../weather.h"
#include "ui.h"

using namespace ui;

static const int NT = 4;
static lv_obj_t *pager, *dots[NT];
// Musik
static lv_obj_t *mCover, *mPh, *mTitle, *mArtist, *mPlay, *mCtrl, *mEmpty;
static lv_image_dsc_t mDsc;
static uint32_t mArtSeen;
// Wetter
static lv_obj_t *wIcon, *wTemp, *wDeg, *wText, *wMore, *wCity;
// Timer
static lv_obj_t *tRing, *tBig, *tRun, *tSet, *tPause;
// Funk
static lv_obj_t *fStatus, *fPtt, *fHalo, *fHint;
static bool fPressing = false;
static int tilesStart = 0;

void tilesOpen(int idx) {
  tilesStart = idx;
  push(&PAGE_TILES);
}

static lv_obj_t* tile(lv_obj_t* parent) {
  lv_obj_t* t = lv_obj_create(parent);
  lv_obj_remove_style_all(t);
  lv_obj_set_size(t, W, H);
  lv_obj_remove_flag(t, LV_OBJ_FLAG_SCROLLABLE);
  return t;
}

static lv_obj_t* tileTitle(lv_obj_t* t, const char* text, lv_color_t c) {
  lv_obj_t* l = label(t, &font_text_20, c, text);
  lv_obj_align(l, LV_ALIGN_TOP_MID, 0, 40);
  return l;
}

static void openOnTap(lv_obj_t* o, const Page* p) {
  lv_obj_add_flag(o, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(o, [](lv_event_t* e) {
    const Page* p = (const Page*)lv_event_get_user_data(e);
    if (p == &PAGE_MUSIC && !music::state().active) browseOpen("home");
    else push(p);
  }, LV_EVENT_CLICKED, (void*)p);
}

static void buildMusic(lv_obj_t* t) {
  tileTitle(t, I_MUSIC "  Musik", C_MUSIC());
  mPh = lv_obj_create(t);
  lv_obj_set_size(mPh, 150, 150);
  lv_obj_align(mPh, LV_ALIGN_TOP_MID, 0, 74);
  lv_obj_set_style_radius(mPh, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(mPh, C_SURF(), 0);
  lv_obj_set_style_border_width(mPh, 0, 0);
  lv_obj_t* ic = label(mPh, &font_icon_64, C_SURF2(), I_ALBUM);
  lv_obj_center(ic);
  openOnTap(mPh, &PAGE_MUSIC);
  mCover = lv_image_create(t);
  lv_image_set_scale(mCover, 256 * 150 / music::ART);
  lv_obj_set_size(mCover, 150, 150);
  lv_image_set_inner_align(mCover, LV_IMAGE_ALIGN_CENTER);
  lv_obj_align(mCover, LV_ALIGN_TOP_MID, 0, 74);
  lv_obj_add_flag(mCover, LV_OBJ_FLAG_HIDDEN);
  openOnTap(mCover, &PAGE_MUSIC);
  mTitle = label(t, &font_text_24, C_TEXT(), "");
  lv_label_set_long_mode(mTitle, LV_LABEL_LONG_SCROLL_CIRCULAR);
  lv_obj_set_width(mTitle, 320);
  lv_obj_set_style_text_align(mTitle, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(mTitle, LV_ALIGN_TOP_MID, 0, 238);
  mArtist = label(t, &font_text_20, C_DIM(), "");
  lv_label_set_long_mode(mArtist, LV_LABEL_LONG_DOT);
  lv_obj_set_size(mArtist, 280, lv_font_get_line_height(&font_text_20));
  lv_obj_set_style_text_align(mArtist, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(mArtist, LV_ALIGN_TOP_MID, 0, 270);
  mCtrl = lv_obj_create(t);
  lv_obj_remove_style_all(mCtrl);
  lv_obj_set_size(mCtrl, 280, 90);
  lv_obj_align(mCtrl, LV_ALIGN_TOP_MID, 0, 304);
  lv_obj_set_flex_flow(mCtrl, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(mCtrl, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(mCtrl, 16, 0);
  lv_obj_t* pv = roundButton(mCtrl, I_PREV, 58, C_SURF(), C_TEXT());
  lv_obj_add_event_cb(pv, [](lv_event_t*) { music::prev(); }, LV_EVENT_CLICKED, nullptr);
  mPlay = roundButton(mCtrl, I_PAUSE, 80, C_MUSIC(), C_BG(), &font_icon_64);
  lv_obj_add_event_cb(mPlay, [](lv_event_t*) { music::toggle(); }, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* nx = roundButton(mCtrl, I_NEXT, 58, C_SURF(), C_TEXT());
  lv_obj_add_event_cb(nx, [](lv_event_t*) { music::next(); }, LV_EVENT_CLICKED, nullptr);
  mEmpty = pill(t, I_LIBRARY "  Bibliothek", C_MUSIC(), C_BG());
  lv_obj_align(mEmpty, LV_ALIGN_TOP_MID, 0, 300);
  lv_obj_add_event_cb(mEmpty, [](lv_event_t*) { browseOpen("home"); }, LV_EVENT_CLICKED, nullptr);
  bezel(t, []() { return music::state().active; });
  mArtSeen = 0xFFFFFFFF;
}

static void updateMusic() {
  music::State s = music::state();
  hide(mCtrl, !s.active);
  hide(mEmpty, s.active);
  const char* title = s.active ? s.title : "Gerade läuft nichts";
  if (strcmp(lv_label_get_text(mTitle), title)) lv_label_set_text(mTitle, title);
  const char* sub = !s.active ? "" : s.buffering ? "Lädt …" : s.artist;
  if (strcmp(lv_label_get_text(mArtist), sub)) lv_label_set_text(mArtist, sub);
  lv_label_set_text(lv_obj_get_child(mPlay, 0), s.playing ? I_PAUSE : I_PLAY);
  uint32_t v;
  const uint16_t* px = music::art(&v);
  if (!s.active) { hide(mCover, true); hide(mPh, false); return; }
  if (px && v != mArtSeen) {
    mArtSeen = v;
    mDsc.header.magic = LV_IMAGE_HEADER_MAGIC;
    mDsc.header.cf = LV_COLOR_FORMAT_RGB565;
    mDsc.header.w = music::ART;
    mDsc.header.h = music::ART;
    mDsc.header.stride = music::ART * 2;
    mDsc.data = (const uint8_t*)px;
    mDsc.data_size = music::ART * music::ART * 2;
    lv_image_cache_drop(&mDsc);
    lv_image_set_src(mCover, &mDsc);
    hide(mCover, false);
    hide(mPh, true);
  }
}

static void buildWeather(lv_obj_t* t) {
  tileTitle(t, I_SUN "  Wetter", C_TIMER());
  wIcon = label(t, &font_icon_64, C_TIMER(), "");
  lv_obj_align(wIcon, LV_ALIGN_TOP_MID, 0, 82);
  wTemp = label(t, &font_num_80, C_TEXT(), "");
  lv_obj_align(wTemp, LV_ALIGN_TOP_MID, -8, 150);
  wDeg = label(t, &font_text_32, C_TEXT(), "°");
  wText = label(t, &font_text_24, C_TEXT(), "");
  lv_obj_align(wText, LV_ALIGN_TOP_MID, 0, 250);
  wMore = label(t, &font_text_20, C_DIM(), "");
  lv_obj_align(wMore, LV_ALIGN_TOP_MID, 0, 290);
  wCity = label(t, &font_text_20, C_DIM(), "");
  lv_obj_align(wCity, LV_ALIGN_TOP_MID, 0, 330);
}

static void updateWeather() {
  weather::Now w = weather::now();
  char b[64];
  time_t now = time(nullptr);
  struct tm tm;
  localtime_r(&now, &tm);
  bool day = tm.tm_hour >= 7 && tm.tm_hour < 19;
  if (!w.ok) {
    lv_label_set_text(wIcon, I_CLOUD);
    lv_label_set_text(wTemp, "--");
    lv_label_set_text(wText, "Noch keine Daten");
    lv_label_set_text(wMore, "Braucht WLAN");
  } else {
    lv_label_set_text(wIcon, weather::icon(w.code, day));
    snprintf(b, sizeof(b), "%d", (int)lroundf(w.temp));
    lv_label_set_text(wTemp, b);
    lv_label_set_text(wText, weather::text(w.code));
    snprintf(b, sizeof(b), "%d° / %d° · Regen %d %% · Wind %d", (int)lroundf(w.tmax), (int)lroundf(w.tmin), w.rain,
             (int)lroundf(w.wind));
    lv_label_set_text(wMore, b);
  }
  lv_obj_align_to(wDeg, wTemp, LV_ALIGN_OUT_RIGHT_TOP, 2, 6);
  snprintf(b, sizeof(b), I_LOCATION " %s", weather::city()[0] ? weather::city() : "Standort unbekannt");
  lv_label_set_text(wCity, b);
}

static void buildTimer(lv_obj_t* t) {
  tRing = lv_arc_create(t);
  lv_obj_set_size(tRing, 440, 440);
  lv_obj_center(tRing);
  lv_arc_set_rotation(tRing, 270);
  lv_arc_set_bg_angles(tRing, 0, 360);
  lv_arc_set_range(tRing, 0, 1000);
  lv_obj_set_style_arc_width(tRing, 10, LV_PART_MAIN);
  lv_obj_set_style_arc_width(tRing, 10, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(tRing, C_SURF(), LV_PART_MAIN);
  lv_obj_set_style_arc_color(tRing, C_TIMER(), LV_PART_INDICATOR);
  lv_obj_set_style_opa(tRing, LV_OPA_TRANSP, LV_PART_KNOB);
  lv_obj_remove_flag(tRing, LV_OBJ_FLAG_CLICKABLE);
  tileTitle(t, I_HOURGLASS "  Timer", C_TIMER());

  // läuft: große Restzeit, Pause
  tRun = lv_obj_create(t);
  lv_obj_remove_style_all(tRun);
  lv_obj_set_size(tRun, W, 300);
  lv_obj_align(tRun, LV_ALIGN_TOP_MID, 0, 120);
  lv_obj_remove_flag(tRun, LV_OBJ_FLAG_SCROLLABLE);
  tBig = label(tRun, &font_num_80, C_TEXT(), "");
  lv_obj_align(tBig, LV_ALIGN_TOP_MID, 0, 20);
  openOnTap(tBig, &PAGE_TIMER);
  tPause = roundButton(tRun, I_PAUSE, 80, C_TIMER(), C_BG(), &font_icon_64);
  lv_obj_align(tPause, LV_ALIGN_TOP_MID, 0, 150);
  lv_obj_add_event_cb(tPause, [](lv_event_t*) { if (cw::timerPaused()) cw::timerResume(); else cw::timerPause(); },
                      LV_EVENT_CLICKED, nullptr);

  // steht: Schnellstart
  tSet = lv_obj_create(t);
  lv_obj_remove_style_all(tSet);
  lv_obj_set_size(tSet, 340, 260);
  lv_obj_align(tSet, LV_ALIGN_TOP_MID, 0, 110);
  lv_obj_set_flex_flow(tSet, LV_FLEX_FLOW_ROW_WRAP);
  lv_obj_set_flex_align(tSet, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_gap(tSet, 14, 0);
  static const int MINS[] = {1, 3, 5, 10, 15, 25};
  for (int i = 0; i < 6; i++) {
    char b[8];
    snprintf(b, sizeof(b), "%d", MINS[i]);
    lv_obj_t* bt = roundButton(tSet, b, 84, C_SURF(), C_TIMER(), &font_num_44);
    lv_obj_add_event_cb(bt, [](lv_event_t* e) {
      cw::timerStart((int)(intptr_t)lv_event_get_user_data(e) * 60);
      play(1);
    }, LV_EVENT_CLICKED, (void*)(intptr_t)MINS[i]);
  }
  lv_obj_t* h = label(tSet, &font_text_20, C_DIM(), "Minuten");
  lv_obj_set_width(h, 300);
  lv_obj_set_style_text_align(h, LV_TEXT_ALIGN_CENTER, 0);
}

static void updateTimer() {
  bool run = cw::timerRunning();
  hide(tRun, !run);
  hide(tSet, run);
  hide(tRing, !run);
  if (!run) return;
  uint32_t left = cw::timerLeftMs(), total = cw::timerTotalS() * 1000;
  char b[16];
  fmtTime(b, sizeof(b), left + 999, false);
  lv_label_set_text(tBig, b);
  lv_arc_set_value(tRing, total ? (int)((uint64_t)left * 1000 / total) : 0);
  lv_label_set_text(lv_obj_get_child(tPause, 0), cw::timerPaused() ? I_PLAY : I_PAUSE);
}

static void buildRadio(lv_obj_t* t) {
  tileTitle(t, I_RADIO "  Funk", C_RADIO());
  fStatus = label(t, &font_text_20, C_DIM(), "");
  lv_obj_align(fStatus, LV_ALIGN_TOP_MID, 0, 74);
  fHalo = lv_obj_create(t);
  lv_obj_set_style_radius(fHalo, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_border_width(fHalo, 0, 0);
  lv_obj_set_style_bg_color(fHalo, C_RADIO(), 0);
  lv_obj_set_style_bg_opa(fHalo, LV_OPA_30, 0);
  lv_obj_remove_flag(fHalo, (lv_obj_flag_t)(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));
  fPtt = lv_obj_create(t);
  lv_obj_set_size(fPtt, 170, 170);
  lv_obj_align(fPtt, LV_ALIGN_CENTER, 0, 10);
  lv_obj_set_style_radius(fPtt, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_border_width(fPtt, 0, 0);
  lv_obj_set_style_bg_color(fPtt, C_RADIO(), 0);
  lv_obj_set_style_bg_color(fPtt, C_RED(), LV_STATE_PRESSED);
  lv_obj_remove_flag(fPtt, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_SCROLL_CHAIN));
  lv_obj_t* m = label(fPtt, &font_icon_64, C_BG(), I_MIC);
  lv_obj_center(m);
  lv_obj_add_event_cb(fPtt, [](lv_event_t*) { fPressing = true; radio::pttDown(0); }, LV_EVENT_PRESSED, nullptr);
  for (lv_event_code_t c : {LV_EVENT_RELEASED, LV_EVENT_PRESS_LOST})
    lv_obj_add_event_cb(fPtt, [](lv_event_t*) { if (fPressing) { fPressing = false; radio::pttUp(); } }, c, nullptr);
  fHint = label(t, &font_text_20, C_DIM(), "");
  lv_obj_align(fHint, LV_ALIGN_CENTER, 0, 124);
  lv_obj_t* more = pill(t, "Funk öffnen", C_SURF(), C_TEXT());
  lv_obj_align(more, LV_ALIGN_BOTTOM_MID, 0, -34);
  lv_obj_add_event_cb(more, [](lv_event_t*) { push(&PAGE_RADIO); }, LV_EVENT_CLICKED, nullptr);
}

static void updateRadio() {
  radio::Peer ps[12];
  int n = radio::peers(ps, 12);
  char b[64];
  if (n == 0) snprintf(b, sizeof(b), "Niemand in Reichweite");
  else if (n == 1) snprintf(b, sizeof(b), "%s ist da", ps[0].name);
  else snprintf(b, sizeof(b), "%d Geräte in Reichweite", n);
  lv_label_set_text(fStatus, b);
  float lvl = 0;
  if (radio::transmitting()) { snprintf(b, sizeof(b), "Sendet an alle …"); lvl = audio::micLevel(); }
  else if (radio::receiving()) { snprintf(b, sizeof(b), "%s spricht", radio::talker()); lvl = audio::outLevel(); }
  else snprintf(b, sizeof(b), "Halten = an alle funken");
  lv_label_set_text(fHint, b);
  int s = 170 + (int)(fminf(1.0f, lvl * 1.5f) * 90);
  lv_obj_set_size(fHalo, s, s);
  lv_obj_align(fHalo, LV_ALIGN_CENTER, 0, 10);
}

static void scrolled(lv_event_t*) {
  int k = (lv_obj_get_scroll_x(pager) + W / 2) / W;
  for (int i = 0; i < NT; i++) lv_obj_set_style_bg_opa(dots[i], i == k ? LV_OPA_COVER : LV_OPA_30, 0);
}

static void build(lv_obj_t* scr) {
  pager = lv_obj_create(scr);
  lv_obj_remove_style_all(pager);
  lv_obj_set_size(pager, W, H);
  lv_obj_set_flex_flow(pager, LV_FLEX_FLOW_ROW);
  lv_obj_set_scroll_dir(pager, LV_DIR_HOR);
  lv_obj_set_scroll_snap_x(pager, LV_SCROLL_SNAP_CENTER);
  lv_obj_add_flag(pager, LV_OBJ_FLAG_SCROLL_ONE);
  lv_obj_remove_flag(pager, LV_OBJ_FLAG_SCROLL_ELASTIC);  // am Anfang: nach rechts wischen = zurück
  lv_obj_set_scrollbar_mode(pager, LV_SCROLLBAR_MODE_OFF);
  lv_obj_add_event_cb(pager, scrolled, LV_EVENT_SCROLL, nullptr);
  buildMusic(tile(pager));
  buildWeather(tile(pager));
  buildTimer(tile(pager));
  buildRadio(tile(pager));
  // Seitenpunkte unten
  for (int i = 0; i < NT; i++) {
    dots[i] = lv_obj_create(scr);
    lv_obj_set_size(dots[i], 8, 8);
    lv_obj_set_style_radius(dots[i], 4, 0);
    lv_obj_set_style_border_width(dots[i], 0, 0);
    lv_obj_set_style_bg_color(dots[i], C_TEXT(), 0);
    lv_obj_align(dots[i], LV_ALIGN_BOTTOM_MID, (i - (NT - 1) / 2.0f) * 16, -14);
    lv_obj_remove_flag(dots[i], LV_OBJ_FLAG_CLICKABLE);
  }
  lv_obj_update_layout(pager);
  lv_obj_scroll_to_x(pager, tilesStart * W, LV_ANIM_OFF);
  tilesStart = 0;
  scrolled(nullptr);
  updateMusic();
  updateWeather();
  updateTimer();
  updateRadio();
}

static void update() {
  updateMusic();
  updateTimer();
  updateRadio();
  static uint32_t at = 0;
  if (millis() - at > 5000) { at = millis(); updateWeather(); }
}

static void leave() {
  if (fPressing) { fPressing = false; radio::pttUp(); }
}

const Page PAGE_TILES = {build, update, leave, nullptr, false, ENTER_RIGHT};
