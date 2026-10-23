// Zähneputzen mit Pferd Luna oder Reh Fips – nach der KAI-Methode, zwei Minuten:
//   Kauflächen (4 × 10 s, hin und her), Außenflächen (3 × ≈ 13 s, Zähne aufeinander, kleine Kreise),
//   Innenflächen (2 × ≈ 20 s, von Rot nach Weiß). Der Mund im Bild zeigt, wo; die Figur sagt es an (Sprachclips).
//   Erst die Figur wählen, dann:
//   Hörgeschichte – nach jeder Ansage erzählt die Figur ein Stück weiter; mit jedem Putzen ein Kapitel (7 je Figur)
//   Summ-Ballon   – Summen lässt den Ballon steigen (je höher der Ton, desto höher), Sterne einsammeln; Stille = sinken
//   Album         – Sticker, die es nach jedem Putzen gibt; 7 Tage am Stück: Pokal und Krone
//   Putzen zwei Uhren gleichzeitig, zeigen sie sich gegenseitig an (Funk) und es gibt Team-Herzen.
//   Am Ende: „Ausspucken, nicht ausspülen“, auf Wunsch „Mama oder Papa putzt nach“; „Zähne putzen“ in der Routine
//   wird abgehakt.
#include <Preferences.h>
#include <esp_random.h>
#include <math.h>
#include "../audio.h"
#include "../clips.h"
#include "../kidart.h"
#include "../kids.h"
#include "../net.h"
#include "../radio.h"
#include "../routines.h"
#include "ui.h"

using namespace ui;
using kidart::Hero;

