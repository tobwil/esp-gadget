// Zähneputzen mit Pferd Luna oder Reh Fips – nach der KAI-Methode, zwei Minuten:
//   Kauflächen (4 × 10 s, hin und her), Außenflächen (3 × ≈ 13 s, Zähne aufeinander, kleine Kreise),
//   Innenflächen (2 × ≈ 20 s, von Rot nach Weiß). Der kleine Mund im Bild zeigt, wo; die Figur sagt es an.
//   Die Uhr steht dabei gegenüber auf dem Tisch und hört zu (Mikrofon). Erst die Figur wählen, dann ein Spiel –
//   jeden Tag ist ein anderes das „Spiel des Tages“:
//   Monsterjagd  – die Zahnmonster verschwinden nur, solange die Uhr Putzen hört; Stille holt sie zurück
//   Hoch-Tief    – kommt der Schmetterling, hoch summen, kommt der Fisch, tief summen: der Frosch springt hinterher
//                  (die erste Runde merkt sich die eigene Stimmlage – Kinder und Erwachsene gleich)
//   Summ-Ballon  – Summen lässt den Ballon steigen, Stille sinken; Sterne einsammeln
//   Album        – Sticker, die es nach jedem Putzen gibt; 7 Tage am Stück: Pokal und Krone
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
enum Game : uint8_t { MONSTER, PITCH, BALLOON, GAMES };
const char* const GAME_NAME[GAMES] = {"Monsterjagd", "Hoch oder tief", "Summ-Ballon"};
const char* const GAME_CUE[GAMES] = {"c/monster", "c/pitch", "c/hum"};
const char* const GAME_REC[GAMES] = {"hm", "hp", "hb"};  // Rekord je Spiel (NVS)
const char* const GAME_UNIT[GAMES] = {"Monster", "Treffer", "Sterne"};

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
constexpr uint32_t OVERRUN_MS = 3000;  // eine Ansage darf einen Abschnitt so viel verlängern

Step step = PICK;
Hero hero = kidart::LUNA;
Game game = MONSTER;
int sec = 0, albumPage = 0, frame = 0, score = 0;
uint32_t secStart = 0, introStart = 0, lastFrameAt = 0, lastSend = 0, hintAt = 0;
bool teamSeen = false, hintTeam = false;
uint16_t *px = nullptr, *big = nullptr;
lv_obj_t *scrRef, *canvas, *topL, *botL, *panel = nullptr;
int awarded[3], nAwarded = 0, showAward = 0;

// ------------------------------------------------------------------ Speicher (Namensraum „brush“)
//   al Album (Bitmaske) · sd letzter Putztag · st Serie · sb beste Serie · hm/hp/hb Rekorde der Spiele

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

Game gameOfDay() { return (Game)(std::max(0, today()) % GAMES); }

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

// ------------------------------------------------------------------ Mikrofon: Summen, Tonhöhe, Putzgeräusch

struct Ear {
  int16_t win[640];
  int have = 0;
  float floor = 0.004f;
  bool voiced = false;  // ein Ton (Summen)
  bool sound = false;   // überhaupt etwas zu hören (Bürste, Summen …)
  float pitch = 0;      // Hz, wenn voiced
  uint32_t deafUntil = 0;
} ear;

