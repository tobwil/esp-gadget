// Zeichen-Kontext für die Zifferblätter aus FaceLab (Garmin-Projekt).
// Die Designs rechnen in Garmin-Koordinaten (454 px, Mitte 227); Dc rechnet sie auf die Displaygröße um und
// zeichnet über LVGL (kantengeglättet). Dazu die Hilfen aus FaceLab: Farben mischen, Easing,
// Zufall, Monoline-Strichschrift und Sonne/Mond.
#pragma once
#include "pins.h"
#include <lvgl.h>
#include <math.h>
#include <stdint.h>

namespace fc {

static const float K = LCD_W / 454.0f;
inline float X(float x) { return LCD_W / 2.0f + (x - 227.0f) * K; }

struct Dc {
  lv_layer_t* L;
  uint32_t col = 0xffffff;
  float pen = 1;
  lv_area_t clipSave;
  bool clipped = false;

  void setColor(uint32_t c) { col = c; }
  void setPenWidth(float w) { pen = w; }
  void drawLine(float x1, float y1, float x2, float y2, bool round = false);
  void fillCircle(float x, float y, float r);
  void drawCircle(float x, float y, float r);            // Ring mit Stiftbreite
  void arc(float cx, float cy, float r, float a0, float a1, bool round = false);  // Uhr-Grad, im Uhrzeigersinn
  void fillRect(float x, float y, float w, float h);
  void fillTri(float x1, float y1, float x2, float y2, float x3, float y3);
  void fillPoly(const float* xy, int n);                 // konvex (Fächer ab Punkt 0)
  void fillStrip(const float* a, const float* b, int n); // Fläche zwischen zwei Punktreihen (Mondsichel)
  void fillEllipse(float x, float y, float rx, float ry);
  // Text in Inter: y = Oberkante der Großbuchstaben (wie FaceLabs Pixelschrift), just 0/1/2 = links/Mitte/rechts
  float text(const char* s, float x, float y, const lv_font_t* f, int just);
  float textW(const char* s, const lv_font_t* f);
  void clip(float x, float y, float w, float h);
  void noClip();
};

// ---- Farben und Mathe (FaceLab „G“)
uint32_t mix(uint32_t a, uint32_t b, float t);
inline uint32_t dim(uint32_t c, float f) { return mix(0, c, f); }
float clamp(float v, float lo, float hi);
float ease(float t);
float easeInOut(float t);
float spring(float t);
float stag(float p, int i, float step, float dur);
inline float ptX(float cx, float r, float deg) { return cx + r * sinf(deg * 0.01745329f); }
inline float ptY(float cy, float r, float deg) { return cy - r * cosf(deg * 0.01745329f); }
uint32_t rnd(uint32_t seed);
inline float rf(uint32_t s) { return s / 2147483647.0f; }
float noise(float x);
void tick(Dc& dc, float cx, float cy, float r1, float r2, float deg);

// ---- Monoline-Strichschrift (FaceLab „F.st“): y = Mitte, h = Versalhöhe, prog < 1 schreibt teilweise
float stW(const char* utf8, float h);
float st(Dc& dc, const char* utf8, float x, float y, float h, float pen, int just, float prog, float slant = 0);

// ---- Sonne und Mond (NOAA-Näherung, FaceLab „AST“); Minuten des Tages in Ortszeit, tz = UTC-Abstand in Minuten
float sunElev(int doy, float min, float lat, float lon, int tz);
float sunCross(int doy, float lat, float lon, int tz, float el, bool rising);  // −1 = nie
float sunNoon(int doy, float lon, int tz);
float moonPhase(double unix);            // 0 = Neumond, 0,5 = Vollmond
inline float illum(float ph) { return (1 - cosf(ph * 6.2831853f)) / 2; }

// ---- Daten, die ein Zifferblatt bekommt
struct Slot { char lbl[16]; char val[16]; };
struct Data {
  int h, m, s, ms, mins;            // Uhrzeit, mins = Minute des Tages
  int day, mon, dow, doy, year, wk; // Datum, wk = Kalenderwoche
  const char* dowStr;               // „FR“
  const char* monStr;               // „OKT“
  bool timeOk;
  uint32_t unix;
  float lat, lon;
  bool locOk;
  int tz;                           // Minuten
  float moon;                       // Mondphase
  int batt;                         // %, −1 = kein Akku
  bool charging;
  bool weatherOk;
  float temp;
  int wcode, rain;
  int peers;                        // Funk-Nachbarn
  float level;                      // Musikpegel 0..1
  bool music;
  uint32_t accent, dcol;
  Slot slots[3];
  int wakeN;                        // zählt jedes Aufwachen
};
struct Anim {
  float intro;  // 0 … 1 nach dem Aufwachen
  bool live;
  uint32_t t;   // ms seit dem Aufwachen
  int ms;
};

struct Face {
  const char* name;
  const char* blurb;
  int fps;      // Bilder pro Sekunde nach dem Intro
  void (*draw)(Dc& dc, const Data& d, const Anim& a);
  bool cache;   // nach dem Intro nur einmal pro Minute zeichnen (aufwendige Designs)
};

// Akkuanzeige „▮ 76 %“ (Blitz beim Laden, rot unter 15 %), mittig bei y
void battery(Dc& dc, const Data& d, float y, uint32_t col);
extern const Face FACE_SUN, FACE_STERNE, FACE_KTEXT, FACE_GLITCH;

}  // namespace fc
