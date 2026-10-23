#include "power.h"
#include <Preferences.h>
#include <WiFi.h>
#include "assist.h"
#include "audio.h"
#include "config.h"
#include "hw.h"
#include "kids.h"
#include "music.h"
#include "net.h"
#include "phone.h"
#include "radio.h"
#include "voice.h"
#include "settings.h"
#include "ui/ui.h"

namespace pwr {

static Mode m = M_AUTO;
static bool active = false;

// Verlauf: alle 2 Minuten ein Wert, 6 Stunden. Mit Uhrzeit (Unix-Zeit), damit er Neustarts und Tiefschlaf
// übersteht; gesichert wird alle 10 Minuten und vor Tiefschlaf/Ausschalten (schont den Flash).
static const int HIST = 180;
static const uint32_t STEP_S = 120;
struct Sample { uint32_t t; uint8_t pct; uint8_t chg; uint16_t mv; };
static Sample* hist = nullptr;
static int hN = 0, hHead = 0, unsaved = 0;

void save();

void begin() {
  Preferences p;
  p.begin("power", true);
  m = (Mode)p.getUChar("mode", M_AUTO);
  hist = (Sample*)heap_caps_calloc(HIST, sizeof(Sample), MALLOC_CAP_SPIRAM);
  if (hist && p.getBytesLength("hist") == HIST * sizeof(Sample)) {
    p.getBytes("hist", hist, HIST * sizeof(Sample));
    hN = min<int>(p.getUShort("hN", 0), HIST);
    hHead = p.getUShort("hHead", 0) % HIST;
  }
  p.end();
  esp_register_shutdown_handler([]() { save(); });  // auch vor jedem Neustart (Update, Einstellungen)
}

void save() {
  if (!hist || !unsaved) return;
  Preferences p;
  p.begin("power", false);
  p.putBytes("hist", hist, HIST * sizeof(Sample));
  p.putUShort("hN", hN);
  p.putUShort("hHead", hHead);
  p.end();
  unsaved = 0;
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
  // jüngster zusammenhängender Abschnitt ohne Laden, mindestens 20 Minuten; eine Lücke über 3 Stunden
  // (Uhr war aus) beendet ihn – Tiefschlaf zählt mit, er gehört zum normalen Tag
  if (!hist || hN < 2 || at(0).chg) return 0;
  int k = 0;
  while (k + 1 < hN && !at(k + 1).chg && at(k).t - at(k + 1).t < 3 * 3600) k++;
  uint32_t span = at(0).t - at(k).t;
  if (span < 20 * 60) return 0;
  float d = at(k).pct - at(0).pct;
  return d > 0 ? d * 3600.0f / span : 0;
}

int remainingMin() {
  int pct = hwBattery();
  float d = drainPerHour();
  if (pct < 0 || hwUsb() || d <= 0) return -1;
  return (int)(pct / d * 60);
}

static const char* sleepWhy = "";
static long sleepLeft = -1;
void sleepInfo(const char* why, long leftS) { sleepWhy = why ? why : ""; sleepLeft = leftS; }
String sleepText() {
  if (sleepWhy[0]) return String("wach: ") + sleepWhy;
  return "Tiefschlaf in " + String(max(1L, (sleepLeft + 59) / 60)) + " min";
}

String remainingText() {
  if (hwBattery() < 0 || hwUsb()) return "";
  int m = remainingMin();
  if (m < 0) return "Restzeit wird gemessen";
  if (m < 60) return "noch ca. " + String(max(m, 1)) + " min";
  m = (m + 5) / 10 * 10;  // auf 10 Minuten – genauer ist die Schätzung nicht
  return "noch ca. " + String(m / 60) + " h" + (m % 60 ? " " + String(m % 60) + " min" : "");
}

String historyJson() {
  uint32_t now = time(nullptr);
  String s = "[";
  for (int i = hN - 1; i >= 0; i--) {
    const Sample& x = at(i);
    s += "[" + String((now - x.t) / 60) + "," + x.pct + "," + x.mv + "," + x.chg + "]";
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

  // Verlauf (erst, wenn die Uhrzeit stimmt)
  uint32_t t = time(nullptr);
  if (hist && pct >= 0 && timeValid() && (!hN || t - at(0).t >= STEP_S || t < at(0).t)) {
    hist[hHead] = {t, (uint8_t)pct, (uint8_t)(hwCharging() || usb), (uint16_t)hwBatteryMv()};
    hHead = (hHead + 1) % HIST;
    if (hN < HIST) hN++;
    if (++unsaved >= 5) save();
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
        save();
        audio::shutdown();
        hwPowerOff();
      }
    }
  } else if (usb) {
    warned = 100;
  }

  // Helligkeit: Obergrenze nachts (Kindermodus) und im Sparmodus
  hwBrightnessCap(kids::night() ? 20 : active ? 40 : 100);

  // Strom sparen, sobald nichts gestreamt wird: das WLAN-Funkmodul hört dann nur zu den Beacons des Routers
  // hin (Modem-Schlaf; Funk mit anderen Uhren läuft im WLAN per Multicast weiter), bei dunklem Display
  // läuft der Prozessor mit 80 statt 240 MHz. Ohne Router (nur ESP-NOW) bleibt das Funkmodul wach.
  bool streaming = music::state().playing || assist::phase() != assist::OFF || voice::phase() == voice::LISTENING ||
                   voice::phase() == voice::THINKING || phone::connected() || radio::transmitting() ||
                   radio::receiving() || radio::screenOpen;
  bool modemSleep = netOnline() && !streaming;
  static int ps = -1;
  if (modemSleep != ps) { ps = modemSleep; WiFi.setSleep(modemSleep); }
  bool slowCpu = !hwScreenOn() && !streaming;
  static int cpu = -1;
  if (slowCpu != cpu) { cpu = slowCpu; setCpuFrequencyMhz(slowCpu ? 80 : 240); }
}

}  // namespace pwr
