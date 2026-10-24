// Tests am Rechner: Hören (Summen, Tonhöhe, Bürstengeräusch) und Zeichnen (Kinder-Zifferblatt, Zähneputzen).
//   sh tests/run_host.sh
// Gleicher Code wie auf der Uhr (firmware/src/hearing.cpp, kidart.cpp), nur für den Rechner übersetzt.
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <set>
#include <string>
#include <vector>
#include "hearing.h"
#include "kidart.h"

static int checks = 0, failures = 0;
#define CHECK(cond, ...)                                    \
  do {                                                      \
    checks++;                                               \
    if (!(cond)) {                                          \
      failures++;                                           \
      printf("  FEHLER %s:%d: ", __FILE__, __LINE__);       \
      printf(__VA_ARGS__);                                  \
      printf("\n");                                         \
    }                                                       \
  } while (0)

// ------------------------------------------------------------------ Signale

static uint32_t rng = 1;
static float noise() {  // gleichverteilt −1 … 1
  rng = rng * 1664525u + 1013904223u;
  return (rng >> 8) / 8388608.0f - 1.0f;
}

struct Sig {
  std::vector<float> v = std::vector<float>(hearing::N, 0.0f);
  Sig& hum(float f, float amp, float phase = 0) {  // Summen: Grundton und Obertöne
    for (int i = 0; i < hearing::N; i++) {
      float t = i / 16000.0f;
      v[i] += amp * (sinf(2 * M_PI * f * t + phase) + 0.5f * sinf(4 * M_PI * f * t + 1 + phase) + 0.3f * sinf(6 * M_PI * f * t + 2));
    }
    return *this;
  }
  Sig& brush(float rms) {  // Bürste: helles, breitbandiges Rauschen (Differenz von weißem Rauschen)
    float prev = noise(), sum = 0;
    std::vector<float> n(hearing::N);
    for (int i = 0; i < hearing::N; i++) { float x = noise(); n[i] = x - prev; prev = x; sum += n[i] * n[i]; }
    float k = rms / sqrtf(sum / hearing::N);
    for (int i = 0; i < hearing::N; i++) v[i] += n[i] * k;
    return *this;
  }
  Sig& room(float rms) {  // leises Raumrauschen
    for (int i = 0; i < hearing::N; i++) v[i] += noise() * rms * 1.73f;
    return *this;
  }
  std::vector<int16_t> pcm() const {
    std::vector<int16_t> o(hearing::N);
    for (int i = 0; i < hearing::N; i++) o[i] = (int16_t)fmaxf(-32767, fminf(32767, v[i] * 32767));
    return o;
  }
};

static float rmsOf(const Sig& s) {
  double e = 0;
  for (float x : s.v) e += x * x;
  return sqrtf(e / s.v.size());
}

// Die Uhr hat schon eine Weile in einem ruhigen Raum zugehört
static float quietFloor() {
  float floor = 0.004f;
  for (int k = 0; k < 40; k++) hearing::analyze(Sig().room(0.0008f).pcm().data(), floor);
  return floor;
}

