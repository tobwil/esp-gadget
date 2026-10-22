// Kiesel Text: die Pebble Text Watch auf Deutsch – „zehn / zweiund / vierzig“, die Stunde fett.
// Beim Aufwachen und bei jedem Minutenwechsel gleiten die Zeilen nacheinander von rechts herein.
// Oben Wetter und Akku-Balken, unten das Datum. Übertragen aus FaceLab (ktext.js, Stil „Schwarz“).
#include <stdio.h>
#include <string.h>
#include "../fonts/fonts.h"
#include "dc.h"

namespace fc {

static const char* HW[] = {"zwölf", "eins", "zwei", "drei", "vier", "fünf", "sechs", "sieben", "acht", "neun", "zehn", "elf"};
static const char* ONES[] = {"", "eins", "zwei", "drei", "vier", "fünf", "sechs", "sieben", "acht", "neun"};
static const char* PRE[] = {"", "ein", "zwei", "drei", "vier", "fünf", "sechs", "sieben", "acht", "neun"};
static const char* TEEN[] = {"zehn", "elf", "zwölf", "dreizehn", "vierzehn", "fünfzehn", "sechzehn", "siebzehn", "achtzehn", "neunzehn"};
static const char* TENS[] = {"", "", "zwanzig", "dreißig", "vierzig", "fünfzig"};
static const char* DOW[] = {"So", "Mo", "Di", "Mi", "Do", "Fr", "Sa"};
static const float X0 = 78;

static int words(int h, int m, char L[3][24]) {
  int n = 0;
  snprintf(L[n++], 24, "%s", HW[h % 12]);
  if (m == 0) snprintf(L[n++], 24, "uhr");
  else if (m < 10) { snprintf(L[n++], 24, "null"); snprintf(L[n++], 24, "%s", ONES[m]); }
  else if (m < 20) snprintf(L[n++], 24, "%s", TEEN[m - 10]);
  else if (m % 10 == 0) snprintf(L[n++], 24, "%s", TENS[m / 10]);
  else { snprintf(L[n++], 24, "%sund", PRE[m % 10]); snprintf(L[n++], 24, "%s", TENS[m / 10]); }
  return n;
}

static int lastMin = -1;
static uint32_t minAt = 0;

static void draw(Dc& dc, const Data& d, const Anim& a) {
  // Minutenwechsel: Zeilen gleiten neu herein (wie beim Original)
  if (lastMin != d.m) {
    if (lastMin >= 0 && a.intro >= 1) minAt = a.t;
    lastMin = d.m;
  }
  float p = a.intro;
  if (minAt && a.t >= minAt) p = clamp((a.t - minAt) / 900.0f, 0, 1);
  if (a.intro < 1) minAt = 0;

  char b[48];
  dc.setColor(0xaaaaaa);
  if (d.batt >= 0) dc.fillRect(187, 60, 80 * clamp(d.batt / 100.0f, 0, 1), 4);
  if (d.slots[0].val[0]) {
    snprintf(b, sizeof(b), "%s %s", d.slots[0].val, d.slots[0].lbl);
    dc.text(b, 227, 76, &font_text_20, 1);
  }
  snprintf(b, sizeof(b), "%s %d.%d.", DOW[d.dow], d.day, d.mon);
  dc.text(b, 227, 372, &font_text_20, 1);
  battery(dc, d, 404, 0x777777);

  char L[3][24];
  int n = words(d.h, d.m, L);
  float y0 = 227 - (n * 72 - 16) / 2.0f;
  dc.setColor(0xffffff);
  for (int i = 0; i < n; i++) {
    float x = X0 + (1 - easeInOut(stag(p, i, 0.14f, 0.5f))) * 420;
    float y = y0 + i * 72;
    if (i == 0) dc.text(L[i], x, y + 8, &font_word_bold_64, 0);
    else dc.text(L[i], x, y + 14, &font_word_light_54, 0);
  }
}

const Face FACE_KTEXT = {"Kiesel Text", "Die Uhrzeit in Worten", 1, draw};

}  // namespace fc