namespace {

constexpr int S = kidart::S, Z = 4, OUT = S * Z;
enum Step { PICK, MODE, ALBUM, INTRO, RUN, DONE };
enum Mode : uint8_t { STORY, BALLOON };

// KAI: neun Abschnitte
struct Section { kidart::MouthView view; uint8_t area; uint16_t sec; const char* cue; const char* where; const char* how; };
const Section SEC[9] = {
    {kidart::M_CHEW, 0, 10, "c/k1", "Oben rechts", "Kauflächen: hin und her"},
    {kidart::M_CHEW, 1, 10, "c/k2", "Oben links", "Kauflächen: hin und her"},
    {kidart::M_CHEW, 2, 10, "c/k3", "Unten links", "Kauflächen: hin und her"},
    {kidart::M_CHEW, 3, 10, "c/k4", "Unten rechts", "Kauflächen: hin und her"},
    {kidart::M_OUTER, 0, 14, "c/a1", "Außen rechts", "Zähne zusammen, kleine Kreise"},
    {kidart::M_OUTER, 1, 13, "c/a2", "Vorne", "Zähne zusammen, kleine Kreise"},
    {kidart::M_OUTER, 2, 14, "c/a3", "Außen links", "Zähne zusammen, kleine Kreise"},
    {kidart::M_INNER, 0, 19, "c/i1", "Innen oben", "Von Rot nach Weiß"},
    {kidart::M_INNER, 1, 20, "c/i2", "Innen unten", "Von Rot nach Weiß"},
};
constexpr int SECTIONS = 9;
constexpr uint32_t OVERRUN_MS = 5000;  // ein Stück Geschichte darf einen Abschnitt so viel verlängern

Step step = PICK;
Hero hero = kidart::LUNA;
Mode mode = STORY;
int sec = 0, chapter = 0, albumPage = 0, frame = 0;
uint32_t secStart = 0, introStart = 0, lastFrameAt = 0, lastSend = 0, hintAt = 0;
bool teamSeen = false, hintTeam = false;
uint16_t *px = nullptr, *big = nullptr;
lv_obj_t *scrRef, *canvas, *topL, *botL, *panel = nullptr;
int awarded[3], nAwarded = 0, showAward = 0;

// ------------------------------------------------------------------ Speicher (Namensraum „brush“)
//   c0/c1 Kapitel Luna/Fips · al Album (Bitmaske) · sd letzter Putztag · st Serie · sb beste Serie · hb Ballon-Rekord

uint32_t getU(const char* key) {
  Preferences p;
  p.begin("brush", true);
  uint32_t v = p.getUInt(key, 0);
  p.end();
  return v;
}

void putU(const char* key, uint32_t v) {
  Preferences p;
  p.begin("brush", false);
  p.putUInt(key, v);
  p.end();
}

// c0/c1 stammen noch aus der ersten Version (UShort)
int chapterOf(Hero h) {
  Preferences p;
  p.begin("brush", true);
  int v = p.getUShort(h == kidart::LUNA ? "c0" : "c1", 0);
  p.end();
  return v % 7;
}

void nextChapter(Hero h) {
  Preferences p;
  p.begin("brush", false);
  const char* k = h == kidart::LUNA ? "c0" : "c1";
  p.putUShort(k, p.getUShort(k, 0) + 1);
  p.end();
}

int today() {  // Tage seit 1970 nach Ortszeit, -1 = Uhrzeit unbekannt
  if (!timeValid()) return -1;
  time_t now = time(nullptr);
  struct tm t;
  localtime_r(&now, &t);
  t.tm_hour = 12;
  t.tm_min = t.tm_sec = 0;
  return (int)(mktime(&t) / 86400);
}

int currentStreak() {  // gilt noch, wenn heute oder gestern geputzt wurde
  int d = today(), last = getU("sd");
  return d >= 0 && last && d - last <= 1 ? getU("st") : 0;
}

uint8_t crowns() { return currentStreak() >= 7 ? 3 : 0; }

// ------------------------------------------------------------------ Zeichnen

void blit() {
  for (int y = 0; y < S; y++) {
    uint16_t* row = big + y * Z * OUT;
    const uint16_t* src = px + y * S;
    for (int x = 0; x < S; x++) {
      uint16_t c = src[x];
      uint16_t* d = row + x * Z;
      d[0] = d[1] = d[2] = d[3] = c;
    }
    for (int r = 1; r < Z; r++) memcpy(row + r * OUT, row, OUT * 2);
  }
  lv_obj_invalidate(canvas);
}

// Ring am Rand: neun Abschnitte, Farbe je Fläche (Kauflächen, außen, innen); was geschafft ist, leuchtet
void ring(float done) {
  static const uint32_t PC[3] = {0x6ff2ff, 0x7ed957, 0xff8fc8};
  static float edge[SECTIONS + 1];
  if (!edge[SECTIONS]) {
    float t = 0;
    for (int i = 0; i < SECTIONS; i++) { edge[i] = t / 120.0f; t += SEC[i].sec; }
    edge[SECTIONS] = 1;
  }
  const float C = 57.5f;
  for (int y = 0; y < S; y++)
    for (int x = 0; x < S; x++) {
      float dx = x + 0.5f - C, dy = y + 0.5f - C, d2 = dx * dx + dy * dy;
      if (d2 > 57.5f * 57.5f) { px[y * S + x] = 0; continue; }
      if (d2 < 52.5f * 52.5f) continue;
      if (d2 < 53.5f * 53.5f) { px[y * S + x] = 0; continue; }
      float a = atan2f(dx, -dy);
      if (a < 0) a += 2 * (float)M_PI;
      float u = a / (2 * (float)M_PI);
      int i = 0;
      while (i < SECTIONS - 1 && u >= edge[i + 1]) i++;
      bool gap = u - edge[i] < 0.008f;
      uint16_t c = gap ? 0 : kidart::rgb(PC[SEC[i].view]);
      if (!gap && u > done) c = kidart::mix(c, 0, 190);
      px[y * S + x] = c;
    }
}

void blackBelowScene() {
  for (int y = 96; y < S; y++)
    for (int x = 0; x < S; x++) px[y * S + x] = 0;
}

float doneFraction() {
  if (step == DONE) return 1;
  if (step != RUN) return 0;
  float t = 0;
  for (int i = 0; i < sec; i++) t += SEC[i].sec;
  t += std::min((float)SEC[sec].sec, (millis() - secStart) / 1000.0f);
  return t / 120.0f;
}

void show() {  // Szene fertig: Ring, unten schwarz, auf den Bildschirm
  blackBelowScene();
  ring(doneFraction());
  blit();
}

// ------------------------------------------------------------------ Summ-Ballon: Mikrofon und Spiel

struct Hum {
  int16_t win[640];
  int have = 0;
  float floor = 0.004f, pitch = 0;
  bool voiced = false;
} hum;

// Neue Mikrofonwerte holen und die letzten 32 ms prüfen: laut genug und periodisch (Autokorrelation) = Summen.
// Vorher ein Tiefpass (≈ 900 Hz): das Zahnbürsten-Geräusch ist hell und breitbandig, Summen tief – so zählt Summen
// auch dann, wenn die Bürste dreimal so laut ist (am Rechner mit künstlichen Signalen geprüft). Tonhöhe 175 … 530 Hz.
void listen() {
  int16_t tmp[512];
  size_t n;
  while ((n = audio::micRead(tmp, 512)) > 0) {
    if (clips::busy()) continue;  // die eigene Ansage nicht mithören
    int keep = std::max(0, std::min(hum.have, 640 - (int)n));
    memmove(hum.win, hum.win + hum.have - keep, keep * 2);
    memcpy(hum.win + keep, tmp, n * 2);
    hum.have = keep + n;
  }
  hum.voiced = false;
  if (clips::busy() || hum.have < 512) return;
  static float x[512];
  const int16_t* w = hum.win + hum.have - 512;
  float e = 0, s1 = 0, s2 = 0;
  for (int i = 0; i < 512; i++) {
    float v = w[i] / 32768.0f;
    e += v * v;
    s1 += 0.3f * (v - s1);
    s2 += 0.3f * (s1 - s2);
    x[i] = s2;
  }
  float rms = sqrtf(e / 512);
  hum.floor += (rms - hum.floor) * (rms < hum.floor ? 0.3f : 0.01f);  // Grundrauschen: fällt schnell, steigt langsam
  if (rms < std::max(0.004f, hum.floor * 2.2f)) return;
  float r[92] = {0}, best = 0;
  for (int lag = 30; lag <= 91; lag++) {
    float s = 0, a = 0, b = 0;
    for (int i = 0; i + lag < 512; i++) { s += x[i] * x[i + lag]; a += x[i] * x[i]; b += x[i + lag] * x[i + lag]; }
    r[lag] = s / (sqrtf(a * b) + 1e-9f);
    if (lag <= 90) best = std::max(best, r[lag]);
  }
  if (best < 0.45f) return;
  // erster Gipfel nahe am besten – sonst landet man eine Oktave zu tief; dann zwischen den Abständen verfeinern
  int lag = 31;
  while (lag < 90 && !(r[lag] >= best * 0.88f && r[lag] >= r[lag - 1] && r[lag] >= r[lag + 1])) lag++;
  float d = r[lag - 1] - 2 * r[lag] + r[lag + 1];
  float off = d < 0 ? 0.5f * (r[lag - 1] - r[lag + 1]) / d : 0;
  hum.voiced = true;
  float f = 16000.0f / (lag + off);
  hum.pitch = hum.pitch ? hum.pitch * 0.6f + f * 0.4f : f;
}

constexpr int B_TOP = 1, B_BOTTOM = 20, MAXOBJ = 6;  // Ballon oben … auf dem Hügel
kidart::BalloonObj objs[MAXOBJ];
int nObj = 0, by = B_BOTTOM, score = 0, nextSpawn = 0;

void balloonTick() {
  listen();
  if (hum.voiced) {
    // 200 Hz unten … 420 Hz oben; wer summt, steigt immer etwas
    float u = std::min(1.0f, std::max(0.0f, (hum.pitch - 200) / 220.0f));
    int target = std::min(B_BOTTOM - 4, B_BOTTOM - (int)(u * (B_BOTTOM - B_TOP) + 0.5f));
    if (by > target) by -= by - target > 4 ? 2 : 1;
    else if (by < target) by++;
  } else if (!clips::busy() && frame % 2 == 0 && by < B_BOTTOM) {
    by++;  // Stille: langsam sinken (während einer Ansage schwebt er)
  }
  for (int k = 0; k < nObj; k++) {
    objs[k].x--;
    bool hit = objs[k].x >= kidart::BALLOON_X - 1 && objs[k].x <= kidart::BALLOON_X + kidart::BALLOON_W &&
               objs[k].y >= by - 1 && objs[k].y <= by + kidart::BALLOON_H;
    if (hit) {
      score += objs[k].kind ? 3 : 1;
      audio::play(objs[k].kind ? audio::FX_CHIME : audio::FX_TICK);
    }
    if (hit || objs[k].x < -3) { objs[k] = objs[--nObj]; k--; }
  }
  if (--nextSpawn <= 0 && nObj < MAXOBJ) {
    objs[nObj++] = {60, (int8_t)(3 + esp_random() % 32), (uint8_t)(esp_random() % 8 == 0)};
    nextSpawn = 8 + esp_random() % 8;
  }
}

// ------------------------------------------------------------------ Bedienelemente

void clearPanel() {
  if (panel) lv_obj_delete(panel);
  panel = lv_obj_create(scrRef);  // auf dem Bildschirm dieser Seite (beim Aufbau ist noch der alte aktiv)
  lv_obj_remove_style_all(panel);
  lv_obj_set_size(panel, W, H);
  lv_obj_remove_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_move_foreground(topL);
  lv_obj_move_foreground(botL);
}

void dropPanel() {
  if (panel) { lv_obj_delete(panel); panel = nullptr; }
}

void setTop(const char* t) { lv_label_set_text(topL, t); hide(topL, !t[0]); }
void setBottom(const char* t) { lv_label_set_text(botL, t); }

lv_obj_t* choice(const char* text, lv_color_t bg, lv_color_t fg, int x, int y, lv_event_cb_t cb, intptr_t v = 0) {
  lv_obj_t* b = pill(panel, text, bg, fg);
  lv_obj_align(b, LV_ALIGN_TOP_MID, x, y);
  lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, (void*)v);
  return b;
}

