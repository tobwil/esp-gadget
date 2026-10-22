#include "power.h"
#include <Preferences.h>
#include <WiFi.h>
#include "assist.h"
#include "audio.h"
#include "config.h"
#include "hw.h"
#include "kids.h"
#include "music.h"
#include "settings.h"
#include "ui/ui.h"

namespace pwr {

static Mode m = M_AUTO;
static bool active = false;

// Verlauf: alle 2 Minuten ein Wert, 6 Stunden
static const int HIST = 180;
static const uint32_t STEP_MS = 120000;
struct Sample { uint8_t pct; uint16_t mv; uint8_t chg; };
static Sample* hist = nullptr;
static int hN = 0, hHead = 0;
static uint32_t lastSample = 0;

void begin() {
  Preferences p;
  p.begin("power", true);
  m = (Mode)p.getUChar("mode", M_AUTO);
  p.end();
  hist = (Sample*)heap_caps_calloc(HIST, sizeof(Sample), MALLOC_CAP_SPIRAM);
}

Mode mode() { return m; }
void setMode(Mode n) {
  m = n;
  Preferences p;
  p.begin("power", false);
  p.putUChar("mode", m);
  p.end();
}
bool saving() { return active; }

uint16_t screenOffS() {
  if (!active) return cfg.screenOff;
  return cfg.screenOff && cfg.screenOff < 10 ? cfg.screenOff : 10;
}
uint16_t deepSleepMin() { return active ? 3 : DEEP_SLEEP_MIN; }

static const Sample& at(int back) { return hist[(hHead - 1 - back + HIST) % HIST]; }

float drainPerHour() {
  // jüngster zusammenhängender Abschnitt ohne Laden, mindestens 20 Minuten
  if (!hist || hN < 2) return 0;
  int k = 0;
  while (k + 1 < hN && !at(k + 1).chg && !at(0).chg) k++;
  if (k * STEP_MS < 20 * 60000UL) return 0;
  float d = at(k).pct - at(0).pct;
  return d > 0 ? d * 60000.0f * 60 / (k * STEP_MS) : 0;
}

int remainingMin() {
  int pct = hwBattery();
  float d = drainPerHour();
  if (pct < 0 || hwUsb() || d <= 0) return -1;
  return (int)(pct / d * 60);
}

String historyJson() {
  String s = "[";
  for (int i = hN - 1; i >= 0; i--) {
    const Sample& x = at(i);
    s += "[" + String(i * STEP_MS / 60000) + "," + x.pct + "," + x.mv + "," + x.chg + "]";
    if (i) s += ",";
  }
  return s + "]";
}

void poll() {
  static uint32_t at1s = 0;
  uint32_t now = millis();
  if (now - at1s < 1000) return;
  at1s = now;
  int pct = hwBattery();
  bool usb = hwUsb();

  // Verlauf
  if (hist && pct >= 0 && (!lastSample || now - lastSample >= STEP_MS)) {
    lastSample = now;
    hist[hHead] = {(uint8_t)pct, (uint16_t)hwBatteryMv(), (uint8_t)(hwCharging() || usb)};
    hHead = (hHead + 1) % HIST;
    if (hN < HIST) hN++;
  }

  // Sparmodus an/aus (Auto: ab 20 %, aus über 25 % oder am Kabel)
  bool want = m == M_ON || (m == M_AUTO && pct >= 0 && !usb && (pct <= 20 || (active && pct <= 25)));
  if (want != active) {
    active = want;
    Serial.printf("Akku: Sparmodus %s\n", active ? "an" : "aus");
    if (active && m == M_AUTO) ui::toast("Akku 20 % – Sparmodus an", 3000);
  }

  // Warnungen und Notabschaltung
  static int warned = 100;
  if (pct >= 0 && !usb) {
    if (pct <= 10 && warned > 10) { warned = 10; ui::toast("Akku 10 % – bitte bald laden", 4000); audio::play(audio::FX_ERROR); }
    if (pct <= 3) {
      static uint32_t lowSince = 0;
      if (!lowSince) lowSince = now;
      if (now - lowSince > 20000) {  // 20 s lang wirklich leer: geordnet ausschalten
        Serial.println("Akku: leer, schalte aus");
        settingsSave();
        audio::shutdown();
        hwPowerOff();
      }
    }
  } else if (usb) {
    warned = 100;
  }

  // Helligkeit: Obergrenze nachts (Kindermodus) und im Sparmodus
  hwBrightnessCap(kids::night() ? 20 : active ? 40 : 100);

  // Ohne laufende Musik und ohne Assistentin: WLAN schlafen lassen, Prozessor langsamer
  bool idle = active && !hwScreenOn() && !music::state().active && assist::phase() == assist::OFF;
  static bool slow = false;
  if (idle != slow) {
    slow = idle;
    WiFi.setSleep(idle);
    setCpuFrequencyMhz(idle ? 80 : 240);
  }
}

}  // namespace pwr
