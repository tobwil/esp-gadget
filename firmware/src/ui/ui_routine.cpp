// Kinder-Routinen: die Liste zum Abhaken, Sterne für jeden Haken, eine kleine Feier, wenn alles geschafft ist.
#include "../audio.h"
#include "../routines.h"
#include "../kids.h"
#include "ui.h"

using namespace ui;

namespace {

lv_obj_t *list, *starL, *party = nullptr;
routines::Which shown = routines::NONE;

// Pixel-Stern (9 × 9)
const char* const STAR[] = {"....X....", "....X....", "...XXX...", "XXXXXXXXX", ".XXXXXXX.",
                            "..XXXXX..", "..XX.XX..", ".XX...XX.", ".X.....X."};

void drawStar(lv_layer_t* L, int cx, int cy, int cell, lv_color_t c) {
  lv_draw_rect_dsc_t d;
  lv_draw_rect_dsc_init(&d);
  d.bg_color = c;
  for (int y = 0; y < 9; y++)
    for (int x = 0; x < 9; x++)
      if (STAR[y][x] == 'X') {
        lv_area_t a = {cx + (x - 4) * cell - cell / 2, cy + (y - 4) * cell - cell / 2, cx + (x - 4) * cell + cell / 2 - 1,
                       cy + (y - 4) * cell + cell / 2 - 1};
        lv_draw_rect(L, &d, &a);
      }
}

lv_obj_t* starObj(lv_obj_t* parent, int size, int cell) {
  lv_obj_t* o = lv_obj_create(parent);
  lv_obj_remove_style_all(o);
  lv_obj_set_size(o, size, size);
  lv_obj_remove_flag(o, (lv_obj_flag_t)(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));
  lv_obj_set_user_data(o, (void*)(intptr_t)cell);
  lv_obj_add_event_cb(o, [](lv_event_t* e) {
    lv_obj_t* o = (lv_obj_t*)lv_event_get_target(e);
    lv_area_t a;
    lv_obj_get_coords(o, &a);
    drawStar(lv_event_get_layer(e), (a.x1 + a.x2) / 2, (a.y1 + a.y2) / 2, (int)(intptr_t)lv_obj_get_user_data(o),
             lv_color_hex(0xFFD23F));
  }, LV_EVENT_DRAW_MAIN, nullptr);
  return o;
}

void showStars() {
  char b[32];
  snprintf(b, sizeof(b), "%d heute", routines::starsToday());
  lv_label_set_text(starL, b);
}

void celebrate(int stars) {
  if (party) lv_obj_delete(party);
  party = lv_obj_create(lv_layer_top());
  lv_obj_remove_style_all(party);
  lv_obj_set_size(party, W, H);
  lv_obj_set_style_bg_color(party, C_BG(), 0);
  lv_obj_set_style_bg_opa(party, LV_OPA_90, 0);
  lv_obj_t* s = starObj(party, 200, 20);
  lv_obj_align(s, LV_ALIGN_CENTER, 0, -60);
  lv_obj_t* t = label(party, &font_text_32, lv_color_hex(0xFFD23F), "Super gemacht!");
  lv_obj_align(t, LV_ALIGN_CENTER, 0, 70);
  char b[32];
  snprintf(b, sizeof(b), "+%d Sterne", stars);
  lv_obj_t* u = label(party, &font_text_24, C_TEXT(), b);
  lv_obj_align(u, LV_ALIGN_CENTER, 0, 115);
  audio::play(audio::FX_CHIME);
  lv_timer_t* tm = lv_timer_create([](lv_timer_t*) { if (party) { lv_obj_delete(party); party = nullptr; } }, 2800, nullptr);
  lv_timer_set_repeat_count(tm, 1);
}

void fill() {
  lv_obj_clean(list);
  shown = routines::active();
  lv_obj_t* head = lv_obj_create(list);
  lv_obj_remove_style_all(head);
  lv_obj_set_size(head, 300, 56);
  lv_obj_set_flex_flow(head, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(head, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(head, 10, 0);
  starObj(head, 44, 4);
  starL = label(head, &font_text_24, lv_color_hex(0xFFD23F), "");
  showStars();
  if (!shown) {
    int at;
    routines::Which w = routines::next(&at);
    char b[80];
    snprintf(b, sizeof(b), "Gerade keine Aufgaben.\n%s geht es um %d:%02d los.", routines::name(w), at / 60, at % 60);
    lv_obj_t* l = label(list, &font_text_24, C_TEXT(), b);
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(l, 320);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    listFit(list);
    return;
  }
  lv_obj_t* t = label(list, &font_text_24, C_BRASS(), routines::name(shown));
  lv_obj_set_style_text_align(t, LV_TEXT_ALIGN_CENTER, 0);
  for (int i = 0; i < routines::count(shown); i++) {
    bool d = routines::done(shown, i);
    lv_obj_t* r = listRow(list, I_CHECK, d ? C_RADIO() : C_SURF2(), routines::item(shown, i).c_str());
    if (d) lv_obj_set_style_bg_color(r, lv_color_mix(C_RADIO(), C_BG(), 50), 0);
    lv_obj_add_event_cb(r, [](lv_event_t* e) {
      int k = (int)(intptr_t)lv_event_get_user_data(e);
      bool now = !routines::done(shown, k);
      int stars = routines::check(shown, k, now);
      play(now ? 1 : 0);
      lv_async_call([](void*) { fill(); }, nullptr);
      if (stars >= 3) celebrate(stars);
    }, LV_EVENT_CLICKED, (void*)(intptr_t)i);
  }
  listFit(list);
}

}  // namespace

static void build(lv_obj_t* scr) {
  list = roundList(scr, 40, 40);
  fill();
}

static void update() {
  static uint32_t at = 0;
  if (millis() - at > 30000) { at = millis(); if (routines::active() != shown) fill(); }  // Fenster vorbei / beginnt
}

static void leave() { if (party) { lv_obj_delete(party); party = nullptr; } }

const Page PAGE_ROUTINE = {build, update, leave, nullptr, false};
