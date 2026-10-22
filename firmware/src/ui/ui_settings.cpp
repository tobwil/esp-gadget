// Einstellungen: bewusst kurz gehalten – sieben Punkte, jeder mit höchstens einer Unterseite.
#include <WiFi.h>
#include "../audio.h"
#include "../music.h"
#include "../net.h"
#include "../radio.h"
#include "../settings.h"
#include "config.h"
#include "ui.h"

using namespace ui;

extern const ui::Page PAGE_WIFI_, PAGE_LEVEL_, PAGE_SCREEN_, PAGE_FUNK_, PAGE_BRIDGE_, PAGE_INFO_;

static void listTitle(lv_obj_t* list, const char* text, lv_color_t c) {
  lv_obj_t* t = label(list, &font_text_24, c, text);
  lv_obj_set_style_text_align(t, LV_TEXT_ALIGN_CENTER, 0);
}

static void fmtScreenOff(char* b, size_t n, int s) {
  if (!s) snprintf(b, n, "Nie");
  else if (s < 60) snprintf(b, n, "Nach %d Sekunden", s);
  else snprintf(b, n, "Nach %d Minute%s", s / 60, s >= 120 ? "n" : "");
}

// ================================================================== Übersicht

static lv_obj_t *rWifi, *rBright, *rVol, *rScreen, *rFunk, *rBridge, *rInfo;
static bool levelIsVolume = false;

static void settingsResume() {
  char b[64];
  if (netOnline()) snprintf(b, sizeof(b), "%s", netSsid());
  else snprintf(b, sizeof(b), knownCount ? "Nicht verbunden" : "Nicht eingerichtet");
  listRowSub(rWifi, b);
  snprintf(b, sizeof(b), "%d %%", cfg.brightness);
  listRowSub(rBright, b);
  snprintf(b, sizeof(b), "%d %%", cfg.volume);
  listRowSub(rVol, b);
  fmtScreenOff(b, sizeof(b), cfg.screenOff);
  listRowSub(rScreen, b);
  snprintf(b, sizeof(b), "%s · Empfang %s", cfg.name, cfg.radioRx ? "an" : "aus");
  listRowSub(rFunk, b);
  listRowSub(rBridge, music::bridgeKnown() ? "Verbunden" : cfg.bridge[0] ? "Nicht erreichbar" : "Wird gesucht …");
  int bat = hwBattery();
  if (bat >= 0) snprintf(b, sizeof(b), "Akku %d %%%s", bat, hwCharging() ? " · lädt" : "");
  else snprintf(b, sizeof(b), "Version " FW_VERSION);
  listRowSub(rInfo, b);
}

static void settingsBuild(lv_obj_t* scr) {
  lv_obj_t* l = roundList(scr);
  listTitle(l, "Einstellungen", C_SET());
  struct R { lv_obj_t** row; const char* icon; lv_color_t col; const char* text; const ui::Page* page; bool vol; };
  static const R rows[] = {
      {&rWifi, I_WIFI, C_WATCH(), "WLAN", &PAGE_WIFI_, false},
      {&rBright, I_BRIGHT, C_TIMER(), "Helligkeit", &PAGE_LEVEL_, false},
      {&rVol, I_VOL_UP, C_MUSIC(), "Lautstärke", &PAGE_LEVEL_, true},
      {&rScreen, I_SLEEP, C_ALARM(), "Display aus", &PAGE_SCREEN_, false},
      {&rFunk, I_RADIO, C_RADIO(), "Funk", &PAGE_FUNK_, false},
      {&rBridge, I_SERVER, C_MUSIC(), "Musik-Brücke", &PAGE_BRIDGE_, false},
      {&rInfo, I_INFO, C_SET(), "Über das Gerät", &PAGE_INFO_, false},
  };
  for (auto& r : rows) {
    *r.row = listRow(l, r.icon, r.col, r.text, " ");
    lv_obj_add_event_cb(*r.row, [](lv_event_t* e) {
      const R* r = (const R*)lv_event_get_user_data(e);
      levelIsVolume = r->vol;
      play(0);
      push(r->page);
    }, LV_EVENT_CLICKED, (void*)&r);
  }
  settingsResume();
  listFit(l);
}

