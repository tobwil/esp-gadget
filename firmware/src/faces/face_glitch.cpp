// Glitch: RGB-Splitter, verschobene Streifen, Blockartefakte und Scanlines. Im Original treibt der
// Stress das Glitchen; hier ist es die Musik – je lauter, desto wilder, ohne Musik nur selten ein Ruck.
// Übertragen aus FaceLab (glitch.js, Stil „RGB“).
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "../fonts/fonts.h"
#include "dc.h"

namespace fc {

static const float TY = 194, TH = 104, CX = 227;
static const uint32_t C0 = 0xff2a4a, C1 = 0x00e5ff, C2 = 0xffffff;

static void draw(Dc& dc, const Data& d, const Anim& a) {
  float p = a.intro;
  uint32_t t = a.t;
  // Glitch-Stärke: Musikpegel statt Stress
  float g = d.music ? clamp(d.level * 1.6f, 0, 1) : 0.1f;

  uint32_t slot = a.live ? t / 120 : 3;
  uint32_t seed = rnd(slot * 977 + 31);
  float burst = 0;
  if (a.live && rf(seed) < 0.03f + 0.3f * g) {
    seed = rnd(seed);
    burst = 0.35f + 0.65f * rf(seed);
  }
  float ib = powf(1 - p, 1.4f);
  float k = clamp(fmaxf(0, g - 0.12f) * 0.6f + burst * (0.3f + 0.7f * g) + ib, 0, 1);

  // ---- Kopfzeile und HUD-Ecken
  char b[48];
  float tp = ease(p * 1.5f - 0.3f);
  dc.setColor(dim(0x9a9a9a, tp));
  snprintf(b, sizeof(b), "%s %d %s // KW %d", d.dowStr, d.day, d.monStr, d.wk);
  dc.text(b, CX, 86, &font_text_20, 1);
  dc.setColor(dim(C0, 0.8f * tp));
  dc.fillRect(CX - 60, 112, 120 * tp, 2);
  dc.setPenWidth(2);
  dc.setColor(dim(0x4a4a4a, tp));
  for (int i = 0; i < 4; i++) {
    float bx = i % 2 == 0 ? 46 : 408, by = i < 2 ? 130 : 258;
    float sx = i % 2 == 0 ? 1 : -1, sy = i < 2 ? 1 : -1;
    dc.drawLine(bx, by, bx + 14 * sx, by);
    dc.drawLine(bx, by, bx, by + 14 * sy);
  }

  // ---- Uhrzeit mit RGB-Versatz und Scanlines
  char ts[8];
  snprintf(ts, sizeof(ts), "%02d:%02d", d.h, d.m);
  float o = k > 0.04f ? 1 + 12 * k : 0;
  float ytop = TY - TH / 2 - 12;
  if (o > 0) {
    dc.setColor(C0);
    st(dc, ts, CX - o, TY, TH, 9, 1, 1.0f);
    dc.setColor(C1);
    st(dc, ts, CX + o, TY + (k > 0.5f ? 2 : 0), TH, 9, 1, 1.0f);
    if (k > 0.15f) {
      dc.setPenWidth(1);
      dc.setColor(0);
      int spc = k > 0.75f ? 3 : 4;
      for (float y = ytop; y < ytop + TH + 24; y += spc) dc.drawLine(30, y, 424, y);
    }
  }
  dc.setColor(C2);
  st(dc, ts, CX, TY, TH, 9, 1, 1.0f);

  // ---- verschobene Streifen, Blockartefakte
  if (k > 0.08f) {
    float bh = (TH + 24) / 6.0f;
    uint32_t s2 = rnd(seed + 7);
    int shifted = 0, maxs = ib > 0.1f ? 3 : 2;
    for (int bi = 0; bi < 6 && shifted < maxs; bi++) {
      s2 = rnd(s2);
      if (rf(s2) > k * 0.85f) continue;
      s2 = rnd(s2);
      float dx = roundf((rf(s2) - 0.5f) * 2 * (6 + 30 * k));
      if (dx > -2 && dx < 2) continue;
      shifted++;
      float y0 = floorf(ytop + bi * bh);
      dc.clip(0, y0, 454, ceilf(bh));
      dc.setColor(0);
      dc.fillRect(0, y0, 454, ceilf(bh));
      dc.setColor(dx < 0 ? C0 : C1);
      st(dc, ts, CX + dx * 1.4f, TY, TH, 9, 1, 1.0f);
      dc.setColor(C2);
      st(dc, ts, CX + dx, TY, TH, 9, 1, 1.0f);
      dc.noClip();
    }
    int nb = (int)(k * 9);
    for (int i = 0; i < nb; i++) {
      s2 = rnd(s2); float bx = 60 + rf(s2) * 300;
      s2 = rnd(s2); float by = 120 + rf(s2) * 170;
      s2 = rnd(s2); float bw = 6 + rf(s2) * 46;
      float bhh = 3 + (s2 % 9);
      dc.setColor(i % 4 == 3 ? C2 : i % 2 == 1 ? C0 : C1);
      dc.fillRect(bx, by, bw, bhh);
    }
    if (k > 0.3f) {
      s2 = rnd(s2);
      dc.setColor(dim(C1, 0.5f));
      dc.fillRect(40, 130 + rf(s2) * 150, 374, 1);
    }
  }

  // ---- Statuszeile mit blinkendem Cursor
  const char* msg = g < 0.34f ? "SYS STABIL" : g < 0.67f ? "SYS UNRUHIG" : "SYS ÜBERLASTET";
  uint32_t scol = g < 0.34f ? 0x3cff9a : g < 0.67f ? 0xffc02e : 0xff3355;
  int len = strlen(msg);
  int nch = (int)(len * clamp(p * 1.6f - 0.4f, 0, 1));
  if (nch > 0) {
    uint32_t s3 = rnd(seed + 99);
    char out[24];
    int j = 0;
    for (int i = 0; i < nch && j < 22; i++) {
      s3 = rnd(s3);
      bool garble = k > 0.25f && rf(s3) < k * 0.3f && (uint8_t)msg[i] < 0x80;
      out[j++] = garble ? '#' : msg[i];
      if ((uint8_t)msg[i] >= 0xC0 && i + 1 < len) out[j++] = msg[++i];  // Umlaut (2 Bytes)
    }
    out[j] = 0;
    float jx = burst > 0 ? roundf((rf(s3) - 0.5f) * 10) : 0;
    dc.setColor(scol);
    float wq = dc.text(out, CX + jx, 270, &font_text_24, 1);
    if (!a.live || (t / 500) % 2 == 0) dc.fillRect(CX + jx + wq / 2 + 6, 268, 12, 21);
  }

  // ---- Datenzeilen
  for (int i = 0; i < 3; i++) {
    float sp = stag(p, i, 0.1f, 0.45f);
    if (sp <= 0 || !d.slots[i].lbl[0]) continue;
    float y = 312 + i * 24, x = CX - 100;
    dc.setColor(dim(C0, sp));
    dc.text(">", x, y, &font_text_20, 0);
    dc.setColor(dim(0x8a8a8a, sp));
    dc.text(d.slots[i].lbl, x + 18, y, &font_text_20, 0);
    dc.setColor(dim(d.dcol, sp));
    dc.text(d.slots[i].val, CX + 100, y, &font_text_20, 2);
  }
}

const Face FACE_GLITCH = {"Glitch", "Zerfällt im Takt der Musik", 12, draw};

}  // namespace fc