// Clip der gewählten Figur, z. B. „c/k1“ → „luna/c/k1“; fehlt er, wenigstens ein Klang
bool say(const char* part, bool chimeIfMissing = true) {
  char id[24];
  snprintf(id, sizeof(id), "%s/%s", hero == kidart::LUNA ? "luna" : "fips", part);
  if (clips::play(id)) return true;
  if (chimeIfMissing) audio::play(audio::FX_CHIME);
  return false;
}

void showPick();
void showMode();
void showAlbum();
void start(Mode m);
void finish();

void pickBottom() {
  char b[96];
  radio::BrushMate m;
  int st = currentStreak();
  if (radio::brushMate(&m) && !m.done) snprintf(b, sizeof(b), "%s putzt gerade – putz mit!", m.name);
  else if (st >= 2) snprintf(b, sizeof(b), "%d Tage am Stück geputzt!", st);
  else snprintf(b, sizeof(b), "Zwei Minuten – Luna und Fips sagen, wo.");
  setBottom(b);
}

void showPick() {
  step = PICK;
  kidart::drawBrushPick(px, 0, crowns());
  show();
  setTop("Wer putzt mit?");
  pickBottom();
  clearPanel();
  auto cb = [](lv_event_t* e) {
    hero = (Hero)(intptr_t)lv_event_get_user_data(e);
    play(1);
    showMode();
  };
  choice("Pferd Luna", lv_color_hex(0xb5703a), C_TEXT(), -92, 300, cb, kidart::LUNA);
  choice("Reh Fips", lv_color_hex(0xc98a4b), C_TEXT(), 92, 300, cb, kidart::FIPS);
}