static void testHearing() {
  printf("Hören\n");
  {
    float floor = quietFloor();
    auto r = hearing::analyze(Sig().room(0.0008f).pcm().data(), floor);
    CHECK(!r.sound && !r.voiced, "ruhiger Raum darf nichts auslösen (rms %.4f)", r.rms);
  }
  // Summen in allen Lagen: Kinder (≈ 200–450 Hz) und Erwachsene (≈ 80–160 Hz)
  for (float f : {80.f, 95.f, 110.f, 130.f, 160.f, 200.f, 250.f, 300.f, 350.f, 450.f, 520.f}) {
    float floor = quietFloor();
    auto r = hearing::analyze(Sig().hum(f, 0.03f).room(0.0008f).pcm().data(), floor);
    CHECK(r.sound && r.voiced, "Summen mit %.0f Hz nicht erkannt", f);
    CHECK(fabsf(r.pitch - f) / f < 0.03f, "Tonhöhe %.0f Hz statt %.0f Hz", r.pitch, f);
  }
  // Summen unter der Zahnbürste
  for (float f : {110.f, 250.f, 350.f})
    for (float k : {1.f, 2.f, 3.f}) {
      float floor = quietFloor();
      Sig h = Sig().hum(f, 0.03f);
      auto r = hearing::analyze(Sig(h).brush(rmsOf(h) * k).pcm().data(), floor);
      CHECK(r.voiced, "Summen %.0f Hz unter %.0f-fach lauter Bürste nicht erkannt", f, k);
      if (r.voiced) CHECK(fabsf(r.pitch - f) / f < 0.05f, "unter Bürste: %.0f Hz statt %.0f Hz", r.pitch, f);
    }
  // nur Bürste: hörbar, aber kein Ton – in vielen Versuchen
  {
    int voiced = 0, heard = 0, n = 300;
    for (int k = 0; k < n; k++) {
      float floor = quietFloor();
      auto r = hearing::analyze(Sig().brush(0.01f + 0.0003f * k).pcm().data(), floor);
      voiced += r.voiced;
      heard += r.sound;
    }
    CHECK(heard == n, "Bürste nur %d von %d mal gehört", heard, n);
    CHECK(voiced <= n / 50, "Bürste %d von %d mal für Summen gehalten", voiced, n);
  }
  // zwei Minuten Dauerputzen (≈ 7 Prüfungen je Sekunde): das Grundrauschen darf die Bürste nicht „schlucken“
  {
    float floor = quietFloor();
    hearing::Result r;
    for (int k = 0; k < 7 * 120; k++) r = hearing::analyze(Sig().brush(0.012f).room(0.0008f).pcm().data(), floor);
    CHECK(r.sound, "nach 2 min Putzen nichts mehr gehört (Grundrauschen %.4f)", floor);
  }
  // nach dem Putzen wieder Stille: sofort nichts mehr
  {
    float floor = quietFloor();
    for (int k = 0; k < 50; k++) hearing::analyze(Sig().brush(0.02f).pcm().data(), floor);
    auto r = hearing::analyze(Sig().room(0.0008f).pcm().data(), floor);
    CHECK(!r.sound, "Stille nach dem Putzen noch als Geräusch gehört");
  }
  // leises Summen in leisem Raum
  {
    float floor = quietFloor();
    auto r = hearing::analyze(Sig().hum(260, 0.006f).room(0.0008f).pcm().data(), floor);
    CHECK(r.voiced, "leises Summen nicht erkannt (rms %.4f)", r.rms);
  }
  // Höher/tiefer als die eigene Lage (gut ein Halbton = 6 %) – hearing liefert die Tonhöhe weiterhin
  {
    float floor = quietFloor();
    auto a = hearing::analyze(Sig().hum(200, 0.03f).pcm().data(), floor);
    auto b = hearing::analyze(Sig().hum(240, 0.03f).pcm().data(), floor);
    auto c = hearing::analyze(Sig().hum(165, 0.03f).pcm().data(), floor);
    CHECK(logf(b.pitch) - logf(a.pitch) > 0.06f, "240 Hz nicht als höher als 200 Hz erkannt");
    CHECK(logf(c.pitch) - logf(a.pitch) < -0.06f, "165 Hz nicht als tiefer als 200 Hz erkannt");
  }
}

// ------------------------------------------------------------------ Zeichnen

using kidart::S;
constexpr int GUARD = 256;
static std::vector<uint16_t> canvas(S * S + 2 * GUARD);
static uint16_t* px() { return canvas.data() + GUARD; }

static void arm() {
  for (int i = 0; i < GUARD; i++) canvas[i] = canvas[GUARD + S * S + i] = 0xBEEF;
}
static bool guardsOk() {
  for (int i = 0; i < GUARD; i++)
    if (canvas[i] != 0xBEEF || canvas[GUARD + S * S + i] != 0xBEEF) return false;
  return true;
}
static std::string hash() {  // FNV-1a über das ganze Raster
  uint64_t h = 1469598103934665603ull;
  for (int i = 0; i < S * S; i++) { h ^= px()[i]; h *= 1099511628211ull; }
  return std::to_string(h);
}
static int colored() {  // Pixel in der Szene, die nicht schwarz sind
  int n = 0;
  for (int i = 0; i < S * 96; i++) n += px()[i] != 0;
  return n;
}

#define DRAW(desc, call)                                                         \
  do {                                                                           \
    arm();                                                                       \
    call;                                                                        \
    CHECK(guardsOk(), "%s schreibt außerhalb des Rasters", desc);                 \
    CHECK(colored() > S * 40, "%s: Szene fast leer (%d Pixel)", desc, colored()); \
  } while (0)

