// Timer, Stoppuhr und Wecker.
#include "../clockwork.h"
#include "../settings.h"
#include "../favs.h"
#include "../music.h"
#include "ui.h"

using namespace ui;

static char opts60[60 * 3 + 1], opts100[100 * 3 + 1], opts24[24 * 3 + 1];

static void buildOpts() {
  if (opts60[0]) return;
  char* p = opts100;
  for (int i = 0; i < 100; i++) p += sprintf(p, i ? "\n%02d" : "%02d", i);
  p = opts60;
  for (int i = 0; i < 60; i++) p += sprintf(p, i ? "\n%02d" : "%02d", i);
  p = opts24;
  for (int i = 0; i < 24; i++) p += sprintf(p, i ? "\n%02d" : "%02d", i);
}

static lv_obj_t* roller(lv_obj_t* parent, const char* opts, int sel, lv_color_t accent) {
  lv_obj_t* r = lv_roller_create(parent);
  lv_roller_set_options(r, opts, LV_ROLLER_MODE_INFINITE);
  lv_roller_set_visible_row_count(r, 3);
  lv_roller_set_selected(r, sel, LV_ANIM_OFF);
  lv_obj_set_width(r, 112);
  lv_obj_set_style_bg_opa(r, 0, 0);
  lv_obj_set_style_border_width(r, 0, 0);
  lv_obj_set_style_text_font(r, &font_num_44, 0);
  lv_obj_set_style_text_color(r, lv_color_hex(0x55524C), 0);
  lv_obj_set_style_text_line_space(r, 18, 0);
  lv_obj_set_style_text_align(r, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_bg_color(r, C_SURF(), LV_PART_SELECTED);
  lv_obj_set_style_bg_opa(r, LV_OPA_COVER, LV_PART_SELECTED);
  lv_obj_set_style_radius(r, 18, LV_PART_SELECTED);
  lv_obj_set_style_text_color(r, accent, LV_PART_SELECTED);
  lv_obj_set_style_text_font(r, &font_num_44, LV_PART_SELECTED);
  lv_obj_remove_flag(r, LV_OBJ_FLAG_GESTURE_BUBBLE);
  return r;
}

static lv_obj_t* edgeArc(lv_obj_t* scr, lv_color_t c) {
  lv_obj_t* a = lv_arc_create(scr);
  lv_obj_set_size(a, 452, 452);
  lv_obj_center(a);
  lv_arc_set_rotation(a, 270);
  lv_arc_set_bg_angles(a, 0, 360);
  lv_arc_set_range(a, 0, 1000);
  lv_obj_set_style_arc_width(a, 10, LV_PART_MAIN);
  lv_obj_set_style_arc_width(a, 10, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(a, C_SURF(), LV_PART_MAIN);
  lv_obj_set_style_arc_color(a, c, LV_PART_INDICATOR);
  lv_obj_set_style_opa(a, LV_OPA_TRANSP, LV_PART_KNOB);
  lv_obj_remove_flag(a, LV_OBJ_FLAG_CLICKABLE);
  return a;
}

static lv_obj_t* box(lv_obj_t* scr) {
  lv_obj_t* b = lv_obj_create(scr);
  lv_obj_remove_style_all(b);
  lv_obj_set_size(b, W, H);
  lv_obj_remove_flag(b, LV_OBJ_FLAG_SCROLLABLE);
  return b;
}

// ================================================================== Timer

static lv_obj_t *tSet, *tRun, *tMin, *tSec, *tArc, *tBig, *tSub, *tPause;

static void tStart(lv_event_t*) {
  uint32_t s = lv_roller_get_selected(tMin) * 60 + lv_roller_get_selected(tSec);
  if (!s) return;
  cfg.timerS = s;
  settingsSave();
  cw::timerStart(s);
  play(1);
}

static void timerBuild(lv_obj_t* scr) {
  buildOpts();
  tSet = box(scr);
  title(tSet, "Timer", C_TIMER());
  lv_obj_t* row = lv_obj_create(tSet);
  lv_obj_remove_style_all(row);
  lv_obj_set_size(row, 300, 230);
  lv_obj_align(row, LV_ALIGN_CENTER, 0, -6);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(row, 6, 0);
  tMin = roller(row, opts100, cfg.timerS / 60 % 100, C_TIMER());
  label(row, &font_num_44, C_DIM(), ":");
  tSec = roller(row, opts60, cfg.timerS % 60, C_TIMER());
  lv_obj_t* hint = label(tSet, &font_text_20, C_DIM(), "Minuten · Sekunden");
  lv_obj_align(hint, LV_ALIGN_TOP_MID, 0, 90);
  lv_obj_t* go = roundButton(tSet, I_PLAY, 88, C_TIMER(), C_BG(), &font_icon_64);
  lv_obj_align(go, LV_ALIGN_BOTTOM_MID, 0, -30);
  lv_obj_add_event_cb(go, tStart, LV_EVENT_CLICKED, nullptr);

  tRun = box(scr);
  tArc = edgeArc(tRun, C_TIMER());
  tSub = label(tRun, &font_text_20, C_DIM(), "");
  lv_obj_align(tSub, LV_ALIGN_CENTER, 0, -78);
  tBig = label(tRun, &font_num_80, C_TEXT(), "");
  lv_obj_align(tBig, LV_ALIGN_CENTER, 0, -6);
  lv_obj_t* row2 = lv_obj_create(tRun);
  lv_obj_remove_style_all(row2);
  lv_obj_set_size(row2, 260, 100);
  lv_obj_align(row2, LV_ALIGN_BOTTOM_MID, 0, -40);
  lv_obj_set_flex_flow(row2, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row2, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(row2, 26, 0);
  lv_obj_t* rs = roundButton(row2, I_STOP, 70, C_SURF(), C_TEXT());
  lv_obj_add_event_cb(rs, [](lv_event_t*) { cw::timerReset(); }, LV_EVENT_CLICKED, nullptr);
  tPause = roundButton(row2, I_PAUSE, 88, C_TIMER(), C_BG(), &font_icon_64);
  lv_obj_add_event_cb(tPause, [](lv_event_t*) {
    if (cw::timerPaused()) cw::timerResume(); else cw::timerPause();
  }, LV_EVENT_CLICKED, nullptr);
}

static void timerUpdate() {
  bool run = cw::timerRunning();
  if (run) { lv_obj_add_flag(tSet, LV_OBJ_FLAG_HIDDEN); lv_obj_remove_flag(tRun, LV_OBJ_FLAG_HIDDEN); }
  else { lv_obj_remove_flag(tSet, LV_OBJ_FLAG_HIDDEN); lv_obj_add_flag(tRun, LV_OBJ_FLAG_HIDDEN); return; }
  uint32_t left = cw::timerLeftMs(), total = cw::timerTotalS() * 1000;
  char b[16], s[32];
  fmtTime(b, sizeof(b), left + 999, false);
  lv_label_set_text(tBig, b);
  fmtTime(b, sizeof(b), total, false);
  snprintf(s, sizeof(s), cw::timerPaused() ? "Pausiert · %s" : "von %s", b);
  lv_label_set_text(tSub, s);
  lv_arc_set_value(tArc, total ? (int)((uint64_t)left * 1000 / total) : 0);
  lv_label_set_text(lv_obj_get_child(tPause, 0), cw::timerPaused() ? I_PLAY : I_PAUSE);
}

const Page PAGE_TIMER = {timerBuild, timerUpdate, nullptr, nullptr, false};

// ================================================================== Stoppuhr

static lv_obj_t *sArc, *sBig, *sLaps, *sMain, *sSide;

static void swBuild(lv_obj_t* scr) {
  sArc = edgeArc(scr, C_WATCH());
  lv_obj_t* t = label(scr, &font_text_20, C_WATCH(), "Stoppuhr");
  lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 60);
  sBig = label(scr, &font_num_80, C_TEXT(), "");
  lv_obj_align(sBig, LV_ALIGN_CENTER, 0, -34);
  sLaps = label(scr, &font_text_20, C_DIM(), "");
  lv_obj_set_style_text_align(sLaps, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_line_space(sLaps, 4, 0);
  lv_obj_align(sLaps, LV_ALIGN_CENTER, 0, 52);
  lv_obj_t* row = lv_obj_create(scr);
  lv_obj_remove_style_all(row);
  lv_obj_set_size(row, 260, 100);
  lv_obj_align(row, LV_ALIGN_BOTTOM_MID, 0, -34);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(row, 26, 0);
  sSide = roundButton(row, I_REPLAY, 70, C_SURF(), C_TEXT());
  lv_obj_add_event_cb(sSide, [](lv_event_t*) { if (cw::swRunning()) cw::swLap(); else cw::swReset(); play(0); },
                      LV_EVENT_CLICKED, nullptr);
  sMain = roundButton(row, I_PLAY, 88, C_WATCH(), C_BG(), &font_icon_64);
  lv_obj_add_event_cb(sMain, [](lv_event_t*) { cw::swStartStop(); play(0); }, LV_EVENT_CLICKED, nullptr);
}

static void swUpdate() {
  uint32_t ms = cw::swMs();
  char b[16];
  fmtTime(b, sizeof(b), ms, true);
  lv_label_set_text(sBig, b);
  lv_arc_set_value(sArc, (ms % 60000) / 60);
  uint32_t laps[3];
  int n = cw::swLaps(laps, 3);
  char l[120] = "";
  int total = 0;
  {
    uint32_t all[30];
    total = cw::swLaps(all, 30);
  }
  for (int i = 0; i < n; i++) {
    char t[16], line[40];
    fmtTime(t, sizeof(t), laps[i], true);
    snprintf(line, sizeof(line), "%sRunde %d   %s", i ? "\n" : "", total - i, t);
    strncat(l, line, sizeof(l) - strlen(l) - 1);
  }
  lv_label_set_text(sLaps, l);
  lv_label_set_text(lv_obj_get_child(sMain, 0), cw::swRunning() ? I_PAUSE : I_PLAY);
  lv_label_set_text(lv_obj_get_child(sSide, 0), cw::swRunning() ? I_FLAG : I_REPLAY);
}

const Page PAGE_STOPWATCH = {swBuild, swUpdate, nullptr, nullptr, false};

// ================================================================== Wecker

static lv_obj_t *aH, *aM, *aToggle, *aInfo, *aSrc;
extern const ui::Page PAGE_ALARMSRC;

static void aSave(lv_event_t*) {
  cfg.alarmH = lv_roller_get_selected(aH);
  cfg.alarmM = lv_roller_get_selected(aM);
  settingsSave();
}

static void alarmBuild(lv_obj_t* scr) {
  buildOpts();
  title(scr, "Wecker", C_ALARM());
  lv_obj_t* row = lv_obj_create(scr);
  lv_obj_remove_style_all(row);
  lv_obj_set_size(row, 300, 196);
  lv_obj_align(row, LV_ALIGN_CENTER, 0, -30);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(row, 6, 0);
  aH = roller(row, opts24, cfg.alarmH, C_ALARM());
  label(row, &font_num_44, C_DIM(), ":");
  aM = roller(row, opts60, cfg.alarmM, C_ALARM());
  lv_obj_add_event_cb(aH, aSave, LV_EVENT_VALUE_CHANGED, nullptr);
  lv_obj_add_event_cb(aM, aSave, LV_EVENT_VALUE_CHANGED, nullptr);
  aInfo = label(scr, &font_text_20, C_DIM(), "");
  lv_obj_align(aInfo, LV_ALIGN_TOP_MID, 0, 80);
  // Radiowecker: womit geweckt wird
  aSrc = pill(scr, "", C_SURF(), C_TEXT());
  lv_obj_set_width(aSrc, 270);
  lv_label_set_long_mode(lv_obj_get_child(aSrc, 0), LV_LABEL_LONG_DOT);
  lv_obj_set_size(lv_obj_get_child(aSrc, 0), 240, lv_font_get_line_height(&font_text_20));
  lv_obj_set_style_text_align(lv_obj_get_child(aSrc, 0), LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(aSrc, LV_ALIGN_CENTER, 0, 96);
  lv_obj_add_event_cb(aSrc, [](lv_event_t*) { play(0); push(&PAGE_ALARMSRC); }, LV_EVENT_CLICKED, nullptr);
  aToggle = pill(scr, "", C_SURF2(), C_TEXT());
  lv_obj_set_size(aToggle, 210, 60);
  lv_obj_set_style_radius(aToggle, 30, 0);
  lv_obj_align(aToggle, LV_ALIGN_BOTTOM_MID, 0, -34);
  lv_obj_add_event_cb(aToggle, [](lv_event_t*) {
    cfg.alarmOn = !cfg.alarmOn;
    aSave(nullptr);
    play(cfg.alarmOn ? 1 : 0);
  }, LV_EVENT_CLICKED, nullptr);
}

static void alarmUpdate() {
  lv_label_set_text(lv_obj_get_child(aToggle, 0), cfg.alarmOn ? I_ALARM_ON "  Wecker an" : I_ALARM_OFF "  Wecker aus");
  lv_obj_set_style_bg_color(aToggle, cfg.alarmOn ? C_ALARM() : C_SURF2(), 0);
  lv_obj_set_style_text_color(lv_obj_get_child(aToggle, 0), cfg.alarmOn ? C_BG() : C_TEXT(), 0);
  char b[48] = "";
  uint32_t s = cw::alarmInS();
  if (cfg.alarmOn && s) snprintf(b, sizeof(b), "klingelt in %u Std. %u Min.", s / 3600, s / 60 % 60);
  else if (cfg.alarmOn) snprintf(b, sizeof(b), "Uhrzeit unbekannt");
  lv_label_set_text(aInfo, b);
  char s2[80];
  snprintf(s2, sizeof(s2), "%s  %s", cfg.alarmSrc[0] ? I_RADIO : I_BELL, cfg.alarmSrc[0] ? cfg.alarmName : "Wecken mit Ton");
  if (strcmp(lv_label_get_text(lv_obj_get_child(aSrc, 0)), s2)) lv_label_set_text(lv_obj_get_child(aSrc, 0), s2);
}

const Page PAGE_ALARM = {alarmBuild, alarmUpdate, nullptr, nullptr, false};

// ================================================================== Wecken mit …

static music::Item* srcItems = nullptr;
static int srcN = 0;

static void setSrc(const char* src, const char* name) {
  snprintf(cfg.alarmSrc, sizeof(cfg.alarmSrc), "%s", src);
  snprintf(cfg.alarmName, sizeof(cfg.alarmName), "%s", name);
  settingsSave();
  play(1);
  back();
}

static void srcBuild(lv_obj_t* scr) {
  lv_obj_t* l = roundList(scr);
  lv_obj_t* t = label(l, &font_text_24, C_ALARM(), "Wecken mit");
  lv_obj_set_style_text_align(t, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_t* r = listRow(l, I_BELL, C_ALARM(), "Ton", "Klassisch piepen");
  lv_obj_add_event_cb(r, [](lv_event_t*) { setSrc("", ""); }, LV_EVENT_CLICKED, nullptr);
  r = listRow(l, I_RADIO_WAVES, C_ALARM(), "tagesschau in 100 Sekunden", "Neueste Folge");
  lv_obj_add_event_cb(r, [](lv_event_t*) { setSrc("ard:set:10815223", "tagesschau in 100 Sekunden"); }, LV_EVENT_CLICKED, nullptr);
  // gemerkte Sender und Audiothek-Sendungen
  if (!srcItems) srcItems = (music::Item*)heap_caps_malloc(sizeof(music::Item) * favs::MAX, MALLOC_CAP_SPIRAM);
  srcN = favs::list(srcItems, favs::MAX);
  int shown = 0;
  for (int i = 0; i < srcN; i++) {
    const music::Item& it = srcItems[i];
    bool station = !strncmp(it.id, "radio:", 6), show = !strncmp(it.id, "ard:set:", 8);
    if (!station && !show) continue;
    shown++;
    r = listRow(l, station ? I_RADIO : I_LIBRARY, C_ALARM(), it.title, station ? "Sender" : "Neueste Folge");
    lv_obj_add_event_cb(r, [](lv_event_t* e) {
      int k = (int)(intptr_t)lv_event_get_user_data(e);
      if (k < srcN) setSrc(srcItems[k].id, srcItems[k].title);
    }, LV_EVENT_CLICKED, (void*)(intptr_t)i);
  }
  lv_obj_t* h = label(l, &font_text_20, C_DIM(), shown ? "Weitere: in Radio oder Audiothek merken" : "Sender merken: in Radio lange drücken");
  lv_label_set_long_mode(h, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_align(h, LV_TEXT_ALIGN_CENTER, 0);
  listFit(l);
}

const Page PAGE_ALARMSRC = {srcBuild, nullptr, nullptr, nullptr, false};