void showMode() {
  step = MODE;
  chapter = chapterOf(hero);
  kidart::drawBrushStory(px, hero, chapter, 0, 0, 0);
  show();
  setTop(kidart::heroName(hero));
  char b[64];
  snprintf(b, sizeof(b), "Kapitel %d: %s", chapter + 1, kidart::brushChapter(hero, chapter));
  setBottom(b);
  clearPanel();
  choice("Hörgeschichte", C_BRASS(), C_BG(), 0, 170, [](lv_event_t*) { start(STORY); });
  choice("Summ-Ballon", lv_color_hex(0x5ec8ff), C_BG(), 0, 236, [](lv_event_t*) { start(BALLOON); });
  choice("Album", lv_color_hex(0x8a6bbf), C_TEXT(), 0, 302, [](lv_event_t*) {
    albumPage = 0;
    showAlbum();
  });
}

void showAlbum() {
  step = ALBUM;
  uint32_t have = getU("al");
  kidart::drawAlbum(px, have, albumPage, frame);
  show();
  setTop("Mein Album");
  char b[64];
  snprintf(b, sizeof(b), "%d von %d Stickern · Seite %d/3", __builtin_popcount(have), kidart::STICKERS, albumPage + 1);
  setBottom(b);
  clearPanel();
  choice("Zurück", C_SURF2(), C_TEXT(), -80, 326, [](lv_event_t*) { showMode(); });
  choice("Weiter", lv_color_hex(0x8a6bbf), C_TEXT(), 80, 326, [](lv_event_t*) {
    albumPage = (albumPage + 1) % 3;
    showAlbum();
  });
}

