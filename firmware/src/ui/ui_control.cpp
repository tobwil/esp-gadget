// Kontrollzentrum wie bei Wear OS / watchOS: vom Zifferblatt herunterwischen, hochwischen schließt.
// Oben Uhrzeit, Akku (mit geschätzter Restlaufzeit) und WLAN, darunter sechs Schalter.
#include <time.h>
#include "../kids.h"
#include "../power.h"
#include "../net.h"
#include "../settings.h"
#include "ui.h"

using namespace ui;

static lv_obj_t *timeL, *statL, *remainL, *hintL, *rxB, *brightB;
static lv_obj_t* torch = nullptr;
static uint8_t torchBright = 0;

static void hint(const char* t) { lv_label_set_text(hintL, t); }

static void torchOff() {
  if (!torch) return;
  lv_obj_delete(torch);
  torch = nullptr;
  hwBrightness(torchBright);
}

static void showRx() {
  lv_label_set_text(lv_obj_get_child(rxB, 0), cfg.radioRx ? I_HEARING : I_HEARING_OFF);
  lv_obj_set_style_bg_color(rxB, cfg.radioRx ? C_RADIO() : C_SURF(), 0);
  lv_obj_set_style_text_color(lv_obj_get_child(rxB, 0), cfg.radioRx ? C_BG() : C_TEXT(), 0);
}

static void build(lv_obj_t* scr) {
  timeL = label(scr, &font_text_24, C_TEXT(), "");
  lv_obj_align(timeL, LV_ALIGN_TOP_MID, 0, 40);
  statL = label(scr, &font_text_20, C_DIM(), "");
  lv_obj_align(statL, LV_ALIGN_TOP_MID, 0, 74);
  remainL = label(scr, &font_text_20, C_DIM(), "");
  lv_obj_align(remainL, LV_ALIGN_TOP_MID, 0, 100);

  lv_obj_t* grid = lv_obj_create(scr);
  lv_obj_remove_style_all(grid);
  lv_obj_set_size(grid, 330, 210);
  lv_obj_align(grid, LV_ALIGN_CENTER, 0, 18);
  lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
  lv_obj_set_flex_align(grid, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_gap(grid, 18, 0);
  lv_obj_remove_flag(grid, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t* vol = roundButton(grid, I_VOL_UP, 92, C_SURF(), C_TEXT());
  lv_obj_add_event_cb(vol, [](lv_event_t*) { volumeShow(true); }, LV_EVENT_CLICKED, nullptr);

  brightB = roundButton(grid, I_BRIGHT, 92, C_SURF(), C_TEXT());
  lv_obj_add_event_cb(brightB, [](lv_event_t*) {
    // reihum 20 → 50 → 80 → 100 %
    cfg.brightness = cfg.brightness < 20 ? 20 : cfg.brightness < 50 ? 50 : cfg.brightness < 80 ? 80 : cfg.brightness < 100 ? 100 : 20;
    hwBrightness(cfg.brightness);
    settingsSave();
    char b[32];
    snprintf(b, sizeof(b), "Helligkeit %d %%", cfg.brightness);
    hint(b);
  }, LV_EVENT_CLICKED, nullptr);

  rxB = roundButton(grid, I_HEARING, 92, C_SURF(), C_TEXT());
  hide(rxB, !kids::allowed(kids::A_RADIO));
  lv_obj_add_event_cb(rxB, [](lv_event_t*) {
    cfg.radioRx = !cfg.radioRx;
    settingsSave();
    showRx();
    hint(cfg.radioRx ? "Funk im Hintergrund an" : "Funk im Hintergrund aus");
  }, LV_EVENT_CLICKED, nullptr);
  showRx();

  lv_obj_t* tb = roundButton(grid, I_TORCH, 92, C_SURF(), C_TEXT());
  lv_obj_add_event_cb(tb, [](lv_event_t*) {
    // Taschenlampe: ganzer Bildschirm weiß mit voller Helligkeit, Antippen schaltet aus
    torchBright = cfg.brightness;
    torch = lv_obj_create(lv_layer_top());
    lv_obj_set_size(torch, W, H);
    lv_obj_set_style_bg_color(torch, lv_color_white(), 0);
    lv_obj_set_style_border_width(torch, 0, 0);
    lv_obj_set_style_radius(torch, 0, 0);
    lv_obj_add_event_cb(torch, [](lv_event_t*) { torchOff(); }, LV_EVENT_CLICKED, nullptr);
    hwBrightness(100, true);  // Taschenlampe: volle Helligkeit, auch im Sparmodus
  }, LV_EVENT_CLICKED, nullptr);

  lv_obj_t* off = roundButton(grid, I_SCREEN_OFF, 92, C_SURF(), C_TEXT());
  lv_obj_add_event_cb(off, [](lv_event_t*) {
    home();
    hwScreen(false);
  }, LV_EVENT_CLICKED, nullptr);

  lv_obj_t* set = roundButton(grid, I_SETTINGS, 92, C_SURF(), C_TEXT());
  hide(set, kids::on());  // Kindermodus: Einstellungen nur über die Weboberfläche
  lv_obj_add_event_cb(set, [](lv_event_t*) { push(&PAGE_SETTINGS); }, LV_EVENT_CLICKED, nullptr);

  hintL = label(scr, &font_text_20, C_DIM(), "Hochwischen zum Schließen");
  lv_obj_align(hintL, LV_ALIGN_BOTTOM_MID, 0, -44);
}

static void update() {
  char b[64] = "--:--";
  if (timeValid()) {
    time_t now = time(nullptr);
    struct tm t;
    localtime_r(&now, &t);
    snprintf(b, sizeof(b), "%02d:%02d", t.tm_hour, t.tm_min);
  }
  if (strcmp(lv_label_get_text(timeL), b)) lv_label_set_text(timeL, b);
  int bat = hwBattery();
  char batS[24];
  if (bat >= 0) snprintf(batS, sizeof(batS), "%s %d %%", hwCharging() ? I_CHARGING : I_BATTERY, bat);
  else snprintf(batS, sizeof(batS), I_BOLT " USB");
  snprintf(b, sizeof(b), "%s%s   %s %s", batS, pwr::saving() ? " · Sparen" : "", netOnline() ? I_WIFI : I_WIFI_OFF,
           netOnline() ? netSsid() : "aus");
  if (strcmp(lv_label_get_text(statL), b)) lv_label_set_text(statL, b);
  String r = pwr::remainingText();
  if (strcmp(lv_label_get_text(remainL), r.c_str())) lv_label_set_text(remainL, r.c_str());
}

static void leave() { torchOff(); }

const Page PAGE_CONTROL = {build, update, leave, nullptr, false, ENTER_TOP};