static void testDrawing() {
  printf("Zeichnen\n");
  using namespace kidart;
  const MouthView views[3] = {M_CHEW, M_OUTER, M_INNER};
  const int areas[3] = {4, 3, 2};
  char d[96];
  // Kinder-Zifferblatt: alle Welten, Tiere, Tageszeiten
  for (int w = 0; w < WORLDS; w++)
    for (int p = 0; p < PETS; p++)
      for (int a = 0; a < kids::ACTS; a++) {
        Scene s{(kids::Act)a, a % 2 == 0, (World)w, (Pet)p, (uint8_t)(1 + a % 3), a % 3 == 0, a == 4, a % 7, 0.5f, a};
        snprintf(d, sizeof(d), "Zifferblatt Welt %d Tier %d Zeit %d", w, p, a);
        DRAW(d, draw(px(), s));
      }
  for (int h = 0; h < 2; h++)
    for (int f = 0; f < 8; f++) {
      DRAW("Auswahl", drawBrushPick(px(), f, f % 4));
      for (int v = 0; v < 3; v++)
        for (int a = 0; a < areas[v]; a++) {
          snprintf(d, sizeof(d), "Monsterjagd %d/%d", v, a);
          for (int g = 0; g <= 3; g++) DRAW(d, drawBrushGame(px(), (Hero)h, views[v], a, g, f % 2, f));
          GameObj o[3] = {{-5, 3, 0}, {30, 20, 1}, {70, 50, 0}};  // auch außerhalb
          DRAW("Zahnpasta-Kugel", drawTiltBall(px(), (Hero)h, views[v], a, -20.0f + f * 12, 60.0f - f * 9, f, f % 2, f));
          DRAW("Sterne fangen", drawCatch(px(), (Hero)h, -10.0f + f * 10, o, 3, views[v], a, f));
          DRAW("Wiesenlauf", drawMeadowRun(px(), (Hero)h, -5.0f + f * 6, o, 3, f % 2, views[v], a, f));
        }
    }
  // dasselbe zweimal gezeichnet = dasselbe Bild
  drawBrushGame(px(), LUNA, M_OUTER, 1, 2, true, 5);
  std::string h1 = hash();
  drawBrushGame(px(), LUNA, M_OUTER, 1, 2, true, 5);
  CHECK(hash() == h1, "Zeichnen ist nicht wiederholbar");
  // Monster: 0 … 3 sind vier verschiedene Bilder
  std::set<std::string> germs;
  for (int g = 0; g <= 3; g++) { drawBrushGame(px(), FIPS, M_CHEW, 2, g, true, 1); germs.insert(hash()); }
  CHECK(germs.size() == 4, "Monster-Anzahl ist im Bild nicht zu unterscheiden");
  // Sticker: alle verschieden, alle mit Namen
  std::set<std::string> st;
  for (int i = 0; i < STICKERS; i++) {
    DRAW("Sticker", drawSticker(px(), i, 0));
    st.insert(hash());
    CHECK(stickerName(i)[0], "Sticker %d ohne Namen", i);
  }
  CHECK((int)st.size() == STICKERS, "nur %d von %d Stickern unterscheiden sich", (int)st.size(), STICKERS);
  CHECK(!stickerName(-1)[0] && !stickerName(STICKERS)[0], "Name für ungültigen Sticker");
  for (int pg = 0; pg < 3; pg++) {
    drawAlbum(px(), 0, pg, 0);
    std::string empty = hash();
    DRAW("Album", drawAlbum(px(), (1u << STICKERS) - 1, pg, 0));
    CHECK(hash() != empty, "Album Seite %d: voll sieht aus wie leer", pg);
  }
  // Ziele der Zahnpasta-Kugel: erreichbar, sichtbar (im runden Bild) und je Abschnitt verschieden
  for (int v = 0; v < 3; v++)
    for (int a = 0; a < areas[v]; a++) {
      std::set<std::pair<int, int>> spots;
      for (int t = 0; t < 3; t++) {
        int x, y;
        tiltTarget(views[v], a, t, x, y);
        spots.insert({x, y});
        // die Kugel bleibt in einem Kreis (Mitte 29/29, Radius 23 Blöcke) und oberhalb von y 45 – siehe ui_brush.cpp
        float dx = x - 29.0f, dy = y - 29.0f;
        CHECK(dx * dx + dy * dy <= 23.0f * 23.0f && y <= 44, "Ziel %d/%d/%d bei (%d, %d) für die Kugel nicht erreichbar", v, a, t, x, y);
      }
      CHECK(spots.size() == 3, "Abschnitt %d/%d: Ziele liegen aufeinander", v, a);
    }
  // tiltTarget darf das gerade gezeichnete Bild nicht verändern
  drawCatch(px(), LUNA, 20, nullptr, 0, M_CHEW, 0, 0);
  std::string before = hash();
  int x, y;
  tiltTarget(M_INNER, 1, 2, x, y);
  CHECK(hash() == before, "tiltTarget verändert das Bild");
}

int main() {
  testHearing();
  testDrawing();
  printf("%d Prüfungen, %d Fehler\n", checks, failures);
  return failures ? 1 : 0;
}