// Neue Mikrofonwerte holen und die letzten 40 ms prüfen. Während eigener Ansagen und Töne hört die Uhr weg.
//   sound:  lauter als das Grundrauschen (das steigt nur sehr langsam mit, damit Dauerputzen nicht „verschwindet“)
//   voiced: nach einem Tiefpass (≈ 900 Hz) deutlich periodisch (Autokorrelation) – Summen ist ein Ton, das
//           Bürstengeräusch ist Rauschen. Am Rechner geprüft: Summen zählt auch unter dreifach lauter Bürste.
//   pitch:  70 … 530 Hz (Kinder summen um 250 Hz, Erwachsene um 110 Hz)
void listen() {
  int16_t tmp[512];
  size_t n;
  bool deaf = clips::busy() || millis() < ear.deafUntil;
  while ((n = audio::micRead(tmp, 512)) > 0) {
    if (deaf) { ear.have = 0; continue; }
    int keep = std::max(0, std::min(ear.have, 640 - (int)n));
    memmove(ear.win, ear.win + ear.have - keep, keep * 2);
    memcpy(ear.win + keep, tmp, n * 2);
    ear.have = keep + n;
  }
  ear.voiced = ear.sound = false;
  constexpr int N = 640, LAG_MIN = 30, LAG_MAX = 229;
  if (deaf || ear.have < N) return;
  static float x[N];
  const int16_t* w = ear.win + ear.have - N;
  float e = 0, s1 = 0, s2 = 0;
  for (int i = 0; i < N; i++) {
    float v = w[i] / 32768.0f;
    e += v * v;
    s1 += 0.3f * (v - s1);
    s2 += 0.3f * (s1 - s2);
    x[i] = s2;
  }
  float rms = sqrtf(e / N);
  ear.floor += (rms - ear.floor) * (rms < ear.floor ? 0.3f : 0.002f);  // fällt schnell, steigt sehr langsam
  if (rms < std::max(0.0015f, ear.floor * 2.0f)) return;
  ear.sound = true;
  static float r[LAG_MAX + 2];
  float best = 0;
  for (int lag = LAG_MIN; lag <= LAG_MAX + 1; lag++) {
    float s = 0, a = 0, b = 0;
    for (int i = 0; i + lag < N; i++) { s += x[i] * x[i + lag]; a += x[i] * x[i]; b += x[i + lag] * x[i + lag]; }
    r[lag] = s / (sqrtf(a * b) + 1e-9f);
    if (lag <= LAG_MAX) best = std::max(best, r[lag]);
  }
  if (best < 0.45f) return;
  // erster Gipfel nahe am besten (sonst eine Oktave zu tief), dann zwischen zwei Abständen verfeinern
  int lag = LAG_MIN + 1;
  while (lag < LAG_MAX && !(r[lag] >= best * 0.88f && r[lag] >= r[lag - 1] && r[lag] >= r[lag + 1])) lag++;
  float d = r[lag - 1] - 2 * r[lag] + r[lag + 1];
  float off = d < 0 ? 0.5f * (r[lag - 1] - r[lag + 1]) / d : 0;
  ear.voiced = true;
  ear.pitch = 16000.0f / (lag + off);
}

// ------------------------------------------------------------------ Spiel: Monsterjagd

constexpr int GERMS = 3;
int germs = GERMS;
uint32_t scrubMs = 0, quietMs = 0, lastHeard = 0;

void monsterSection() { germs = GERMS; scrubMs = quietMs = 0; }

void monsterTick(uint32_t dt) {
  listen();
  if (ear.sound) { lastHeard = millis(); scrubMs += dt; quietMs = 0; }
  else if (!clips::busy()) quietMs += dt;
  if (scrubMs >= 1800 && germs > 0) {  // knapp 2 s Putzen: ein Monster weniger
    scrubMs = 0;
    germs--;
    score++;
    audio::play(audio::FX_TAP);
  }
  if (germs == 0 && scrubMs >= 2500) {  // alle weg – in den langen Abschnitten kommt ein neues
    scrubMs = 0;
    germs = 1;
  }
  if (quietMs >= 3000 && germs < GERMS) {  // Stille: eins kommt zurück
    quietMs = 0;
    germs++;
  }
}

bool heardRecently() { return millis() - lastHeard < 600; }

// ------------------------------------------------------------------ Spiel: Hoch oder tief

enum PitchPhase { P_CALIB, P_PROMPT, P_LISTEN, P_RESULT };
PitchPhase pPhase = P_CALIB;
kidart::PitchTarget pTarget = kidart::T_NONE;
float pCenter = 0;  // eigene Stimmlage (log Hz)
int pFrog = 0, pResult = 0, pVoiced = 0, pRound = 0;
float pSum = 0;
uint32_t pAt = 0;

void pitchNext() {
  pTarget = (esp_random() % 2) ? kidart::T_HIGH : kidart::T_LOW;
  if (pRound >= 2 && pTarget == (pRound % 2 ? kidart::T_HIGH : kidart::T_LOW) && esp_random() % 3 == 0)
    pTarget = pTarget == kidart::T_HIGH ? kidart::T_LOW : kidart::T_HIGH;  // nicht zu oft dasselbe
  pRound++;
  pPhase = P_PROMPT;
  pFrog = pResult = pVoiced = 0;
  pSum = 0;
  pAt = millis();
  audio::play(pTarget == kidart::T_HIGH ? audio::FX_HIGH : audio::FX_LOW);
  ear.deafUntil = millis() + 650;  // den eigenen Ton nicht mithören
}