const Page PAGE_SETTINGS = {settingsBuild, nullptr, nullptr, settingsResume, false};

// ================================================================== WLAN

static lv_obj_t *wList, *wStatus;
static bool wScanning = false;
static char wPick[33];
static ScanNet wNets[16];
static int wCount = 0;

static void wifiFill();

static void wifiConnectTo(const char* ssid, const char* pass) {
  knownAdd(ssid, pass);
  settingsSave();
  netConnect(ssid, pass);
  char b[64];
  snprintf(b, sizeof(b), I_WIFI "  Verbinde mit %s …", ssid);
  toast(b);
}

static void wifiScan() {
  wScanning = true;
  netScanStart();
  wifiFill();
}

static void wifiFill() {
  lv_obj_clean(wList);
  listTitle(wList, "WLAN", C_WATCH());
  char b[64];
  if (netOnline()) snprintf(b, sizeof(b), "Verbunden · %s", netIp().c_str());
  else snprintf(b, sizeof(b), "Nicht verbunden");
  wStatus = label(wList, &font_text_20, C_DIM(), b);
  if (wScanning) {
    label(wList, &font_text_20, C_DIM(), "Suche Netze …");
  }
  for (int i = 0; i < wCount; i++) {
    bool isKnown = knownIndex(wNets[i].ssid) >= 0;
    bool cur = netOnline() && !strcmp(netSsid(), wNets[i].ssid);
    char sub[48];
    snprintf(sub, sizeof(sub), "%s%s%d dBm", cur ? "Verbunden · " : isKnown ? "Gespeichert · " : "",
             wNets[i].open ? "offen · " : "", wNets[i].rssi);
    lv_obj_t* r = listRow(wList, wNets[i].open ? I_WIFI : I_LOCK, cur ? C_RADIO() : isKnown ? C_WATCH() : C_DIM(),
                          wNets[i].ssid, sub);
    lv_obj_add_event_cb(r, [](lv_event_t* e) {
      int i = (int)(intptr_t)lv_event_get_user_data(e);
      snprintf(wPick, sizeof(wPick), "%s", wNets[i].ssid);
      const char* pw = knownPass(wPick);
      if (pw) wifiConnectTo(wPick, pw);
      else if (wNets[i].open) wifiConnectTo(wPick, "");
      else keyboard(wPick, "", true, [](const char* pass) { wifiConnectTo(wPick, pass); });
    }, LV_EVENT_SHORT_CLICKED, (void*)(intptr_t)i);
    lv_obj_add_event_cb(r, [](lv_event_t* e) {
      int i = (int)(intptr_t)lv_event_get_user_data(e);
      if (knownIndex(wNets[i].ssid) < 0) return;
      knownForget(wNets[i].ssid);
      settingsSave();
      toast("Netz vergessen");
      wifiFill();
      lv_indev_wait_release(lv_indev_active());
    }, LV_EVENT_LONG_PRESSED, (void*)(intptr_t)i);
  }
  // gespeicherte Netze, die gerade nicht in Reichweite sind
  for (int k = 0; k < knownCount; k++) {
    bool seen = false;
    for (int i = 0; i < wCount; i++) if (!strcmp(wNets[i].ssid, known[k].ssid)) seen = true;
    if (seen || wScanning) continue;
    lv_obj_t* r = listRow(wList, I_WIFI_OFF, C_DIM(), known[k].ssid, "Gespeichert · nicht in Reichweite");
    lv_obj_add_event_cb(r, [](lv_event_t* e) {
      int k = (int)(intptr_t)lv_event_get_user_data(e);
      if (k >= knownCount) return;
      knownForget(known[k].ssid);
      settingsSave();
      toast("Netz vergessen");
      wifiFill();
      lv_indev_wait_release(lv_indev_active());
    }, LV_EVENT_LONG_PRESSED, (void*)(intptr_t)k);
  }
  if (!wScanning) {
    lv_obj_t* p = pill(wList, I_REFRESH "  Neu suchen", C_SURF2(), C_TEXT());
    lv_obj_add_event_cb(p, [](lv_event_t*) { wifiScan(); }, LV_EVENT_CLICKED, nullptr);
    lv_obj_t* h = label(wList, &font_text_20, C_DIM(), "Lange drücken = vergessen");
    lv_obj_set_style_text_align(h, LV_TEXT_ALIGN_CENTER, 0);
  }
  listFit(wList);
}

