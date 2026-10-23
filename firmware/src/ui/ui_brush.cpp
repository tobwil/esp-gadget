// Zähneputzen mit Pferd Luna oder Reh Fips: zwei Minuten, vier Viertel à 30 s (oben links, oben rechts, unten links,
// unten rechts – das Gebiss im Bild zeigt, wo).
//   Erst die Figur wählen, dann die Aufgabe:
//   Geschichte – geht mit jedem Putzen ein Kapitel weiter; am Ende ein Teil für die Sammlung (Hufeisen bzw. Glühwürmchen)
//   Rätsel     – in jedem Viertel tauchen Dinge auf (zählen, Farben merken), danach Fragen zum Antippen;
//                leicht (ab 4) oder knifflig (ab 7, mit Zusammenrechnen und Reihenfolge), einstellbar in der Weboberfläche
//   Zum Schluss Sterne, und „Zähne putzen“ in der laufenden Routine wird abgehakt.
#include <Preferences.h>
#include <esp_random.h>
#include <ctype.h>
#include <math.h>
#include "../audio.h"
#include "../kidart.h"
#include "../kids.h"
#include "../routines.h"
#include "ui.h"

using namespace ui;
using kidart::Hero;
using kidart::QuizBeat;

namespace {

constexpr int S = kidart::S, Z = 4, OUT = S * Z;
constexpr uint32_t QUARTER_MS = 30000;
enum Step { PICK, MODE, RUN, QUIZ, DONE };

Step step = PICK;
Hero hero = kidart::LUNA;
bool quizMode = false;
uint32_t t0 = 0;
int lastBeat = -1, lastFrame = -1;
uint16_t *px = nullptr, *big = nullptr;
lv_obj_t *scrRef, *canvas, *topL, *botL, *panel = nullptr;
QuizBeat qb[4];

struct Question {
  char text[96];
  int n;             // Anzahl der Antworten (2 oder 3)
  char opt[3][24];
  int color[3];      // -1 = keine Farbe
  int correct;
};
Question qs[3];
int qn = 0, qi = 0, rightAnswers = 0;

// ------------------------------------------------------------------ Fortschritt (Kapitel, Sammlung)

int stored(const char* key) {
  Preferences p;
  p.begin("brush", true);
  int v = p.getUShort(key, 0);
  p.end();
  return v;
}

void bump(const char* key) {
  Preferences p;
  p.begin("brush", false);
  p.putUShort(key, p.getUShort(key, 0) + 1);
  p.end();
}

int chapterNow() { return stored(hero == kidart::LUNA ? "c0" : "c1") % 7; }

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

// Ring am Rand: vier Viertel, was geschafft ist, leuchtet
void ring(float done) {
  static const uint32_t QC[4] = {0x6ff2ff, 0x7ed957, 0xffd23f, 0xff8fc8};
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
      int q = std::min(3, (int)(u * 4));
      bool gap = fmodf(u * 4, 1.0f) < 0.015f;
      uint16_t c = gap ? 0 : kidart::rgb(QC[q]);
      if (!gap && u > done) c = kidart::mix(c, 0, 190);
      px[y * S + x] = c;
    }
}

void blackBelowScene() {
  for (int y = 96; y < S; y++)
    for (int x = 0; x < S; x++) px[y * S + x] = 0;
}

void drawRun(int beat, float prog, int f) {
  if (quizMode) kidart::drawBrushQuiz(px, hero, qb[beat], beat, f);
  else kidart::drawBrushStory(px, hero, chapterNow(), beat, prog, f);
  blackBelowScene();
  ring((beat + prog) / 4.0f);
  blit();
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

void setTop(const char* t) { lv_label_set_text(topL, t); hide(topL, !t[0]); }
void setBottom(const char* t) { lv_label_set_text(botL, t); }

lv_obj_t* choice(const char* text, lv_color_t bg, lv_color_t fg, int x, int y, lv_event_cb_t cb, intptr_t v) {
  lv_obj_t* b = pill(panel, text, bg, fg);
  lv_obj_align(b, LV_ALIGN_TOP_MID, x, y);
  lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, (void*)v);
  return b;
}

void showMode();
void start();
void showQuestion();
void finish();