// ------------------------------------------------------------------ Ablauf

void enterSection(int i) {
  sec = i;
  secStart = millis();
  setTop(SEC[i].where);
  setBottom(SEC[i].how);
  hintAt = millis();
  say(SEC[i].cue);
  if (mode == STORY) {
    char part[8];
    snprintf(part, sizeof(part), "%d/%d", chapter, i);
    say(part, false);  // hinter die Ansage
  }
}

void start(Mode m) {
  mode = m;
  chapter = chapterOf(hero);
  step = INTRO;
  introStart = millis();
  teamSeen = hintTeam = false;
  score = 0;
  dropPanel();
  play(1);
  if (m == STORY) {
    char part[8];
    snprintf(part, sizeof(part), "%d/in", chapter);
    say(part);
    setTop(kidart::heroName(hero));
    char b[64];
    snprintf(b, sizeof(b), "Kapitel %d: %s", chapter + 1, kidart::brushChapter(hero, chapter));
    setBottom(b);
    kidart::drawBrushStory(px, hero, chapter, 0, 0, 0);
  } else {
    say("c/hum");
    nObj = 0;
    by = B_BOTTOM;
    nextSpawn = 4;
    hum = Hum();
    audio::micStart();
    setTop("Summ-Ballon");
    setBottom("Summen = steigen, Stille = sinken");
    kidart::drawBalloon(px, hero, by, objs, 0, SEC[0].view, SEC[0].area, false, 0);
  }
  show();
}

int pickSticker(uint32_t have) {  // einer, der noch fehlt; sind alle da, irgendeiner
  int miss[24], n = 0;
  for (int i = 0; i < 24; i++) if (!(have >> i & 1)) miss[n++] = i;
  return n ? miss[esp_random() % n] : (int)(esp_random() % 24);
}