static void wifiBuild(lv_obj_t* scr) {
  netManual(true);
  wList = roundList(scr);
  wCount = 0;
  wifiScan();
}

static void wifiUpdate() {
  static uint32_t at = 0;
  static bool wasOnline = false;
  if (wScanning) {
    int n = netScanResult(wNets, 16);
    if (n >= 0) { wCount = n; wScanning = false; wifiFill(); }
  } else if (netOnline() != wasOnline || millis() - at > 5000) {
    at = millis();
    char b[64];
    if (netOnline()) snprintf(b, sizeof(b), "Verbunden · %s", netIp().c_str());
    else snprintf(b, sizeof(b), "Nicht verbunden");
    if (wStatus) lv_label_set_text(wStatus, b);
    if (netOnline() != wasOnline) wifiFill();
  }
  wasOnline = netOnline();
}

static void wifiLeave() { netManual(false); wStatus = nullptr; }

const ui::Page PAGE_WIFI_ = {wifiBuild, wifiUpdate, wifiLeave, nullptr, false};

// ================================================================== Helligkeit / Lautstärke

static lv_obj_t *lvArc, *lvVal;

static void levelShow() {
  char b[8];
  snprintf(b, sizeof(b), "%d", lv_arc_get_value(lvArc));
  lv_label_set_text(lvVal, b);
}

