// Funk: großer Sprechknopf, Empfänger wählen, Anklopfen.
#include "../audio.h"
#include "../radio.h"
#include "../settings.h"
#include "ui.h"

using namespace ui;

static lv_obj_t *statusL, *targetB, *ptt, *halo, *hintL, *knockB;
static uint32_t target = 0;
static char targetName[17] = "Alle";
static bool pressing = false;

uint32_t radioTarget() { return target; }

static void setTarget(uint32_t id, const char* name) {
  target = id;
  snprintf(targetName, sizeof(targetName), "%s", name);
  char b[40];
  snprintf(b, sizeof(b), "An: %s  " I_RIGHT, targetName);
  lv_label_set_text(lv_obj_get_child(targetB, 0), b);
}

// Empfänger reihum: Alle → Gerät 1 → Gerät 2 → … → Alle
static void nextTarget(lv_event_t*) {
  radio::Peer ps[12];
  int n = radio::peers(ps, 12);
  int cur = -1;
  for (int i = 0; i < n; i++) if (ps[i].id == target) cur = i;
  if (cur + 1 < n) setTarget(ps[cur + 1].id, ps[cur + 1].name);
  else setTarget(0, "Alle");
  play(0);
}

static void build(lv_obj_t* scr) {
  radio::screenOpen = true;
  title(scr, "Funk", C_RADIO());
  statusL = label(scr, &font_text_20, C_DIM(), "");
  lv_obj_align(statusL, LV_ALIGN_TOP_MID, 0, 82);

  targetB = pill(scr, "", C_SURF(), C_TEXT());
  lv_obj_align(targetB, LV_ALIGN_TOP_MID, 0, 116);
  lv_obj_add_event_cb(targetB, nextTarget, LV_EVENT_CLICKED, nullptr);
  setTarget(target, targetName);

  halo = lv_obj_create(scr);
  lv_obj_set_style_radius(halo, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_border_width(halo, 0, 0);
  lv_obj_set_style_bg_color(halo, C_RADIO(), 0);
  lv_obj_set_style_bg_opa(halo, LV_OPA_30, 0);
  lv_obj_remove_flag(halo, (lv_obj_flag_t)(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));

  ptt = lv_obj_create(scr);
  lv_obj_set_size(ptt, 176, 176);
  lv_obj_align(ptt, LV_ALIGN_CENTER, 0, 22);
  lv_obj_set_style_radius(ptt, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_border_width(ptt, 0, 0);
  lv_obj_set_style_bg_color(ptt, C_RADIO(), 0);
  lv_obj_set_style_bg_color(ptt, C_RED(), LV_STATE_PRESSED);
  lv_obj_remove_flag(ptt, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE));
  lv_obj_t* m = label(ptt, &font_icon_64, C_BG(), I_MIC);
  lv_obj_center(m);
  lv_obj_add_event_cb(ptt, [](lv_event_t*) { pressing = true; radio::pttDown(target); }, LV_EVENT_PRESSED, nullptr);
  lv_obj_add_event_cb(ptt, [](lv_event_t*) { if (pressing) { pressing = false; radio::pttUp(); } }, LV_EVENT_RELEASED, nullptr);
  lv_obj_add_event_cb(ptt, [](lv_event_t*) { if (pressing) { pressing = false; radio::pttUp(); } }, LV_EVENT_PRESS_LOST, nullptr);

  hintL = label(scr, &font_text_20, C_DIM(), "");
  lv_obj_align(hintL, LV_ALIGN_CENTER, 0, 134);

  knockB = pill(scr, I_BELL "  Anklopfen", C_SURF(), C_TEXT());
  lv_obj_align(knockB, LV_ALIGN_BOTTOM_MID, 0, -30);
  lv_obj_add_event_cb(knockB, [](lv_event_t*) {
    radio::knock(target);
    char b[48];
    snprintf(b, sizeof(b), I_BELL "  Angeklopft bei %s", targetName);
    toast(b, 1500);
  }, LV_EVENT_CLICKED, nullptr);
}

static void update() {
  radio::Peer ps[12];
  int n = radio::peers(ps, 12);
  bool found = !target;
  for (int i = 0; i < n; i++) if (ps[i].id == target) found = true;
  if (!found) setTarget(0, "Alle");  // Empfänger ist weg

  char b[64];
  if (n == 0) snprintf(b, sizeof(b), "Niemand in Reichweite");
  else if (n == 1) snprintf(b, sizeof(b), "%s ist da", ps[0].name);
  else snprintf(b, sizeof(b), "%d Geräte in Reichweite", n);
  lv_label_set_text(statusL, b);

  float lvl = 0;
  lv_color_t col = C_RADIO();
  if (radio::transmitting()) {
    snprintf(b, sizeof(b), "Sendet an %s …", targetName);
    lvl = audio::micLevel();
    col = C_RED();
  } else if (radio::receiving()) {
    snprintf(b, sizeof(b), "%s spricht", radio::talker());
    lvl = audio::outLevel();
  } else {
    snprintf(b, sizeof(b), "Halten zum Sprechen · %s", cfg.name);
  }
  lv_label_set_text(hintL, b);
  int s = 176 + (int)(min(1.0f, lvl * 1.5f) * 110);
  lv_obj_set_size(halo, s, s);
  lv_obj_align(halo, LV_ALIGN_CENTER, 0, 22);
  lv_obj_set_style_bg_color(halo, col, 0);
}

static void leave() {
  radio::screenOpen = false;
  if (pressing) { pressing = false; radio::pttUp(); }
}

const Page PAGE_RADIO = {build, update, leave, nullptr, false};
