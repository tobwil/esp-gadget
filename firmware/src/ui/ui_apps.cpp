// App-Liste wie bei Wear OS: oben die Uhrzeit, darunter die Apps mit Symbol und Namen.
// Öffnen: auf dem Zifferblatt hochwischen oder BOOT drücken. Schließen: runter- oder nach rechts wischen.
#include <time.h>
#include "../music.h"
#include "../net.h"
#include "ui.h"

using namespace ui;

struct App { const char* icon; const char* name; lv_color_t (*color)(); const Page* page; };
static const App APPS[] = {
    {I_MUSIC, "Musik", C_MUSIC, &PAGE_MUSIC},
    {I_RADIO, "Funk", C_RADIO, &PAGE_RADIO},
    {I_HOURGLASS, "Timer", C_TIMER, &PAGE_TIMER},
    {I_STOPWATCH, "Stoppuhr", C_WATCH, &PAGE_STOPWATCH},
    {I_ALARM, "Wecker", C_ALARM, &PAGE_ALARM},
    {I_SUN, "Wetter", C_TIMER, &PAGE_TILES},
    {I_WATCH, "Zifferblatt", C_BRASS, &PAGE_FACES},
    {I_SETTINGS, "Einstellungen", C_SET, &PAGE_SETTINGS},
};
static const int N = sizeof(APPS) / sizeof(APPS[0]);
static lv_obj_t* timeL;

static void build(lv_obj_t* scr) {
  lv_obj_t* l = roundList(scr, 56, 40);
  lv_obj_remove_flag(l, LV_OBJ_FLAG_SCROLL_ELASTIC);  // oben angekommen: runterwischen schließt
  timeL = label(l, &font_text_24, C_TEXT(), "");
  lv_obj_set_style_text_align(timeL, LV_TEXT_ALIGN_CENTER, 0);
  for (int i = 0; i < N; i++) {
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