void finish() {
  step = DONE;
  audio::micStop();
  radio::brushSend(hero, mode, SECTIONS, std::min(score, 255), true);
  clips::stop();
  // Belohnungen: ein Sticker, Team-Herzen, Pokal bei 7 Tagen am Stück
  uint32_t have = getU("al");
  nAwarded = showAward = 0;
  awarded[nAwarded++] = pickSticker(have);
  radio::BrushMate m;
  if (teamSeen || radio::brushMate(&m)) awarded[nAwarded++] = kidart::STICKER_TEAM;
  int d = today(), streak = currentStreak();
  bool trophy = false;
  if (d >= 0 && (int)getU("sd") != d) {  // erstes Putzen heute: Serie geht weiter
    streak++;
    putU("sd", d);
    putU("st", streak);
    if (streak > (int)getU("sb")) putU("sb", streak);
    if (streak % 7 == 0) { awarded[nAwarded++] = kidart::STICKER_TROPHY; trophy = true; }
  }
  for (int i = 0; i < nAwarded; i++) have |= 1u << awarded[i];
  putU("al", have);
  bool record = false;
  if (mode == STORY) nextChapter(hero);
  else if (score > (int)getU("hb")) { putU("hb", score); record = true; }
  int stars = routines::checkMatching("ähne") + routines::checkMatching("Zahn");  // „Zähne putzen“ in der Routine

  say("c/spit");
  if (kids::cfg().brushParent) say("c/parent", false);
  say(trophy ? "c/streak" : "c/sticker", false);
  audio::play(audio::FX_CHIME);

  kidart::drawSticker(px, awarded[0], 0);
  show();
  setTop("Super geputzt!");
  char b[128], extra[48] = "";
  if (mode == BALLOON) snprintf(extra, sizeof(extra), record ? " · Rekord: %d Sterne!" : " · %d Sterne", score);
  else if (stars > 0) snprintf(extra, sizeof(extra), " · +%d Sterne", stars);
  snprintf(b, sizeof(b), "Neuer Sticker: %s%s", kidart::stickerName(awarded[0]), extra);
  setBottom(b);
  clearPanel();
  choice("Fertig", C_BRASS(), C_BG(), 0, 300, [](lv_event_t*) { back(); });
}

void teamHint() {  // unten abwechselnd: wie putzen / was die andere Uhr macht
  radio::BrushMate m;
  bool mate = radio::brushMate(&m);
  if (mate && !teamSeen) say("c/team", false);  // „Deine Schwester putzt auch gerade …“
  if (mate) teamSeen = true;
  if (millis() - hintAt < 3500) return;
  hintAt = millis();
  hintTeam = mate && !hintTeam;
  if (!hintTeam) { setBottom(SEC[sec].how); return; }
  char b[80];
  if (m.done) snprintf(b, sizeof(b), "%s ist schon fertig!", m.name);
  else if (m.section < SECTIONS) snprintf(b, sizeof(b), "%s putzt mit: %s", m.name, SEC[m.section].where);
  else snprintf(b, sizeof(b), "%s putzt mit!", m.name);
  setBottom(b);
}

