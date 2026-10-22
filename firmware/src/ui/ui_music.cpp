// Musik: „Läuft gerade“ (Cover, Fortschritt am Rand, Steuerung) und die Listen der Brücke.
#include "../music.h"
#include "../voice.h"
#include "../settings.h"
#include "../favs.h"
#include "../kids.h"
#include "ui.h"

using namespace ui;

// ================================================================== Läuft gerade

static lv_obj_t* phIcon;  // Symbol im Cover-Platzhalter
static lv_obj_t* ring;      // Fortschritt am Rand
static lv_obj_t* cover;
static lv_obj_t* coverPh;   // Platzhalter ohne Cover
static lv_obj_t* titleL;
static lv_obj_t* artistL;
static lv_obj_t* playB;
static lv_obj_t* emptyBox;
static lv_obj_t* ctrlBox;
static lv_obj_t* hintL;
static lv_image_dsc_t artDsc;
static uint32_t artSeen = 0xFFFFFFFF;
static char lastId[16];
static bool hintShown = false;

static void build(lv_obj_t* scr) {
  ring = lv_arc_create(scr);
  lv_obj_set_size(ring, 456, 456);
  lv_obj_center(ring);
  lv_arc_set_rotation(ring, 270);
  lv_arc_set_bg_angles(ring, 0, 360);
  lv_arc_set_range(ring, 0, 1000);
  lv_obj_set_style_arc_width(ring, 6, LV_PART_MAIN);
  lv_obj_set_style_arc_width(ring, 6, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(ring, C_SURF2(), LV_PART_MAIN);
  lv_obj_set_style_arc_color(ring, C_MUSIC(), LV_PART_INDICATOR);
  lv_obj_set_style_opa(ring, LV_OPA_TRANSP, LV_PART_KNOB);
  lv_obj_remove_flag(ring, LV_OBJ_FLAG_CLICKABLE);

  // Cover (oder Platzhalter); Antippen öffnet den Lautstärkeregler
  coverPh = lv_obj_create(scr);
  lv_obj_set_size(coverPh, music::ART, music::ART);
  lv_obj_align(coverPh, LV_ALIGN_TOP_MID, 0, 34);
  lv_obj_set_style_radius(coverPh, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(coverPh, C_SURF(), 0);
  lv_obj_set_style_border_width(coverPh, 0, 0);
  lv_obj_remove_flag(coverPh, LV_OBJ_FLAG_SCROLLABLE);
  phIcon = label(coverPh, &font_icon_64, C_SURF2(), I_ALBUM);
  lv_obj_center(phIcon);
  cover = lv_image_create(scr);
  lv_obj_align(cover, LV_ALIGN_TOP_MID, 0, 34);
  lv_obj_add_flag(cover, LV_OBJ_FLAG_HIDDEN);
  for (lv_obj_t* o : {coverPh, (lv_obj_t*)cover}) {
    lv_obj_add_flag(o, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(o, [](lv_event_t*) { volumeShow(true); }, LV_EVENT_CLICKED, nullptr);
  }

  titleL = label(scr, &font_text_24, C_TEXT(), "");
  lv_label_set_long_mode(titleL, LV_LABEL_LONG_SCROLL_CIRCULAR);
  lv_obj_set_width(titleL, 330);
  lv_obj_set_style_text_align(titleL, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(titleL, LV_ALIGN_TOP_MID, 0, 264);
  artistL = label(scr, &font_text_20, C_DIM(), "");
  lv_label_set_long_mode(artistL, LV_LABEL_LONG_DOT);
  lv_obj_set_size(artistL, 300, lv_font_get_line_height(&font_text_20));
  lv_obj_set_style_text_align(artistL, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(artistL, LV_ALIGN_TOP_MID, 0, 297);

  // Steuerleiste: Bibliothek · zurück · Pause · weiter · Lautstärke
  ctrlBox = lv_obj_create(scr);
  lv_obj_remove_style_all(ctrlBox);
  lv_obj_set_size(ctrlBox, 370, 92);
  lv_obj_align(ctrlBox, LV_ALIGN_TOP_MID, 0, 330);
  lv_obj_set_flex_flow(ctrlBox, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(ctrlBox, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(ctrlBox, 10, 0);
  lv_obj_t* lib = roundButton(ctrlBox, I_LIBRARY, 52, C_BG(), C_DIM(), &font_text_32);
  lv_obj_add_event_cb(lib, [](lv_event_t*) { browseOpen("home"); }, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* pv = roundButton(ctrlBox, I_PREV, 62, C_SURF(), C_TEXT());
  lv_obj_add_event_cb(pv, [](lv_event_t*) { music::prev(); }, LV_EVENT_CLICKED, nullptr);
  playB = roundButton(ctrlBox, I_PAUSE, 86, C_MUSIC(), C_BG(), &font_icon_64);
  lv_obj_add_event_cb(playB, [](lv_event_t*) { music::toggle(); }, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* nx = roundButton(ctrlBox, I_NEXT, 62, C_SURF(), C_TEXT());
  lv_obj_add_event_cb(nx, [](lv_event_t*) { music::next(); }, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* vol = roundButton(ctrlBox, I_VOL_UP, 52, C_BG(), C_DIM(), &font_text_32);
  lv_obj_add_event_cb(vol, [](lv_event_t*) { volumeShow(true); }, LV_EVENT_CLICKED, nullptr);

  hintL = label(scr, &font_text_20, C_BRASS(), "Am Rand drehen = Lautstärke");
  lv_obj_align(hintL, LV_ALIGN_BOTTOM_MID, 0, -22);
  hide(hintL, hintShown);
  hintShown = true;
  lv_timer_t* ht = lv_timer_create([](lv_timer_t* t) {
    lv_obj_t* l = (lv_obj_t*)lv_timer_get_user_data(t);
    if (lv_obj_is_valid(l)) hide(l, true);
  }, 5000, hintL);
  lv_timer_set_repeat_count(ht, 1);

  // nichts läuft
  emptyBox = lv_obj_create(scr);
  lv_obj_remove_style_all(emptyBox);
  lv_obj_set_size(emptyBox, 340, 200);
  lv_obj_align(emptyBox, LV_ALIGN_TOP_MID, 0, 262);
  lv_obj_set_flex_flow(emptyBox, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(emptyBox, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(emptyBox, 16, 0);
  label(emptyBox, &font_text_24, C_TEXT(), "Gerade läuft nichts");
  lv_obj_t* go = pill(emptyBox, I_LIBRARY "  Bibliothek", C_MUSIC(), C_BG());
  lv_obj_add_event_cb(go, [](lv_event_t*) { browseOpen("home"); }, LV_EVENT_CLICKED, nullptr);

  bezel(scr, []() { return music::state().active; });
  artSeen = 0xFFFFFFFF;
  lastId[0] = 0;
}

static void update() {
  music::State s = music::state();
  // „Nichts läuft“: nur Platzhalter und Bibliothek-Knopf, keine Steuerung
  hide(emptyBox, s.active);
  hide(ctrlBox, !s.active);
  hide(ring, !s.active);
  if (!s.active) hide(hintL, true);
  if (strcmp(lastId, s.id)) {
    snprintf(lastId, sizeof(lastId), "%s", s.id);
    lv_label_set_text(titleL, s.active ? s.title : "");
    lv_label_set_text(phIcon, s.src == music::SRC_RADIO ? I_RADIO : I_ALBUM);  // Radio ohne Logo
    lv_obj_add_flag(cover, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(coverPh, LV_OBJ_FLAG_HIDDEN);
  }
  // Hinweis statt Interpret, solange gepuffert wird oder etwas schiefging
  const char* sub = !s.active ? "" : s.msg[0] ? s.msg : s.buffering ? "Lädt …" : s.artist;
  if (strcmp(lv_label_get_text(artistL), sub)) lv_label_set_text(artistL, sub);
  lv_obj_set_style_text_color(artistL, s.msg[0] ? C_TIMER() : C_DIM(), 0);
  lv_label_set_text(lv_obj_get_child(playB, 0), s.playing ? I_PAUSE : I_PLAY);

  uint32_t v;
  const uint16_t* px = music::art(&v);
  if (px && v != artSeen) {
    artSeen = v;
    artDsc.header.magic = LV_IMAGE_HEADER_MAGIC;
    artDsc.header.cf = LV_COLOR_FORMAT_RGB565;
    artDsc.header.w = music::ART;
    artDsc.header.h = music::ART;
    artDsc.header.stride = music::ART * 2;
    artDsc.data = (const uint8_t*)px;
    artDsc.data_size = music::ART * music::ART * 2;
    lv_image_cache_drop(&artDsc);
    lv_image_set_src(cover, &artDsc);
    lv_obj_remove_flag(cover, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(coverPh, LV_OBJ_FLAG_HIDDEN);
  }

  int p = s.active && s.dur ? (int)(s.pos * 1000 / s.dur) : 0;
  lv_arc_set_value(ring, constrain(p, 0, 1000));
}

static void resume() { artSeen = 0xFFFFFFFF; lastId[0] = 0; }

const Page PAGE_MUSIC = {build, update, nullptr, resume, false};

// ================================================================== Listen der Brücke

struct BrowseCtx {
  char path[240];
  uint32_t seq;      // browseSeq() beim Anfragen
  bool waiting;
  lv_obj_t* list;
  lv_obj_t* spin;
  lv_obj_t* mini;    // „läuft gerade“-Knopf
  music::Item* items;
  int n;
};

static char pendingPath[240];

// ================================================================== Suchen (Sprache oder Tastatur)

// Suche für vier Quellen: Sprache oder Tastatur
enum SearchMode { S_SPOTIFY, S_PODCAST, S_RADIO, S_ARD };
static int searchMode = S_SPOTIFY;
static const char* const S_TITLE[] = {"Spotify durchsuchen", "Podcast suchen", "Sender suchen", "Audiothek durchsuchen"};
static const char* const S_PREFIX[] = {"search:", "pcsearch:", "rlsearch:", "ardsearch:"};
static lv_color_t sColor() {
  return searchMode == S_PODCAST ? C_ALARM() : searchMode == S_RADIO ? C_WATCH() : searchMode == S_ARD ? C_TIMER() : C_RADIO();
}
static lv_obj_t *sMic, *sHalo, *sStatus, *sTitle;
static bool sHolding = false;
static uint32_t sDoneAt = 0;

static void runSearch(const char* q) {
  if (!q[0]) return;
  char p[160];
  snprintf(p, sizeof(p), "%s%s", S_PREFIX[searchMode], q);
  snprintf(pendingPath, sizeof(pendingPath), "%s", p);
  push(&PAGE_BROWSE);
}

static void searchKeyboard() {
  keyboard(S_TITLE[searchMode], "", false, [](const char* q) { runSearch(q); });
}

static void searchBuild(lv_obj_t* scr) {
  lv_color_t c = sColor();
  sTitle = title(scr, S_TITLE[searchMode], c);
  sHalo = lv_obj_create(scr);
  lv_obj_set_style_radius(sHalo, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_border_width(sHalo, 0, 0);
  lv_obj_set_style_bg_color(sHalo, c, 0);
  lv_obj_set_style_bg_opa(sHalo, LV_OPA_30, 0);
  lv_obj_remove_flag(sHalo, (lv_obj_flag_t)(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));
  sMic = lv_obj_create(scr);
  lv_obj_set_size(sMic, 170, 170);
  lv_obj_align(sMic, LV_ALIGN_CENTER, 0, -6);
  lv_obj_set_style_radius(sMic, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_border_width(sMic, 0, 0);
  lv_obj_set_style_bg_color(sMic, c, 0);
  lv_obj_set_style_bg_color(sMic, C_RED(), LV_STATE_PRESSED);
  lv_obj_remove_flag(sMic, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE));
  lv_obj_t* m = label(sMic, &font_icon_64, C_BG(), I_MIC);
  lv_obj_center(m);
  lv_obj_add_event_cb(sMic, [](lv_event_t*) { sHolding = true; voice::start(); play(3); }, LV_EVENT_PRESSED, nullptr);
  for (lv_event_code_t code : {LV_EVENT_RELEASED, LV_EVENT_PRESS_LOST})
    lv_obj_add_event_cb(sMic, [](lv_event_t*) { if (sHolding) { sHolding = false; voice::finish(); play(4); } }, code, nullptr);
  sStatus = label(scr, &font_text_24, C_TEXT(), "");
  lv_label_set_long_mode(sStatus, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(sStatus, 330);
  lv_obj_set_style_text_align(sStatus, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(sStatus, LV_ALIGN_CENTER, 0, 128);
  lv_obj_t* kb = pill(scr, I_ENTER "  Tastatur", C_SURF(), C_TEXT());
  lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, -26);
  lv_obj_add_event_cb(kb, [](lv_event_t*) { searchKeyboard(); }, LV_EVENT_CLICKED, nullptr);
  if (!voice::configured()) lv_label_set_text(sStatus, "Gemini-Schlüssel fehlt – Tastatur nutzen");
  voice::cancel();
  sDoneAt = 0;
}

static void searchUpdate() {
  voice::Phase p = voice::phase();
  float lvl = p == voice::LISTENING ? voice::level() : 0;
  int s = 170 + (int)(fminf(1.0f, lvl * 1.5f) * 100);
  lv_obj_set_size(sHalo, s, s);
  lv_obj_align(sHalo, LV_ALIGN_CENTER, 0, -6);
  const char* t = "";
  if (p == voice::IDLE) t = voice::configured() ? "Halten und sprechen" : "Gemini-Schlüssel fehlt – Tastatur nutzen";
  else if (p == voice::LISTENING) t = "Ich höre zu …";
  else if (p == voice::THINKING) t = "Verstehe …";
  else t = voice::text();
  if (strcmp(lv_label_get_text(sStatus), t)) lv_label_set_text(sStatus, t);
  lv_obj_set_style_text_color(sStatus, p == voice::FAILED ? C_TIMER() : C_TEXT(), 0);
  // erkannt: kurz zeigen, dann suchen
  if (p == voice::DONE && !sDoneAt) sDoneAt = millis();
  if (sDoneAt && millis() - sDoneAt > 700) {
    sDoneAt = 0;
    char q[96];
    snprintf(q, sizeof(q), "%s", voice::text());
    voice::cancel();
    runSearch(q);
  }
}

static void searchLeave() { voice::cancel(); }

const Page PAGE_SEARCH = {searchBuild, searchUpdate, searchLeave, nullptr, false};

void browseOpen(const char* path) {
  if (!strcmp(path, "@voice") || !strcmp(path, "@pcsearch") || !strcmp(path, "@radiosearch") || !strcmp(path, "@ardsearch")) {
    searchMode = path[1] == 'p' ? S_PODCAST : path[1] == 'r' ? S_RADIO : path[1] == 'a' ? S_ARD : S_SPOTIFY;
    push(&PAGE_SEARCH);
    return;
  }
  if (!strcmp(path, "@search")) {
    searchMode = S_SPOTIFY;
    searchKeyboard();
    return;
  }
  if (!strcmp(path, "@spotifyhelp")) {
    toast("Spotify-App öffnen → Lautsprecher-Symbol → diese Uhr wählen. Danach geht es auch ohne App.", 7000);
    return;
  }
  snprintf(pendingPath, sizeof(pendingPath), "%s", path);
  push(&PAGE_BROWSE);
}

static BrowseCtx* ctx() {
  lv_obj_t* s = topScreen();
  return s ? (BrowseCtx*)lv_obj_get_user_data(s) : nullptr;
}

static void itemClicked(lv_event_t* e) {
  BrowseCtx* c = ctx();
  int i = (int)(intptr_t)lv_event_get_user_data(e);
  if (!c || i >= c->n) return;
  const music::Item& it = c->items[i];
  play(0);
  if (it.kind == music::K_LIST) browseOpen(it.id);
  else { music::playList(c->items, c->n, i); show(&PAGE_MUSIC); }
}


static void fill(BrowseCtx* c) {
  char title[72], err[64];
  if (!c->items) c->items = (music::Item*)lv_malloc(sizeof(music::Item) * 80);
  c->n = music::browseCopy(title, sizeof(title), c->items, 80, err, sizeof(err));
  lv_obj_clean(c->list);
  lv_obj_t* t = label(c->list, &font_text_24, C_MUSIC(), title[0] ? title : "Musik");
  lv_label_set_long_mode(t, LV_LABEL_LONG_DOT);
  lv_obj_set_style_text_align(t, LV_TEXT_ALIGN_CENTER, 0);
  if (err[0]) {
    lv_obj_t* l = label(c->list, &font_text_20, C_DIM(), err);
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_t* r = pill(c->list, I_REFRESH "  Nochmal", C_SURF2(), C_TEXT());
    lv_obj_add_event_cb(r, [](lv_event_t*) {
      BrowseCtx* c = ctx();
      if (!c) return;
      c->seq = music::browseSeq();
      c->waiting = true;
      lv_obj_remove_flag(c->spin, LV_OBJ_FLAG_HIDDEN);
      music::browse(c->path);
    }, LV_EVENT_CLICKED, nullptr);
  } else if (!c->n) {
    label(c->list, &font_text_20, C_DIM(), "Keine Einträge");
  }
  for (int i = 0; i < c->n; i++) {
    const music::Item& it = c->items[i];
    char sub[80];
    if (it.kind == music::K_TRACK && it.dur >= 3600)
      snprintf(sub, sizeof(sub), "%s%s%d:%02d:%02d", it.sub, it.sub[0] ? " · " : "", it.dur / 3600, it.dur / 60 % 60, it.dur % 60);
    else if (it.kind == music::K_TRACK && it.dur)
      snprintf(sub, sizeof(sub), "%s%s%d:%02d", it.sub, it.sub[0] ? " · " : "", it.dur / 60, it.dur % 60);
    else
      snprintf(sub, sizeof(sub), "%s", it.sub);
    bool station = !strncmp(it.id, "radio:", 6);
    const char* icon = station                                                    ? I_RADIO
                       : it.kind == music::K_TRACK                                ? I_MUSIC
                       : !strncmp(it.id, "fav", 3)                               ? I_HEART
                       : !strcmp(it.id, "@search")                              ? I_SEARCH
                       : !strncmp(it.id, "@voice", 6) || strstr(it.id, "search") ? I_MIC
                       : !strncmp(it.id, "pc", 2)                                ? I_RADIO_WAVES
                       : !strncmp(it.id, "rl", 2)                                ? I_RADIO
                       : !strncmp(it.id, "ard", 3)                               ? I_LIBRARY
                       : !strcmp(it.id, "sp:home")                               ? I_MUSIC
                       : !strcmp(it.id, "@spotifyhelp")                          ? I_INFO
                                                                                 : I_QUEUE;
    lv_color_t ic = it.kind == music::K_TRACK ? C_DIM() : !strncmp(it.id, "fav:", 4) ? C_BRASS() : C_MUSIC();
    lv_obj_t* r = listRow(c->list, icon, ic, it.title, sub[0] ? sub : nullptr);
    lv_obj_add_event_cb(r, itemClicked, LV_EVENT_SHORT_CLICKED, (void*)(intptr_t)i);
    // Sender lange drücken: merken bzw. aus den Favoriten nehmen (nicht im Kindermodus)
    if (station && !kids::on())
      lv_obj_add_event_cb(r, [](lv_event_t* e) {
        BrowseCtx* c = ctx();
        int k = (int)(intptr_t)lv_event_get_user_data(e);
        if (!c || k >= c->n) return;
        const music::Item& s = c->items[k];
        bool had = favs::has(s.id);
        favs::toggle(s.id, s.title, s.sub);
        play(had ? 0 : 1);
        toast(had ? "Sender aus den Favoriten entfernt" : "Sender gemerkt");
      }, LV_EVENT_LONG_PRESSED, (void*)(intptr_t)i);
  }
  listFit(c->list);
}

static void browseBuild(lv_obj_t* scr) {
  BrowseCtx* c = (BrowseCtx*)lv_malloc_zeroed(sizeof(BrowseCtx));
  snprintf(c->path, sizeof(c->path), "%s", pendingPath);
  lv_obj_set_user_data(scr, c);
  lv_obj_add_event_cb(scr, [](lv_event_t* e) {
    BrowseCtx* c = (BrowseCtx*)lv_event_get_user_data(e);
    if (c->items) lv_free(c->items);
    lv_free(c);
  }, LV_EVENT_DELETE, c);

  c->list = roundList(scr, 70, 40);
  c->spin = lv_spinner_create(scr);
  lv_obj_set_size(c->spin, 64, 64);
  lv_obj_center(c->spin);
  lv_obj_set_style_arc_color(c->spin, C_MUSIC(), LV_PART_INDICATOR);
  lv_obj_set_style_arc_width(c->spin, 6, LV_PART_INDICATOR);
  lv_obj_set_style_arc_width(c->spin, 6, LV_PART_MAIN);
  lv_obj_set_style_arc_color(c->spin, C_SURF(), LV_PART_MAIN);

  c->mini = pill(scr, I_MUSIC "  Läuft gerade", C_MUSIC(), C_BG());
  lv_obj_align(c->mini, LV_ALIGN_BOTTOM_MID, 0, -22);
  lv_obj_add_event_cb(c->mini, [](lv_event_t*) { show(&PAGE_MUSIC); }, LV_EVENT_CLICKED, nullptr);
  lv_obj_add_flag(c->mini, LV_OBJ_FLAG_HIDDEN);

  c->seq = music::browseSeq();
  c->waiting = true;
  music::browse(c->path);
}

static void browseUpdate() {
  BrowseCtx* c = ctx();
  if (!c) return;
  if (c->waiting && music::browseSeq() != c->seq && !music::browseBusy()) {
    c->waiting = false;
    lv_obj_add_flag(c->spin, LV_OBJ_FLAG_HIDDEN);
    fill(c);
  }
  bool act = music::state().active;
  if (act == lv_obj_has_flag(c->mini, LV_OBJ_FLAG_HIDDEN)) {
    if (act) lv_obj_remove_flag(c->mini, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(c->mini, LV_OBJ_FLAG_HIDDEN);
  }
}

const Page PAGE_BROWSE = {browseBuild, browseUpdate, nullptr, nullptr, false};
