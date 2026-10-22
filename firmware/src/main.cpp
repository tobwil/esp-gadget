#include <math.h>
// Taschenfunk – YT-Music-Player, Taschenuhr, Timer und Walkie-Talkie
// für das Waveshare ESP32-S3-Touch-AMOLED-1.75C.
//
// Bedienung:
//   Uhr: antippen oder nach oben wischen = Apps · nach links wischen = Musik
//   überall: nach rechts wischen = zurück
//   BOOT kurz = zurück (auf der Uhr: Musik Pause/Weiter) · BOOT halten = Funk an alle (Push-to-Talk)
//   PWR kurz = Display an/aus · PWR lang = Ausschalten/Neustart
#include <Arduino.h>
#include <mbedtls/platform.h>
#include "audio.h"
#include "clockwork.h"
#include "config.h"
#include "pins.h"
#include "hw.h"
#include "music.h"
#include "net.h"
#include "radio.h"
#include "settings.h"
#include "weather.h"
#include "voice.h"
#include "keys.h"
#include "webui.h"
#include "imu.h"
#include "kids.h"
#include "power.h"
#include "phone.h"
#include "routines.h"
#include "assist.h"
#include "ui/ui.h"
extern const ui::Page PAGE_WEB_;

// Kennung des Boards in der Firmware: Updates per WLAN prüfen sie (webui.cpp)
extern const char TF_BOARD_TAG[];
const char TF_BOARD_TAG[] = "TFBOARD=" BOARD_NAME ";";

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
  } else if (sscanf(line, "browse %99[^\n]", a) == 1) {
    browseOpen(a);
  } else if (!strcmp(line, "toggle")) music::toggle();
  else if (!strcmp(line, "next")) music::next();
  else if (sscanf(line, "vol %99s", a) == 1) { cfg.volume = atoi(a); audio::setVolume(cfg.volume); music::volumeChanged(cfg.volume); }
  else if (sscanf(line, "fx %99s", a) == 1) audio::play((audio::Fx)atoi(a));
  else if (!strcmp(line, "home")) ui::home();
  else if (sscanf(line, "page %99s", a) == 1) {
    const ui::Page* p = !strcmp(a, "apps") ? &PAGE_APPS : !strcmp(a, "music") ? &PAGE_MUSIC : !strcmp(a, "timer") ? &PAGE_TIMER
                      : !strcmp(a, "stopwatch") ? &PAGE_STOPWATCH : !strcmp(a, "alarm") ? &PAGE_ALARM
                      : !strcmp(a, "radio") ? &PAGE_RADIO : !strcmp(a, "settings") ? &PAGE_SETTINGS : !strcmp(a, "game") ? &PAGE_GAME : !strcmp(a, "web") ? &PAGE_WEB_ : !strcmp(a, "assist") ? &PAGE_ASSIST : nullptr;
    if (p) ui::push(p);
  } else if (sscanf(line, "timer %99s", a) == 1) cw::timerStart(atoi(a));
  else if (sscanf(line, "face %99s", a) == 1) { cfg.face = atoi(a); settingsSave(); ui::home(); }
  else if (!strcmp(line, "wake")) ui::wake();
  else if (!strcmp(line, "perf")) perfLog = !perfLog;
  else if (sscanf(line, "kids %99s", a) == 1) { kids::Cfg k = kids::cfg(); k.on = !strcmp(a, "on"); kids::set(k); ui::home(); }
  else if (!strcmp(line, "kb")) ui::keyboard("Sender suchen", "", false, nullptr);
  else if (!strcmp(line, "voice start")) voice::start();  // Test: Spracheingabe ohne Finger
  else if (!strcmp(line, "voice stop")) voice::finish();
  else if (sscanf(line, "angry %99s", a) == 1) { kids::Cfg k = kids::cfg(); k.angry = !strcmp(a, "on"); if (k.angry) { k.on = true; music::stop(); } kids::set(k); ui::home(); }
  else if (sscanf(line, "shake %99s", a) == 1) angryTestShake = atof(a);
  else if (sscanf(line, "level %99s", a) == 1) gameTestLevel(atoi(a));
  else if (!strcmp(line, "talk on")) assist::talk(true);  // Test: Assistent ohne Finger
  else if (!strcmp(line, "talk off")) assist::talk(false);
  else if (sscanf(line, "sleep %99s", a) == 1) music::sleepSet(atoi(a));  // Test: Sleep-Timer in Minuten
  else if (!strncmp(line, "alarmsrc ", 9)) {
    snprintf(a, sizeof(a), "%s", line + 9);  // Test: „alarmsrc ard:set:10815223 Name“, „-“ = Ton
    char* sp = strchr(a, ' ');
    if (sp) *sp++ = 0;
    snprintf(cfg.alarmSrc, sizeof(cfg.alarmSrc), "%s", strcmp(a, "-") ? a : "");
    snprintf(cfg.alarmName, sizeof(cfg.alarmName), "%s", sp ? sp : "Test");
    settingsSave();
  }
  else if (sscanf(line, "alarm %99s", a) == 1) {  // Test: Wecker stellen, z. B. „alarm 7:30“
    int h = 0, m = 0;
    if (sscanf(a, "%d:%d", &h, &m) == 2) { cfg.alarmH = h; cfg.alarmM = m; cfg.alarmOn = true; settingsSave(); }
    else if (!strcmp(a, "off")) { cfg.alarmOn = false; settingsSave(); }
  }
  else if (sscanf(line, "kidtime %99s", a) == 1) { int h = 0, m = 0; kidFaceTestMin = sscanf(a, "%d:%d", &h, &m) == 2 ? h * 60 + m : -1; }
  else if (!strcmp(line, "imu")) { imu::sleep(false); delay(50); float x, y; imu::gravity(x, y); Serial.printf("Neigung: x %.2f y %.2f\n", x, y); }
  else if (!strcmp(line, "status")) {
    music::State s = music::state();
    Serial.printf("WLAN %s %s · Musik %s %s %.0f/%u s %s · Akku %d%% · Heap %u\n", netOnline() ? "an" : "aus",
                  netOnline() ? netIp().c_str() : "", s.active ? (s.playing ? "spielt" : "Pause") : "aus", s.title, s.pos, s.dur, s.msg,
                  hwBattery(), heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
  } else return false;
  return true;
}