void pitchTick() {
  listen();
  if (clips::busy()) { pAt = millis(); return; }  // während einer Ansage ruht das Spiel
  uint32_t in = millis() - pAt;
  switch (pPhase) {
    case P_CALIB:  // „Summ erst einmal ganz normal“: eigene Stimmlage merken
      if (ear.voiced) { pSum += logf(ear.pitch); pVoiced++; }
      if (pVoiced >= 6) { pCenter = pSum / pVoiced; pitchNext(); }
      break;
    case P_PROMPT:
      if (in > 650) { pPhase = P_LISTEN; pAt = millis(); }
      break;
    case P_LISTEN: {
      if (ear.voiced) {
        float lp = logf(ear.pitch);
        if (!pCenter) pCenter = lp;  // ohne Eichung: der erste Ton ist die Mitte
        pSum += lp;
        pVoiced++;
      }
      if (pVoiced >= 3) {  // ≈ 0,5 s Summen reichen für eine Entscheidung
        float rel = pSum / pVoiced - pCenter;  // > 0 höher als sonst
        bool high = rel > 0.06f, low = rel < -0.06f;  // gut ein Halbton
        bool ok = pTarget == kidart::T_HIGH ? high : low;
        if (ok || high || low || pVoiced >= 8) {
          pResult = ok ? 1 : 2;
          pFrog = ok ? (pTarget == kidart::T_HIGH ? 1 : 2) : 0;
          if (ok) { score++; audio::play(audio::FX_OK); }
          pPhase = P_RESULT;
          pAt = millis();
        }
      } else if (in > 5000) {  // nichts gehört: noch einmal
        pitchNext();
      }
      break;
    }
    case P_RESULT:
      if (in > 1300) pitchNext();
      break;
  }
}

// ------------------------------------------------------------------ Spiel: Summ-Ballon

constexpr int B_TOP = 1, B_BOTTOM = 20, MAXOBJ = 6;  // Ballon oben … auf dem Hügel
kidart::BalloonObj objs[MAXOBJ];
int nObj = 0, by = B_BOTTOM, nextSpawn = 0;

