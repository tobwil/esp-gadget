// App-Liste wie bei Wear OS: oben die Uhrzeit, darunter die Apps mit Symbol und Namen.
// Öffnen: auf dem Zifferblatt hochwischen oder BOOT drücken. Schließen: runter- oder nach rechts wischen.
#include <time.h>
#include "../music.h"
#include "../assist.h"
#include "../kids.h"
#include "../net.h"
#include "ui.h"

using namespace ui;

struct App { const char* icon; const char* name; lv_color_t (*color)(); const Page* page; uint8_t kid; };
static char assistName[24] = "KIra";  // Name der Assistentin (Weboberfläche)
static const App APPS[] = {
    // kid: im Kindermodus nur mit diesem App-Bit (0xFF = immer, 0 = nie)
    {"KI", assistName, C_TEXT, &PAGE_ASSIST, 0},  // nie im Kindermodus
    {I_MUSIC, "Musik", C_MUSIC, &PAGE_MUSIC, 0xFF},
    {I_RADIO, "Funk", C_RADIO, &PAGE_RADIO, kids::A_RADIO},
    {I_HOURGLASS, "Timer", C_TIMER, &PAGE_TIMER, kids::A_TIMER},
    {I_STOPWATCH, "Stoppuhr", C_WATCH, &PAGE_STOPWATCH, kids::A_STOPWATCH},
    {I_ALARM, "Wecker", C_ALARM, &PAGE_ALARM, kids::A_ALARM},
    {I_SUN, "Wetter", C_TIMER, &PAGE_TILES, kids::A_WEATHER},
    {I_BOLT, "Balance", C_BRASS, &PAGE_GAME, kids::A_GAME},
    {I_WATCH, "Zifferblatt", C_BRASS, &PAGE_FACES, 0},
    {I_SETTINGS, "Einstellungen", C_SET, &PAGE_SETTINGS, 0},
};
static const int N = sizeof(APPS) / sizeof(APPS[0]);
static lv_obj_t* timeL;

static void build(lv_obj_t* scr) {
  lv_obj_t* l = roundList(scr, 56, 40);
  lv_obj_remove_flag(l, LV_OBJ_FLAG_SCROLL_ELASTIC);  // oben angekommen: runterwischen schließt
  timeL = label(l, &font_text_24, C_TEXT(), "");
  lv_obj_set_style_text_align(timeL, LV_TEXT_ALIGN_CENTER, 0);
  snprintf(assistName, sizeof(assistName), "%s", assist::name().c_str());
  for (int i = 0; i < N; i++) {
    if (APPS[i].page == &PAGE_ASSIST && !assist::available()) continue;
    if (kids::on() && APPS[i].kid != 0xFF && !kids::allowed((kids::App)APPS[i].kid)) continue;
    if (kids::night() && (APPS[i].page != &PAGE_MUSIC || kids::musicLocked())) continue;  // Schlafenszeit
    lv_color_t c = APPS[i].color();
    lv_obj_t* r = listRow(l, nullptr, c, APPS[i].name);
    lv_obj_set_height(r, 76);
    // farbiger Kreis mit Symbol vor dem Namen
    lv_obj_t* dot = lv_obj_create(r);
    lv_obj_move_to_index(dot, 0);
    lv_obj_set_size(dot, 54, 54);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(dot, lv_color_mix(c, C_BG(), 70), 0);
    lv_obj_set_style_border_width(dot, 0, 0);
    lv_obj_remove_flag(dot, (lv_obj_flag_t)(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));
    lv_obj_t* ic = label(dot, &font_text_32, c, APPS[i].icon);
    lv_obj_center(ic);
    lv_obj_set_style_pad_left(r, 12, 0);
    lv_obj_add_event_cb(r, [](lv_event_t* e) {
      int k = (int)(intptr_t)lv_event_get_user_data(e);
      play(0);
      if (APPS[k].page == &PAGE_MUSIC && !music::state().active) browseOpen("home");
      else if (APPS[k].page == &PAGE_TILES) tilesOpen(1);  // Wetter
      else push(APPS[k].page);
    }, LV_EVENT_CLICKED, (void*)(intptr_t)i);
  }
  if (kids::night()) {
    char b[64];
    snprintf(b, sizeof(b), I_SLEEP "  Gute Nacht!\nBis morgen um %d:%02d", kids::cfg().wake / 60, kids::cfg().wake % 60);
    lv_obj_t* g = label(l, &font_text_24, C_ALARM(), b);
    lv_obj_set_style_text_align(g, LV_TEXT_ALIGN_CENTER, 0);
  }
  listFit(l);
}

static void update() {
  char b[8] = "--:--";
  if (timeValid()) {
    time_t now = time(nullptr);
    struct tm t;
    localtime_r(&now, &t);
    snprintf(b, sizeof(b), "%02d:%02d", t.tm_hour, t.tm_min);
  }
  if (strcmp(lv_label_get_text(timeL), b)) lv_label_set_text(timeL, b);
}

const Page PAGE_APPS = {build, update, nullptr, nullptr, false, ENTER_BOTTOM};