void build(lv_obj_t* scr) {
  scrRef = scr;
  if (!px) px = (uint16_t*)heap_caps_malloc(S * S * 2, MALLOC_CAP_SPIRAM);
  if (!big) big = (uint16_t*)heap_caps_malloc(OUT * OUT * 2, MALLOC_CAP_SPIRAM);
  canvas = lv_canvas_create(scr);
  lv_canvas_set_buffer(canvas, big, OUT, OUT, LV_COLOR_FORMAT_RGB565);
  lv_obj_set_pos(canvas, (W - OUT) / 2, (H - OUT) / 2);
  topL = label(scr, &font_text_24, C_TEXT(), "");
  lv_obj_set_style_bg_color(topL, C_BG(), 0);
  lv_obj_set_style_bg_opa(topL, LV_OPA_60, 0);
  lv_obj_set_style_radius(topL, 16, 0);
  lv_obj_set_style_pad_hor(topL, 14, 0);
  lv_obj_set_style_pad_ver(topL, 4, 0);
  lv_obj_align(topL, LV_ALIGN_TOP_MID, 0, 34);
  botL = label(scr, &font_text_20, C_TEXT(), "");
  lv_label_set_long_mode(botL, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(botL, 290);
  lv_obj_set_style_text_align(botL, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(botL, LV_ALIGN_TOP_MID, 0, 386);
  panel = nullptr;
  showPick();
}

void update() {
  hwActivity();  // Display bleibt an
  uint32_t now = millis();
  uint32_t tick = (step == RUN || step == INTRO) && mode == BALLOON ? 150 : 350;
  if (now - lastFrameAt < tick) return;
  lastFrameAt = now;
  frame++;

  switch (step) {
    case MODE:
      return;
    case PICK:
      kidart::drawBrushPick(px, frame, crowns());
      if (frame % 6 == 0) pickBottom();
      show();
      return;
    case ALBUM:
      kidart::drawAlbum(px, getU("al"), albumPage, frame);
      show();
      return;
    case DONE:
      if (frame % 7 == 0 && nAwarded > 1) {  // mehrere Sticker: abwechselnd zeigen
        showAward = (showAward + 1) % nAwarded;
        char b[64];
        snprintf(b, sizeof(b), "Neuer Sticker: %s", kidart::stickerName(awarded[showAward]));
        setBottom(b);
      }
      kidart::drawSticker(px, awarded[showAward], frame);
      show();
      return;
    case INTRO:  // erst die Begrüßung ausreden lassen (höchstens 12 s), dann läuft die Zeit
      if ((clips::busy() || now - introStart < 1200) && now - introStart < 12000) {
        if (mode == STORY) kidart::drawBrushStory(px, hero, chapter, 0, 0, frame);
        else {
          balloonTick();
          kidart::drawBalloon(px, hero, by, objs, nObj, SEC[0].view, SEC[0].area, hum.voiced, frame);
        }
        show();
        return;
      }
      clips::stop();
      step = RUN;
      enterSection(0);
      break;
    case RUN:
      break;
  }

  uint32_t in = now - secStart, len = SEC[sec].sec * 1000u;
  if (in >= len && (!clips::busy() || in >= len + OVERRUN_MS)) {  // nächster Abschnitt – ein Satz darf noch zu Ende gehen
    if (sec + 1 >= SECTIONS) { finish(); return; }
    clips::stop();
    audio::play(audio::FX_TICK);
    enterSection(sec + 1);
    in = 0;
  }
  if (now - lastSend > 2000) {
    lastSend = now;
    radio::brushSend(hero, mode, sec, std::min(score, 255), false);
  }
  teamHint();
  if (mode == BALLOON) {
    balloonTick();
    kidart::drawBalloon(px, hero, by, objs, nObj, SEC[sec].view, SEC[sec].area, hum.voiced, frame);
  } else {
    int germs = std::max(0, 4 - (int)(in * 5 / len));  // die Zahnmonster verschwinden nach und nach
    kidart::drawBrushGame(px, hero, SEC[sec].view, SEC[sec].area, germs, true, frame);
  }
  show();
}

void leave() {
  if (step != DONE) clips::stop();  // „Ausspucken …“ darf nach „Fertig“ noch ausreden
  audio::micStop();
  panel = nullptr;
  step = PICK;
  if (big) { heap_caps_free(big); big = nullptr; }
  if (px) { heap_caps_free(px); px = nullptr; }
}

}  // namespace

BrushStats brushStats() {
  BrushStats s{};
  s.lunaChapter = chapterOf(kidart::LUNA) + 1;
  s.fipsChapter = chapterOf(kidart::FIPS) + 1;
  s.streak = currentStreak();
  s.best = getU("sb");
  s.balloon = getU("hb");
  s.have = getU("al");
  return s;
}

const Page PAGE_BRUSH = {build, update, leave, nullptr, false};