void showPick() {
  step = PICK;
  kidart::drawBrushPick(px, 0);
  blackBelowScene();
  ring(0);
  blit();
  setTop("Wer putzt mit?");
  setBottom("Zwei Minuten – das Bild zeigt, wo du putzt.");
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
  kidart::drawBrushStory(px, hero, chapterNow(), 0, 0, 0);
  blackBelowScene();
  ring(0);
  blit();
  setTop("Was machen wir?");
  char b[80];
  int have = stored(hero == kidart::LUNA ? "n0" : "n1");
  if (have) snprintf(b, sizeof(b), "Gesammelt: %d %s", have, hero == kidart::LUNA ? "goldene Hufeisen" : "Glühwürmchen");
  else snprintf(b, sizeof(b), "Bei jeder Geschichte gibt es etwas für die Sammlung.");
  setBottom(b);
  clearPanel();
  char t[48];
  snprintf(t, sizeof(t), "Geschichte · Kapitel %d", chapterNow() + 1);
  choice(t, C_BRASS(), C_BG(), 0, 200, [](lv_event_t*) { quizMode = false; start(); }, 0);
  choice(kids::cfg().brushHard ? "Rätsel · knifflig" : "Rätsel", lv_color_hex(0x8a6bbf), C_TEXT(), 0, 270,
         [](lv_event_t*) { quizMode = true; start(); }, 0);
}

// ------------------------------------------------------------------ Rätsel

int rnd(int n) { return (int)(esp_random() % (uint32_t)n); }
bool colored(int o) { return o == kidart::Q_FLOWER || o == kidart::Q_BUTTERFLY || o == kidart::Q_BIRD || o == kidart::Q_MUSHROOM; }

void makeBeats(bool hard) {
  int prev = -1;
  for (int b = 0; b < 4; b++) {
    int o;
    do o = rnd(kidart::Q_OBJS); while (o == prev);
    prev = o;
    qb[b] = {(uint8_t)o, (uint8_t)(hard ? 3 + rnd(10) : 1 + rnd(5)), (uint8_t)rnd(kidart::Q_COLORS)};
  }
}

void shuffle(Question& q) {
  for (int i = q.n - 1; i > 0; i--) {
    int j = rnd(i + 1);
    std::swap(q.opt[i], q.opt[j]);
    std::swap(q.color[i], q.color[j]);
    if (q.correct == i) q.correct = j;
    else if (q.correct == j) q.correct = i;
  }
}

void countQuestion(Question& q, int correct, int maxv) {
  q.n = 3;
  int v[3] = {correct, 0, 0};
  for (int k = 1; k < 3; k++) {
    int c;
    do c = correct + (rnd(2) ? 1 : -1) * (1 + rnd(2)); while (c < 1 || c > maxv || c == v[0] || (k == 2 && c == v[1]));
    v[k] = c;
  }
  for (int k = 0; k < 3; k++) { snprintf(q.opt[k], sizeof(q.opt[k]), "%d", v[k]); q.color[k] = -1; }
  q.correct = 0;
  shuffle(q);
}