void balloonTick() {
  listen();
  if (ear.voiced) {
    if (by > B_TOP) by--;  // Summen: steigen, egal wie hoch der Ton
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
void start(Game g);
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

void showMode() {  // das Spiel des Tages oben und groß, darunter die anderen und das Album
  step = MODE;
  kidart::drawBrushPick(px, 0, crowns());
  show();
  setTop(kidart::heroName(hero));
  setBottom("Das goldene ist heute dran.");
  clearPanel();
  auto cb = [](lv_event_t* e) { start((Game)(intptr_t)lv_event_get_user_data(e)); };
  Game g0 = gameOfDay();
  choice(GAME_NAME[g0], C_BRASS(), C_BG(), 0, 132, cb, g0);
  for (int k = 1; k < GAMES; k++) {
    Game g = (Game)((g0 + k) % GAMES);
    choice(GAME_NAME[g], C_SURF2(), C_TEXT(), 0, 132 + k * 62, cb, g);
  }
  choice("Album", lv_color_hex(0x8a6bbf), C_TEXT(), 0, 132 + GAMES * 62, [](lv_event_t*) {
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

void scoreLine() {
  char b[64];
  snprintf(b, sizeof(b), "%s · %d %s", SEC[sec].how, score, GAME_UNIT[game]);
  setBottom(b);
}

void enterSection(int i) {
  sec = i;
  secStart = millis();
  setTop(SEC[i].where);
  scoreLine();
  hintAt = millis();
  if (i) audio::play(audio::FX_TICK);
  say(SEC[i].cue);
  if (game == MONSTER) monsterSection();
}

void start(Game g) {
  game = g;
  step = INTRO;
  introStart = millis();
  teamSeen = hintTeam = false;
  score = 0;
  ear = Ear();
  dropPanel();
  play(1);
  audio::hold(true);  // Verstärker bleibt an: kein Plopp zwischen den Ansagen
  audio::micStart();
  say(GAME_CUE[g]);
  setTop(GAME_NAME[g]);
  if (g == MONSTER) {
    germs = GERMS;
    setBottom("Putz kräftig – dann verschwinden sie!");
  } else if (g == PITCH) {
    pPhase = P_CALIB;
    pTarget = kidart::T_NONE;
    pCenter = pSum = 0;
    pVoiced = pRound = pFrog = pResult = 0;
    setBottom("Summ erst einmal ganz normal");
  } else {
    nObj = 0;
    by = B_BOTTOM;
    nextSpawn = 4;
    setBottom("Summen = steigen, Stille = sinken");
  }
}

int pickSticker(uint32_t have) {  // einer, der noch fehlt; sind alle da, irgendeiner
  int miss[24], n = 0;
  for (int i = 0; i < 24; i++) if (!(have >> i & 1)) miss[n++] = i;
  return n ? miss[esp_random() % n] : (int)(esp_random() % 24);
}

void finish() {
  step = DONE;
  audio::micStop();
  radio::brushSend(hero, game, SECTIONS, std::min(score, 255), true);
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
  bool record = score > (int)getU(GAME_REC[game]);
  if (record) putU(GAME_REC[game], score);
  int stars = routines::checkMatching("ähne") + routines::checkMatching("Zahn");  // „Zähne putzen“ in der Routine

  say("c/spit");
  if (kids::cfg().brushParent) say("c/parent", false);
  say(trophy ? "c/streak" : "c/sticker", false);
  audio::play(audio::FX_CHIME);

  kidart::drawSticker(px, awarded[0], 0);
  show();
  setTop("Super geputzt!");
  char b[128], extra[56] = "";
  if (score > 0) snprintf(extra, sizeof(extra), record ? " · Rekord: %d %s!" : " · %d %s", score, GAME_UNIT[game]);
  else if (stars > 0) snprintf(extra, sizeof(extra), " · +%d Sterne", stars);
  snprintf(b, sizeof(b), "Neuer Sticker: %s%s", kidart::stickerName(awarded[0]), extra);
  setBottom(b);
  clearPanel();
  choice("Fertig", C_BRASS(), C_BG(), 0, 300, [](lv_event_t*) { back(); });
}

void teamHint() {  // unten abwechselnd: wie putzen und Punkte / was die andere Uhr macht
  radio::BrushMate m;
  bool mate = radio::brushMate(&m);
  if (mate && !teamSeen) say("c/team", false);  // „Deine Schwester putzt auch gerade …“
  if (mate) teamSeen = true;
  if (millis() - hintAt < 3500) return;
  hintAt = millis();
  hintTeam = mate && !hintTeam;
  if (!hintTeam) { scoreLine(); return; }
  char b[80];
  if (m.done) snprintf(b, sizeof(b), "%s ist schon fertig!", m.name);
  else if (m.section < SECTIONS) snprintf(b, sizeof(b), "%s putzt mit: %s", m.name, SEC[m.section].where);
  else snprintf(b, sizeof(b), "%s putzt mit!", m.name);
  setBottom(b);
}

void drawGame(int s) {
  const Section& c = SEC[s];
  if (game == MONSTER) kidart::drawBrushGame(px, hero, c.view, c.area, germs, heardRecently(), frame);
  else if (game == PITCH) kidart::drawPitchGame(px, hero, pTarget, pFrog, pResult, ear.voiced, c.view, c.area, frame);
  else kidart::drawBalloon(px, hero, by, objs, nObj, c.view, c.area, ear.voiced, frame);
}

void tickGame(uint32_t dt) {
  if (game == MONSTER) monsterTick(dt);
  else if (game == PITCH) pitchTick();
  else balloonTick();
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
  uint32_t tick = step == RUN || step == INTRO ? 150 : 350;
  if (now - lastFrameAt < tick) return;
  uint32_t dt = std::min<uint32_t>(now - lastFrameAt, 500);
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
    case INTRO: {
      // Erklärung ausreden lassen; bei Hoch-Tief danach eichen (höchstens 10 s), dann läuft die Zeit
      bool talking = clips::busy() && now - introStart < 15000;
      if (!talking) tickGame(dt);
      bool ready = !talking && now - introStart > 1200 && (game != PITCH || pPhase != P_CALIB || now - introStart > 25000);
      if (!ready) {
        drawGame(0);
        show();
        return;
      }
      if (game == PITCH && pPhase == P_CALIB) pitchNext();  // keine Eichung gehört: der erste Ton wird die Mitte
      step = RUN;
      enterSection(0);
      break;
    }
    case RUN:
      break;
  }

  uint32_t in = now - secStart, len = SEC[sec].sec * 1000u;
  if (in >= len && (!clips::busy() || in >= len + OVERRUN_MS)) {  // nächster Abschnitt – die Ansage darf ausreden
    if (sec + 1 >= SECTIONS) { finish(); return; }
    clips::stop();
    enterSection(sec + 1);
  }
  if (now - lastSend > 2000) {
    lastSend = now;
    radio::brushSend(hero, game, sec, std::min(score, 255), false);
  }
  tickGame(dt);
  teamHint();
  drawGame(sec);
  show();
}

void leave() {
  audio::hold(false);
  audio::micStop();
  if (step != DONE) clips::stop();  // „Ausspucken …“ darf nach „Fertig“ noch ausreden
  panel = nullptr;
  step = PICK;
  if (big) { heap_caps_free(big); big = nullptr; }
  if (px) { heap_caps_free(px); px = nullptr; }
}

}  // namespace

BrushStats brushStats() {
  BrushStats s{};
  s.streak = currentStreak();
  s.best = getU("sb");
  s.monster = getU("hm");
  s.pitch = getU("hp");
  s.balloon = getU("hb");
  s.have = getU("al");
  return s;
}

const Page PAGE_BRUSH = {build, update, leave, nullptr, false};
