// Taschenfunk – YT-Music-Player, Taschenuhr, Timer und Walkie-Talkie
// für das Waveshare ESP32-S3-Touch-AMOLED-1.75C.
//
// Bedienung:
//   Uhr: antippen oder nach oben wischen = Apps · nach links wischen = Musik
//   überall: nach rechts wischen = zurück
//   BOOT kurz = zurück (auf der Uhr: Musik Pause/Weiter) · BOOT halten = Funk an alle (Push-to-Talk)
//   PWR kurz = Display an/aus · PWR lang = Ausschalten/Neustart
#include <Arduino.h>
#include "audio.h"
#include "clockwork.h"
#include "config.h"
#include "hw.h"
#include "music.h"
#include "net.h"
#include "radio.h"
#include "settings.h"
#include "weather.h"
#include "ui/ui.h"

// Serielle Befehle für Tests über USB (zusätzlich zu tap/swipe/shot in hw.cpp)
static bool perfLog = false;

bool appCommand(const char* line) {
  char a[100], b[100];
  if (sscanf(line, "wifi %99s %99[^\n]", a, b) >= 1) {
    if (sscanf(line, "wifi %99s %99[^\n]", a, b) == 1) b[0] = 0;
    knownAdd(a, b);
    settingsSave();
    netConnect(a, b);
    Serial.printf("WLAN %s gespeichert\n", a);
  } else if (sscanf(line, "bridge %99s", a) == 1) {
    snprintf(cfg.bridge, sizeof(cfg.bridge), "%s", strcmp(a, "auto") ? a : "");
    settingsSave();
    music::bridgeChanged();
  } else if (sscanf(line, "browse %99[^\n]", a) == 1) {
    browseOpen(a);
  } else if (!strcmp(line, "toggle")) music::toggle();
  else if (!strcmp(line, "next")) music::next();
  else if (sscanf(line, "vol %99s", a) == 1) { cfg.volume = atoi(a); audio::setVolume(cfg.volume); }
  else if (sscanf(line, "fx %99s", a) == 1) audio::play((audio::Fx)atoi(a));
  else if (!strcmp(line, "home")) ui::home();
  else if (sscanf(line, "page %99s", a) == 1) {
    const ui::Page* p = !strcmp(a, "apps") ? &PAGE_APPS : !strcmp(a, "music") ? &PAGE_MUSIC : !strcmp(a, "timer") ? &PAGE_TIMER
                      : !strcmp(a, "stopwatch") ? &PAGE_STOPWATCH : !strcmp(a, "alarm") ? &PAGE_ALARM
                      : !strcmp(a, "radio") ? &PAGE_RADIO : !strcmp(a, "settings") ? &PAGE_SETTINGS : nullptr;
    if (p) ui::push(p);
  } else if (sscanf(line, "timer %99s", a) == 1) cw::timerStart(atoi(a));
  else if (sscanf(line, "face %99s", a) == 1) { cfg.face = atoi(a); settingsSave(); ui::home(); }
  else if (!strcmp(line, "wake")) ui::wake();
  else if (!strcmp(line, "perf")) perfLog = !perfLog;
  else if (!strcmp(line, "status")) {
    music::State s = music::state();
    Serial.printf("WLAN %s %s · Brücke %s %s · Musik %s %s %.0f/%u s · Akku %d%% · Heap %u\n", netOnline() ? "an" : "aus",
                  netOnline() ? netIp().c_str() : "", music::bridgeKnown() ? "ok" : "–", music::bridgeUrl(),
                  s.active ? (s.playing ? "spielt" : "Pause") : "aus", s.title, s.pos, s.dur, hwBattery(),
                  heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
  } else return false;
  return true;
}

void setup() {
  Serial.begin(115200);
  settingsLoad();
  hwBegin();
  audio::begin(cfg.volume);
  netBegin();
  radio::begin();
  music::begin();
  weather::begin();
  ui::begin();
  lv_timer_handler();
  hwBrightness(cfg.brightness);
  Serial.printf("Taschenfunk %s bereit (%s)\n", FW_VERSION, cfg.name);
}

static void power() {
  // Display aus nach der eingestellten Zeit ohne Bedienung
  if (hwScreenOn() && cfg.screenOff && hwIdleMs() > cfg.screenOff * 1000UL && !cw::ringing() && !radio::transmitting())
    hwScreen(false);
  // Tiefschlaf im Akkubetrieb, wenn nichts mehr läuft
  if (!hwScreenOn() && DEEP_SLEEP_MIN && !hwUsb() && hwBattery() >= 0 &&
      hwIdleMs() > (cfg.screenOff + DEEP_SLEEP_MIN * 60UL) * 1000UL && !music::state().active && !cw::busy() &&
      !radio::transmitting() && !radio::receiving()) {
    audio::shutdown();
    hwDeepSleep(cw::alarmInS());
  }
}

void loop() {
  hwLoop();
  BtnEvent b;
  while ((b = hwButton()) != BTN_NONE) {
    if (b == PWR_SHORT) {
      hwScreen(!hwScreenOn());
      if (hwScreenOn()) ui::wake();
      continue;
    }
    if (!hwScreenOn() && b == BOOT_SHORT) { hwScreen(true); ui::wake(); continue; }
    if (b != BOOT_HOLD && b != BOOT_RELEASE) hwActivity();
    ui::onButton(b);
  }
  if (hwWakeRequested()) { hwScreen(true); ui::wake(); }

  static uint32_t slow = 0;
  if (millis() - slow > 1000) {
    slow = millis();
    netPoll();
    power();
  }
  int v = music::takeRemoteVolume();
  if (v >= 0) { cfg.volume = v; audio::setVolume(v); settingsSave(); }

  cw::poll();
  ui::tick();
  uint32_t t0 = micros();
  uint32_t next = lv_timer_handler();
  uint32_t dt = micros() - t0;
  static uint32_t worst = 0, perfAt = 0;
  if (dt > worst) worst = dt;
  if (perfLog && millis() - perfAt > 2000) { perfAt = millis(); Serial.printf("UI: längster Durchlauf %u ms\n", worst / 1000); worst = 0; }
  delay(min<uint32_t>(next, 5));
}