void makeQuestions(bool hard) {
  qn = 0;
  // 1: zählen
  int b = rnd(4);
  Question& a = qs[qn++];
  snprintf(a.text, sizeof(a.text), "Wie viele %s waren es?", kidart::quizName((kidart::QuizObj)qb[b].obj, true));
  countQuestion(a, qb[b].count, hard ? 15 : 7);
  // 2: Farbe, wenn es etwas Farbiges gab – sonst noch einmal zählen
  int c = -1;
  for (int k = 0; k < 4; k++) if (colored(qb[k].obj) && (c < 0 || k != b)) c = k;
  Question& q2 = qs[qn++];
  if (c >= 0) {
    snprintf(q2.text, sizeof(q2.text), "Welche Farbe hatten die %s?", kidart::quizName((kidart::QuizObj)qb[c].obj, true));
    q2.n = 3;
    int col[3] = {qb[c].color, 0, 0};
    for (int k = 1; k < 3; k++) {
      int x;
      do x = rnd(kidart::Q_COLORS); while (x == col[0] || (k == 2 && x == col[1]));
      col[k] = x;
    }
    for (int k = 0; k < 3; k++) { snprintf(q2.opt[k], sizeof(q2.opt[k]), "%s", kidart::quizColor(col[k])); q2.color[k] = col[k]; }
    q2.correct = 0;
    shuffle(q2);
  } else {
    int d = (b + 2) % 4;
    snprintf(q2.text, sizeof(q2.text), "Und wie viele %s?", kidart::quizName((kidart::QuizObj)qb[d].obj, true));
    countQuestion(q2, qb[d].count, hard ? 15 : 7);
  }
  if (!hard) return;
  // 3 (knifflig): zusammenrechnen oder Reihenfolge
  Question& q3 = qs[qn++];
  int x = rnd(4), y;
  do y = rnd(4); while (y == x);
  if (x > y) std::swap(x, y);
  if (rnd(2)) {
    snprintf(q3.text, sizeof(q3.text), "%s und %s – wie viele zusammen?", kidart::quizName((kidart::QuizObj)qb[x].obj, true),
             kidart::quizName((kidart::QuizObj)qb[y].obj, true));
    countQuestion(q3, qb[x].count + qb[y].count, 24);
  } else {
    snprintf(q3.text, sizeof(q3.text), "Was kam zuerst?");
    q3.n = 2;
    snprintf(q3.opt[0], sizeof(q3.opt[0]), "%s", kidart::quizName((kidart::QuizObj)qb[x].obj, true));
    snprintf(q3.opt[1], sizeof(q3.opt[1]), "%s", kidart::quizName((kidart::QuizObj)qb[y].obj, true));
    q3.color[0] = q3.color[1] = -1;
    q3.correct = 0;
    shuffle(q3);
  }
}

void answer(lv_event_t* e) {
  int k = (int)(intptr_t)lv_event_get_user_data(e);
  if (step != QUIZ || !panel) return;
  Question& q = qs[qi];
  bool ok = k == q.correct;
  rightAnswers += ok;
  audio::play(ok ? audio::FX_OK : audio::FX_ERROR);
  // Rückmeldung: richtige Antwort grün, falsche rot
  for (int i = 0; i < q.n; i++) {
    lv_obj_t* b = lv_obj_get_child(panel, i + 1);
    if (i == q.correct) lv_obj_set_style_border_color(b, lv_color_hex(0x3ddc97), 0);
    else if (i == k) lv_obj_set_style_border_color(b, C_RED(), 0);
    lv_obj_set_style_border_width(b, i == q.correct || i == k ? 6 : 0, 0);
    lv_obj_remove_flag(b, LV_OBJ_FLAG_CLICKABLE);
  }
  setBottom(ok ? "Richtig!" : "Fast – die richtige ist grün.");
  lv_timer_t* t = lv_timer_create([](lv_timer_t*) {
    if (step != QUIZ) return;
    qi++;
    if (qi < qn) showQuestion();
    else finish();
  }, 1600, nullptr);
  lv_timer_set_repeat_count(t, 1);
}

