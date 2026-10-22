#include "clockwork.h"
#include "audio.h"
#include "net.h"
#include "settings.h"

namespace cw {

// Timer und Stoppuhr überstehen keinen Tiefschlaf; solange sie laufen, schläft das Gerät nicht.
static bool tRun = false, tPause = false;
static uint32_t tEnd = 0, tLeft = 0, tTotal = 0;

static bool sRun = false;
static uint32_t sStart = 0, sAcc = 0;
static uint32_t laps[30];
static int lapCount = 0;

static Ring ring = RING_NONE;
static uint32_t ringStart = 0, ringBeep = 0;
static bool quiet = false;
static time_t snoozeUntil = 0;
static int lastAlarmMin = -1;

void timerStart(uint32_t seconds) {
  tTotal = seconds;
  tEnd = millis() + seconds * 1000;
  tRun = true;
  tPause = false;
}
void timerPause() { if (tRun && !tPause) { tLeft = timerLeftMs(); tPause = true; } }
void timerResume() { if (tRun && tPause) { tEnd = millis() + tLeft; tPause = false; } }
void timerReset() { tRun = tPause = false; }
bool timerRunning() { return tRun; }
bool timerPaused() { return tPause; }
uint32_t timerTotalS() { return tTotal; }
uint32_t timerLeftMs() {
  if (!tRun) return 0;
  if (tPause) return tLeft;
  int32_t d = (int32_t)(tEnd - millis());
  return d > 0 ? d : 0;
}

void swStartStop() {
  if (sRun) { sAcc += millis() - sStart; sRun = false; }
  else { sStart = millis(); sRun = true; }
}
uint32_t swMs() { return sAcc + (sRun ? millis() - sStart : 0); }
bool swRunning() { return sRun; }
void swReset() { sRun = false; sAcc = 0; lapCount = 0; }
void swLap() {
  if (!sRun) return;
  uint32_t t = swMs();
  if (lapCount == 30) { memmove(laps, laps + 1, 29 * sizeof(uint32_t)); lapCount--; }
  laps[lapCount++] = t;
}
int swLaps(uint32_t* out, int max) {
  int n = min(max, lapCount);
  for (int i = 0; i < n; i++) {
    int k = lapCount - 1 - i;
    out[i] = laps[k] - (k ? laps[k - 1] : 0);
  }
  return n;
}

Ring ringing() { return ring; }
void ringQuiet(bool q) { quiet = q; }
void ringStop() { ring = RING_NONE; }
void snooze() {
  if (ring == RING_ALARM) snoozeUntil = time(nullptr) + 5 * 60;
  ring = RING_NONE;
}

uint32_t alarmInS() {
  if (!timeValid()) return 0;
  time_t now = time(nullptr);
  if (snoozeUntil > now) return snoozeUntil - now;
  if (!cfg.alarmOn) return 0;
  struct tm t;
  localtime_r(&now, &t);
  int nowS = t.tm_hour * 3600 + t.tm_min * 60 + t.tm_sec;
  int at = cfg.alarmH * 3600 + cfg.alarmM * 60;
  int d = at - nowS;
  if (d <= 0) d += 86400;
  return d;
}

static void startRing(Ring r) {
  ring = r;
  ringStart = millis();
  ringBeep = 0;
  quiet = false;
}

void poll() {
  if (tRun && !tPause && timerLeftMs() == 0) {
    tRun = false;
    startRing(RING_TIMER);
  }
  if (timeValid()) {
    time_t now = time(nullptr);
    struct tm t;
    localtime_r(&now, &t);
    int m = t.tm_hour * 60 + t.tm_min;
    if (cfg.alarmOn && t.tm_hour == cfg.alarmH && t.tm_min == cfg.alarmM && m != lastAlarmMin) {
      lastAlarmMin = m;
      startRing(RING_ALARM);
    }
    if (snoozeUntil && now >= snoozeUntil) { snoozeUntil = 0; startRing(RING_ALARM); }
  }
  if (ring != RING_NONE) {
    if (millis() - ringStart > (quiet ? 3600000UL : 90000UL)) ring = RING_NONE;  // Ton höchstens 90 s, Radio 1 Std.
    else if (!quiet && millis() - ringBeep > 1000) { ringBeep = millis(); audio::play(audio::FX_ALARM); }
  }
}

bool busy() { return (tRun && !tPause) || sRun || ring != RING_NONE; }

}  // namespace cw