// TLS-Puffer (je Verbindung 20–40 KB) ins PSRAM: sonst reicht der interne RAM nicht für Stream, Cover,
// Gemini und n8n gleichzeitig
static void* tlsCalloc(size_t n, size_t size) {
  void* p = heap_caps_calloc(n, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  return p ? p : heap_caps_calloc(n, size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
}

void setup() {
  Serial.begin(115200);
  mbedtls_platform_set_calloc_free(tlsCalloc, free);
  settingsLoad();
  kids::begin();
  cfg.volume = kids::limitVolume(cfg.volume);
  keys::begin();
  hwBegin();
  audio::begin(cfg.volume);
  imu::begin();
  netBegin();
  radio::begin();
  music::begin();
  weather::begin();
  voice::begin();
  assist::begin();
  pwr::begin();
  routines::begin();
  webui::begin();
  ui::begin();
  lv_timer_handler();
  hwBrightness(cfg.brightness);
  Serial.printf("Taschenfunk %s bereit (%s)\n", FW_VERSION, cfg.name);
}

// Radiowecker: statt zu piepen einen Sender bzw. die neueste Folge spielen. Kommt nach 20 s kein Ton
// (kein WLAN, Sender weg), piept der Wecker doch. Stopp, Schlummern oder Zeitablauf beenden die Wiedergabe.
static void radioAlarm() {
  static cw::Ring last = cw::RING_NONE;
  static uint32_t at = 0;
  static bool on = false, heard = false;
  cw::Ring r = cw::ringing();
  if (r == cw::RING_ALARM && last != cw::RING_ALARM && cfg.alarmSrc[0] && netOnline()) {
    cw::ringQuiet(true);
    music::wake(cfg.alarmSrc, cfg.alarmName);
    at = millis();
    on = true;
    heard = false;
  }
  if (on && music::state().pos >= 1) heard = true;
  if (on && heard && r == cw::RING_ALARM && !music::state().active) { cw::ringStop(); on = false; }  // Folge zu Ende
  if (on && r == cw::RING_ALARM && millis() - at > 20000 && music::state().pos < 1) {
    cw::ringQuiet(false);  // kein Ton angekommen: klassisch klingeln
    music::stop();
    on = false;
  }
  if (on && r != cw::RING_ALARM) { music::stop(); on = false; }
  last = r;
}

static void power() {
  // Display aus nach der eingestellten Zeit ohne Bedienung
  uint32_t off = pwr::screenOffS();
  if (hwScreenOn() && off && hwIdleMs() > off * 1000UL && !cw::ringing() && !radio::transmitting())
    hwScreen(false);
  // Tiefschlaf im Akkubetrieb, wenn nichts mehr läuft
  if (!hwScreenOn() && pwr::deepSleepMin() && !hwUsb() && hwBattery() >= 0 &&
      hwIdleMs() > (off + pwr::deepSleepMin() * 60UL) * 1000UL && !music::state().active && !cw::busy() &&
      !radio::transmitting() && !radio::receiving() && !kids::angry()) {
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
  // Wutmodus: Schütteln weckt das Display
  static uint32_t shakeAt = 0;
  if (kids::angry() && !hwScreenOn() && millis() - shakeAt > 40) {
    shakeAt = millis();
    static float lx = 0, ly = 0, lz = 1;
    float x, y, z;
    if (imu::accel(x, y, z)) {
      float d = fabsf(x - lx) + fabsf(y - ly) + fabsf(z - lz);
      lx = x; ly = y; lz = z;
      if (d > 1.2f) { hwScreen(true); hwActivity(); ui::wake(); }
    }
  }

  static uint32_t slow = 0;
  if (millis() - slow > 1000) {
    slow = millis();
    netPoll();
    power();
  }
  music::poll();
  kids::poll();
  pwr::poll();
  phone::poll();
  webui::poll();

  cw::poll();
  radioAlarm();
  ui::tick();
  uint32_t t0 = micros();
  uint32_t next = lv_timer_handler();
  uint32_t dt = micros() - t0;
  static uint32_t worst = 0, perfAt = 0;
  if (dt > worst) worst = dt;
  if (perfLog && millis() - perfAt > 2000) { perfAt = millis(); Serial.printf("UI: längster Durchlauf %u ms\n", worst / 1000); worst = 0; }
  delay(min<uint32_t>(next, 5));
}