void showQuestion() {
  Question& q = qs[qi];
  setTop("");
  char b[24];
  snprintf(b, sizeof(b), "Frage %d von %d", qi + 1, qn);
  setBottom(b);
  clearPanel();
  lv_obj_set_style_bg_color(panel, C_BG(), 0);
  lv_obj_set_style_bg_opa(panel, LV_OPA_80, 0);
  lv_obj_t* l = label(panel, &font_text_24, C_TEXT(), q.text);
  lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(l, 330);
  lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(l, LV_ALIGN_TOP_MID, 0, 110);
  int size = 112, gap = q.n == 3 ? 128 : 150;
  for (int i = 0; i < q.n; i++) {
    lv_color_t bg = q.color[i] >= 0 ? lv_color_hex(kidart::quizColorRgb(q.color[i])) : C_SURF2();
    lv_obj_t* r = lv_obj_create(panel);
    lv_obj_remove_style_all(r);
    lv_obj_set_size(r, size, size);
    lv_obj_set_style_radius(r, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(r, bg, 0);
    lv_obj_set_style_bg_opa(r, LV_OPA_COVER, 0);
    lv_obj_add_flag(r, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_align(r, LV_ALIGN_TOP_MID, (int)((i - (q.n - 1) / 2.0f) * gap), 236);
    lv_obj_t* t = label(r, q.opt[i][0] && isdigit((unsigned char)q.opt[i][0]) ? &font_text_32 : &font_text_20,
                        q.color[i] >= 0 && q.color[i] != 2 ? lv_color_white() : q.color[i] == 2 ? C_BG() : C_TEXT(), q.opt[i]);
    lv_label_set_long_mode(t, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(t, size - 12);
    lv_obj_set_style_text_align(t, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(t);
    lv_obj_add_event_cb(r, answer, LV_EVENT_CLICKED, (void*)(intptr_t)i);
  }
}

// ------------------------------------------------------------------ Ablauf

void start() {
  step = RUN;
  if (quizMode) makeBeats(kids::cfg().brushHard);
  t0 = millis();
  lastBeat = lastFrame = -1;
  if (panel) { lv_obj_delete(panel); panel = nullptr; }
  play(1);
}

void finish() {
  if (quizMode && step == RUN) {  // erst die Fragen
    step = QUIZ;
    qi = rightAnswers = 0;
    makeQuestions(kids::cfg().brushHard);
    kidart::QuizBeat none{qb[3].obj, 0, 0};
    kidart::drawBrushQuiz(px, hero, none, 4, 0);
    blackBelowScene();
    ring(1);
    blit();
    showQuestion();
    return;
  }
  step = DONE;
  int stars = routines::checkMatching("ähne") + routines::checkMatching("Zahn");  // „Zähne putzen“ in der Routine
  char line[96];
  if (quizMode) {
    if (rightAnswers) routines::addStars(rightAnswers);
    stars += rightAnswers;
    snprintf(line, sizeof(line), "%d von %d richtig!", rightAnswers, qn);
  } else {
    bump(hero == kidart::LUNA ? "n0" : "n1");
    bump(hero == kidart::LUNA ? "c0" : "c1");
    int have = stored(hero == kidart::LUNA ? "n0" : "n1");
    snprintf(line, sizeof(line), hero == kidart::LUNA ? "Ein goldenes Hufeisen! Jetzt %d." : "Ein neues Glühwürmchen! Jetzt %d.", have);
  }
  kidart::drawBrushStory(px, hero, 0, 4, 0, 0);
  blackBelowScene();
  ring(1);
  blit();
  audio::play(audio::FX_CHIME);
  setTop("Super geputzt!");
  char b[128];
  if (stars > 0) snprintf(b, sizeof(b), "%s  +%d Sterne", line, stars);
  else snprintf(b, sizeof(b), "%s", line);
  setBottom(b);
  clearPanel();
  choice("Fertig", C_BRASS(), C_BG(), 0, 290, [](lv_event_t*) { back(); }, 0);
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
  if (step != RUN) return;
  uint32_t el = millis() - t0;
  int beat = el / QUARTER_MS;
  if (beat >= 4) { finish(); return; }
  float prog = (el % QUARTER_MS) / (float)QUARTER_MS;
  if (beat != lastBeat) {
    lastBeat = beat;
    if (beat) audio::play(audio::FX_CHIME);
    static const char* const WHERE[4] = {"Oben links", "Oben rechts", "Unten links", "Unten rechts"};
    setTop(WHERE[beat]);
    char b[96];
    if (!quizMode) snprintf(b, sizeof(b), "%s", kidart::brushText(hero, chapterNow(), beat));
    else if (kids::cfg().brushHard) snprintf(b, sizeof(b), "Gut hinschauen und alles merken!");
    else snprintf(b, sizeof(b), colored(qb[beat].obj) ? "Zähl die %s – und merk dir die Farbe!" : "Zähl mit: Wie viele %s?",
                  kidart::quizName((kidart::QuizObj)qb[beat].obj, true));
    setBottom(b);
  }
  int f = millis() / 350;
  if (f == lastFrame) return;
  lastFrame = f;
  drawRun(beat, prog, f);
}

void leave() {
  panel = nullptr;
  step = PICK;  // ein noch laufender Antwort-Timer tut dann nichts mehr
  if (big) { heap_caps_free(big); big = nullptr; }
  if (px) { heap_caps_free(px); px = nullptr; }
}

}  // namespace

void brushStats(int& lunaChapter, int& lunaShoes, int& fipsChapter, int& fipsFlies) {
  lunaChapter = stored("c0") % 7 + 1;
  lunaShoes = stored("n0");
  fipsChapter = stored("c1") % 7 + 1;
  fipsFlies = stored("n1");
}

const Page PAGE_BRUSH = {build, update, leave, nullptr, false};