static void levelBuild(lv_obj_t* scr) {
  lv_color_t c = levelIsVolume ? C_MUSIC() : C_TIMER();
  lvArc = lv_arc_create(scr);
  lv_obj_set_size(lvArc, 380, 380);
  lv_obj_center(lvArc);
  lv_arc_set_bg_angles(lvArc, 135, 45);
  lv_arc_set_range(lvArc, levelIsVolume ? 0 : 5, 100);
  lv_arc_set_value(lvArc, levelIsVolume ? cfg.volume : cfg.brightness);
  lv_obj_set_style_arc_width(lvArc, 30, LV_PART_MAIN);
  lv_obj_set_style_arc_width(lvArc, 30, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(lvArc, C_SURF(), LV_PART_MAIN);
  lv_obj_set_style_arc_color(lvArc, c, LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(lvArc, C_TEXT(), LV_PART_KNOB);
  lv_obj_set_style_pad_all(lvArc, 2, LV_PART_KNOB);
  lv_obj_remove_flag(lvArc, LV_OBJ_FLAG_GESTURE_BUBBLE);
  lv_obj_add_event_cb(lvArc, [](lv_event_t*) {
    int v = lv_arc_get_value(lvArc);
    if (levelIsVolume) { cfg.volume = v; audio::setVolume(v); }
    else { cfg.brightness = v; hwBrightness(v); }
    levelShow();
  }, LV_EVENT_VALUE_CHANGED, nullptr);
  lv_obj_add_event_cb(lvArc, [](lv_event_t*) { if (levelIsVolume) play(0); }, LV_EVENT_RELEASED, nullptr);
  lv_obj_t* ic = label(scr, &font_icon_40, c, levelIsVolume ? I_VOL_UP : I_BRIGHT);
  lv_obj_align(ic, LV_ALIGN_CENTER, 0, -80);
  lvVal = label(scr, &font_num_80, C_TEXT(), "");
  lv_obj_align(lvVal, LV_ALIGN_CENTER, 0, 0);
  lv_obj_t* t = label(scr, &font_text_20, C_DIM(), levelIsVolume ? "Lautstärke in %" : "Helligkeit in %");
  lv_obj_align(t, LV_ALIGN_CENTER, 0, 62);
  levelShow();
}

static void levelLeave() { settingsSave(); }

const ui::Page PAGE_LEVEL_ = {levelBuild, nullptr, levelLeave, nullptr, false};

// ================================================================== Display aus nach …

static const uint16_t SCREEN_OPTS[] = {15, 30, 60, 120, 300, 0};
static lv_obj_t* sList;

static void screenFill() {
  lv_obj_clean(sList);
  listTitle(sList, "Display aus", C_ALARM());
  for (int i = 0; i < 6; i++) {
    char b[32];
    fmtScreenOff(b, sizeof(b), SCREEN_OPTS[i]);
    bool sel = cfg.screenOff == SCREEN_OPTS[i];
    lv_obj_t* r = listRow(sList, sel ? I_CHECK : " ", C_ALARM(), b);
    lv_obj_add_event_cb(r, [](lv_event_t* e) {
      cfg.screenOff = SCREEN_OPTS[(int)(intptr_t)lv_event_get_user_data(e)];
      settingsSave();
      play(0);
      back();
    }, LV_EVENT_CLICKED, (void*)(intptr_t)i);
  }
  lv_obj_t* h = label(sList, &font_text_20, C_DIM(), "Antippen, BOOT oder PWR weckt es wieder");
  lv_label_set_long_mode(h, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_align(h, LV_TEXT_ALIGN_CENTER, 0);
  listFit(sList);
}

static void screenBuild(lv_obj_t* scr) {
  sList = roundList(scr);
  screenFill();
}

const ui::Page PAGE_SCREEN_ = {screenBuild, nullptr, nullptr, nullptr, false};

// ================================================================== Funk

static lv_obj_t *fName, *fRx;

static void funkBuild(lv_obj_t* scr) {
  lv_obj_t* l = roundList(scr);
  listTitle(l, "Funk", C_RADIO());
  fName = listRow(l, I_PERSON, C_RADIO(), "Name", cfg.name);
  lv_obj_add_event_cb(fName, [](lv_event_t*) {
    keyboard("Name für den Funk", cfg.name, false, [](const char* t) {
      if (!t[0]) return;
      snprintf(cfg.name, sizeof(cfg.name), "%s", t);
      settingsSave();
      radio::nameChanged();
      listRowSub(fName, cfg.name);
    });
  }, LV_EVENT_CLICKED, nullptr);
  fRx = listRow(l, I_RADIO_WAVES, C_RADIO(), "Im Hintergrund hören", "Auch außerhalb der Funk-App");
  lv_obj_t* sw = listRowSwitch(fRx, cfg.radioRx);
  lv_obj_add_event_cb(fRx, [](lv_event_t* e) {
    cfg.radioRx = !cfg.radioRx;
    settingsSave();
    lv_obj_t* sw = (lv_obj_t*)lv_event_get_user_data(e);
    if (cfg.radioRx) lv_obj_add_state(sw, LV_STATE_CHECKED); else lv_obj_remove_state(sw, LV_STATE_CHECKED);
  }, LV_EVENT_CLICKED, sw);
  char b[120];
  snprintf(b, sizeof(b), "In der Nähe per ESP-NOW, im selben WLAN zusätzlich übers Netz. Ohne WLAN: Kanal %d.", netChannel());
  lv_obj_t* h = label(l, &font_text_20, C_DIM(), b);
  lv_label_set_long_mode(h, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_align(h, LV_TEXT_ALIGN_CENTER, 0);
  listFit(l);
}

const ui::Page PAGE_FUNK_ = {funkBuild, nullptr, nullptr, nullptr, false};

// ================================================================== Musik-Brücke

static lv_obj_t *bStatus;

static void bridgeUpdate() {
  char b[140];
  if (music::bridgeKnown()) snprintf(b, sizeof(b), "Verbunden mit\n%s", music::bridgeUrl());
  else if (!netOnline()) snprintf(b, sizeof(b), "Kein WLAN");
  else if (cfg.bridge[0]) snprintf(b, sizeof(b), "Nicht erreichbar:\n%s", cfg.bridge);
  else snprintf(b, sizeof(b), "Suche im WLAN …");
  if (strcmp(lv_label_get_text(bStatus), b)) lv_label_set_text(bStatus, b);
}

static void bridgeBuild(lv_obj_t* scr) {
  lv_obj_t* l = roundList(scr);
  listTitle(l, "Musik-Brücke", C_MUSIC());
  bStatus = label(l, &font_text_20, C_TEXT(), "");
  lv_label_set_long_mode(bStatus, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_align(bStatus, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_t* r1 = listRow(l, I_SEARCH, C_MUSIC(), "Automatisch suchen", "mDNS im WLAN");
  lv_obj_add_event_cb(r1, [](lv_event_t*) {
    cfg.bridge[0] = 0;
    settingsSave();
    music::bridgeChanged();
    toast("Suche die Brücke …");
  }, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* r2 = listRow(l, I_SERVER, C_MUSIC(), "Adresse eingeben", "z. B. http://192.168.1.20:8765");
  lv_obj_add_event_cb(r2, [](lv_event_t*) {
    keyboard("Adresse der Brücke", cfg.bridge[0] ? cfg.bridge : "http://", false, [](const char* t) {
      snprintf(cfg.bridge, sizeof(cfg.bridge), "%s", t);
      settingsSave();
      music::bridgeChanged();
    });
  }, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* h = label(l, &font_text_20, C_DIM(), "Die Brücke (bridge/server.py) läuft auf einem Rechner im WLAN und holt die Musik von YouTube Music.");
  lv_label_set_long_mode(h, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_align(h, LV_TEXT_ALIGN_CENTER, 0);
  bridgeUpdate();
  listFit(l);
}

const ui::Page PAGE_BRIDGE_ = {bridgeBuild, bridgeUpdate, nullptr, nullptr, false};

// ================================================================== Über das Gerät

static lv_obj_t* iText;

static void infoUpdate() {
  static uint32_t at = 0;
  if (at && millis() - at < 1000) return;
  at = millis();
  char b[400];
  int bat = hwBattery();
  char batS[32];
  if (bat >= 0) snprintf(batS, sizeof(batS), "%d %%%s", bat, hwCharging() ? ", lädt" : hwUsb() ? ", USB" : "");
  else snprintf(batS, sizeof(batS), "kein Akku");
  snprintf(b, sizeof(b),
           "Akku: %s\nWLAN: %s\nIP: %s\nFunk-ID: %08X\nSpeicher: %u KB frei\nVersion " FW_VERSION,
           batS, netOnline() ? netSsid() : "–", netOnline() ? netIp().c_str() : "–", (unsigned)radio::myId(),
           (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024));
  lv_label_set_text(iText, b);
}

static void infoBuild(lv_obj_t* scr) {
  lv_obj_t* l = roundList(scr);
  listTitle(l, "Über das Gerät", C_SET());
  iText = label(l, &font_text_20, C_TEXT(), "");
  lv_obj_set_style_text_align(iText, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_line_space(iText, 6, 0);
  lv_obj_t* r = pill(l, I_RESTART "  Neustart", C_SURF2(), C_TEXT());
  lv_obj_add_event_cb(r, [](lv_event_t*) { ESP.restart(); }, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* o = pill(l, I_POWER "  Ausschalten", C_RED(), C_TEXT());
  lv_obj_add_event_cb(o, [](lv_event_t*) { audio::shutdown(); delay(50); hwPowerOff(); }, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* h = label(l, &font_text_20, C_DIM(), "Einschalten: PWR-Taste");
  lv_obj_set_style_text_align(h, LV_TEXT_ALIGN_CENTER, 0);
  infoUpdate();
  listFit(l);
}

const ui::Page PAGE_INFO_ = {infoBuild, infoUpdate, nullptr, nullptr, false};
