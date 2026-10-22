// Sternenhimmel: der echte Himmel über dem Standort, jetzt – helle Sterne, Sternbilder, Milchstraße,
// Sonne und Mond (mit Phase) in einer zenitzentrierten stereografischen Projektion (Norden oben, Osten
// links, wie auf dem Rücken liegend). Tagsüber blauer Himmel mit Sonne, die Sterne nur schwach.
// Übertragen aus FaceLab (sterne.js, Stil „Planetarium“).
#include <math.h>
#include <stdio.h>
#include "../fonts/fonts.h"
#include "dc.h"
#include "starsky.h"

namespace fc {

static const float RAD = 0.01745329f, CX = 227, R = 196;
static const int NS = sizeof(SK_ST) / sizeof(SK_ST[0]);
static const int NM = sizeof(SK_MW) / sizeof(SK_MW[0]);
static const int NL = sizeof(SK_LN) / sizeof(SK_LN[0]);
static const int NCN = sizeof(SK_CN) / sizeof(SK_CN[0]);
static const uint32_t STAR_COL[4] = {0xcfe0ff, 0xffffff, 0xffe0a8, 0xffa486};

static float MAG[NS], SDE[NS], CDE[NS];
static bool decoded = false;
static long KC = -1;
static float SX[NS], SY[NS], SU[NS], WX[NM], WY[NM], WU[NM];
static float SUNX, SUNY, SUNU, MOX, MOY, MOU, EL, LST;
static float sp_ = 0, cp_ = 1, px_, py_, pu_;
static uint32_t SPAL[64];
static long SPB = -1;
static int SCI[NS];
static float SFF[NS];
static uint32_t MWC[NM * 2];
static int NLB = 0;
static float LBX[3], LBY[3], LBW[3];
static int LBI[3];
static long LK = -2;
static float RX[NS], RY[NS];

static float wrap(float v) { return v - 360 * floorf(v / 360); }

static void projV(float e, float n, float u) {
  pu_ = u;
  float k = R / (u > -0.95f ? 1 + u : 0.05f);
  px_ = CX - k * e;
  py_ = CX - k * n;
}

static void proj(float h, float sd, float cd) {
  float sh = sinf(h * RAD), ch = cosf(h * RAD);
  projV(-cd * sh, cp_ * sd - sp_ * cd * ch, sp_ * sd + cp_ * cd * ch);
}

static void projEcl(float lam, float bet, float lst) {
  float ep = 23.439f * RAD;
  float sl = sinf(lam * RAD), cl = cosf(lam * RAD), sb = sinf(bet * RAD), cb = cosf(bet * RAD);
  float ra = atan2f(sl * cosf(ep) - (sb / cb) * sinf(ep), cl) / RAD;
  float de = asinf(sb * cosf(ep) + cb * sinf(ep) * sl);
  proj(lst - ra, sinf(de), cosf(de));
}

// Himmel für die aktuelle Minute (einmal pro Minute komplett neu berechnet)
static void sky(const Data& d) {
  if (!decoded) {
    for (int i = 0; i < NS; i++) {
      uint32_t v = SK_ST[i];
      MAG[i] = ((v >> 2) & 127) / 10.0f - 2;
      float de = (((v >> 9) & 2047) / 10.0f - 90) * RAD;
      SDE[i] = sinf(de);
      CDE[i] = cosf(de);
    }
    decoded = true;
  }
  long key = ((long)(d.year % 10) * 400 + d.doy) * 1440 + d.mins;
  if (key == KC) return;
  KC = key;
  sp_ = sinf(d.lat * RAD);
  cp_ = cosf(d.lat * RAD);
  double sec = (double)d.unix - 946728000.0;
  double nd = floor(sec / 86400);
  double fr = (sec - nd * 86400) / 86400.0;
  LST = wrap((float)fmod(280.46061837 + 0.98564736629 * nd + 360.98564736629 * fr + d.lon, 360.0));
  for (int i = 0; i < NM; i++) {
    uint32_t v = SK_MW[i];
    float de = (((v >> 9) & 2047) / 10.0f - 90) * RAD;
    proj(LST - (v >> 20) * 0.375f, sinf(de), cosf(de));
    WX[i] = px_; WY[i] = py_; WU[i] = pu_;
  }
  float n = (float)(nd + fr);
  float g = wrap(357.528f + 0.9856003f * n);
  float ls = wrap(280.46f + 0.9856474f * n) + 1.915f * sinf(g * RAD) + 0.02f * sinf(2 * g * RAD);
  projEcl(ls, 0, LST);
  SUNX = px_; SUNY = py_; SUNU = pu_;
  EL = asinf(clamp(pu_, -1, 1)) / RAD;
  float l0 = wrap(218.316f + 13.176396f * n), mm = wrap(134.963f + 13.064993f * n), ff = wrap(93.272f + 13.22935f * n);
  projEcl(l0 + 6.289f * sinf(mm * RAD), 5.128f * sinf(ff * RAD), LST);
  MOX = px_; MOY = py_; MOU = pu_;
  for (int i = 0; i < NS; i++) {
    float h = (LST - (SK_ST[i] >> 20) * 0.375f) * RAD;
    float sh = sinf(h), ch = cosf(h), sd = SDE[i], cd = CDE[i];
    float e = -cd * sh, nn = cp_ * sd - sp_ * cd * ch, u = sp_ * sd + cp_ * cd * ch;
    float kk = R / (u > -0.95f ? 1 + u : 0.05f);
    SX[i] = CX - kk * e;
    SY[i] = CX - kk * nn;
    SU[i] = u;
  }
  SPB = -1;  // Sternfarben neu
  LK = -2;   // Beschriftung neu
}

static void skyCols(float el, uint32_t& zen, uint32_t& hor) {
  const uint32_t zn = 0x03050d, hn = 0x0b1730, zt = 0x0c1840, ht = 0x3a4f86, zd = 0x1f56a6, hd = 0x7cb2e2;
  if (el >= 4) { zen = zd; hor = hd; return; }
  if (el >= -6) { float f = (el + 6) / 10.0f; zen = mix(zt, zd, f); hor = mix(ht, hd, f); return; }
  if (el >= -18) { float f = (el + 18) / 12.0f; zen = mix(zn, zt, f); hor = mix(hn, ht, f); return; }
  zen = zn; hor = hn;
}

static void moon(Dc& dc, float x, float y, float r, float ph, uint32_t lit, uint32_t dark) {
  dc.setColor(dark);
  dc.fillCircle(x, y, r);
  float il = illum(ph);
  if (il < 0.03f) return;
  float ux = SUNX - MOX, uy = SUNY - MOY, l = sqrtf(ux * ux + uy * uy);
  if (l < 0.5f) { ux = 1; uy = 0; } else { ux /= l; uy /= l; }
  dc.setColor(lit);
  if (il > 0.97f) { dc.fillCircle(x, y, r); return; }
  float c = cosf(ph * 6.2831853f), vx = -uy, vy = ux;
  float a[18], b[18];
  for (int i = 0; i <= 8; i++) {
    float t = (-90 + i * 22.5f) * RAD, ca = r * cosf(t), sb = r * sinf(t);
    a[2 * i] = x + ux * ca + vx * sb;
    a[2 * i + 1] = y + uy * ca + vy * sb;
    b[2 * i] = x + ux * ca * c + vx * sb;
    b[2 * i + 1] = y + uy * ca * c + vy * sb;
  }
  dc.fillStrip(a, b, 9);
}

static bool under(float x, float y, float tw) {
  if (y > 136 && y < 262 && fabsf(x - CX) < tw) return true;
  return y >= 262 && y < 318 && x > 72 && x < 382;
}

static void star(Dc& dc, float x, float y, float mag, int pi) {
  int b = pi - (pi % 16), q = pi % 16;
  if (mag < 0.6f) {
    dc.setColor(SPAL[b + (int)(q * 0.22f + 0.5f)]);
    dc.fillCircle(x, y, 7);
    dc.setColor(SPAL[b + (int)(q * 0.5f + 0.5f)]);
    dc.fillRect(x - 9, y, 19, 1);
    dc.fillRect(x, y - 9, 1, 19);
    dc.setColor(SPAL[pi]);
    dc.fillCircle(x, y, 3.2f);
  } else {
    dc.setColor(SPAL[b + (int)(q * 0.4f + 0.5f)]);
    dc.fillRect(x - 4, y, 9, 1);
    dc.fillRect(x, y - 4, 1, 9);
    dc.setColor(SPAL[pi]);
    dc.fillCircle(x, y, 2.4f);
  }
}

static void labels(Dc& dc, float tw) {
  NLB = 0;
  for (int c = 0; c < NCN && NLB < 3; c++) {
    if (!SK_CN[c][0]) continue;
    int s0 = SK_CS[c * 2], n = SK_CS[c * 2 + 1];
    float sx = 0, sy = 0;
    bool ok = true;
    for (int k = s0; k < s0 + n; k++) {
      if (SU[k] <= 0.05f) { ok = false; break; }
      sx += SX[k];
      if (SY[k] > sy) sy = SY[k];
    }
    if (!ok) continue;
    sx /= n;
    sy += 7;
    if (c == 1) { sx = SX[7]; sy = SY[7] + 7; }
    float lw = dc.textW(SK_CN[c], &font_text_20) / 2;
    float ddx = fabsf(sx - CX) + lw, ddy = fabsf(sy + 7 - CX) + 7;
    if (ddx * ddx + ddy * ddy > (R - 4) * (R - 4)) continue;
    if (under(sx - lw, sy, tw) || under(sx + lw, sy, tw) || under(sx - lw, sy + 14, tw) || under(sx + lw, sy + 14, tw)) continue;
    bool hit = false;
    for (int q = 0; q < NLB; q++)
      if (fabsf(LBX[q] - sx) < LBW[q] + lw + 6 && fabsf(LBY[q] - sy) < 18) hit = true;
    if (hit) continue;
    LBX[NLB] = sx; LBY[NLB] = sy; LBW[NLB] = lw; LBI[NLB] = c;
    NLB++;
  }
}

static void draw(Dc& dc, const Data& d, const Anim& a) {
  uint32_t acc = d.accent;
  float p = a.intro;
  sky(d);
  float dark = clamp((-EL - 2) / 12.0f, 0, 1);
  uint32_t zen, hor;
  skyCols(EL, zen, hor);
  uint32_t bg = mix(hor, zen, 0.5f);
  uint32_t t = a.live ? a.t : 0;
  float e = ease(p);
  bool rest = p >= 1;
  char ts[8];
  snprintf(ts, sizeof(ts), "%02d:%02d", d.h, d.m);
  const float th = 84;
  float tw = stW(ts, th) / 2 + 14;
  float vis = 0.16f + 0.84f * dark;

  // ---- Farbtabellen, wenn sich die Himmelsfarbe ändert
  if (SPB != (long)bg) {
    SPB = bg;
    for (int c = 0; c < 4; c++)
      for (int q = 0; q < 16; q++) SPAL[c * 16 + q] = mix(bg, STAR_COL[c], q / 15.0f);
    for (int i = 0; i < NM; i++) {
      float w = ((SK_MW[i] & 511) + (SK_MW[(i + 1) % NM] & 511)) / 200.0f;
      MWC[i] = mix(bg, 0x2a3658, 0.45f * w * dark);
      MWC[NM + i] = mix(bg, 0x2a3658, 0.7f * w * dark);
    }
    for (int i = 0; i < NS; i++) {
      float f = 0;
      if (SU[i] > 0) {
        f = vis * clamp(1.25f - MAG[i] * 0.13f, 0.45f, 1);
        if (SU[i] < 0.12f) f *= 0.45f + SU[i] * 4.5f;
        if (under(SX[i], SY[i], tw)) f *= 0.55f;
      }
      SFF[i] = f;
      int q = (int)(f * 15 + 0.5f);
      SCI[i] = q > 0 ? (SK_ST[i] & 3) * 16 + q : -1;
    }
  }
  if (rest && LK != KC) { LK = KC; labels(dc, tw); }

  // ---- Intro: die Kuppel dreht sich an ihren Platz
  float rot = -50 * (1 - e), cr = cosf(rot * RAD), sr = sinf(rot * RAD);
  const float* rx = rest ? SX : RX;
  const float* ry = rest ? SY : RY;
  if (!rest)
    for (int i = 0; i < NS; i++) {
      if (SU[i] <= 0) continue;
      float dx = SX[i] - CX, dy = SY[i] - CX;
      RX[i] = CX + dx * cr - dy * sr;
      RY[i] = CX + dx * sr + dy * cr;
    }

  // ---- Hintergrund: radialer Verlauf in 10 Ringen, Horizont → Zenit
  float rw = R / 10.0f;
  dc.setPenWidth(rw + 2);
  for (int k = 0; k < 10; k++) {
    dc.setColor(mix(hor, zen, powf((k + 0.5f) / 10.0f, 0.6f)));
    if (k < 9) dc.drawCircle(CX, CX, R - rw * (k + 0.5f));
    else dc.fillCircle(CX, CX, rw + 1);
  }
  // Dämmerungsschein in Richtung Sonne
  float gl = 1 - fabsf(EL + 3) / 10.0f;
  if (gl > 0) {
    float gx = SUNX - CX, gy = SUNY - CX, gr = sqrtf(gx * gx + gy * gy);
    if (gr > R * 1.15f) { gx = gx * R * 1.15f / gr; gy = gy * R * 1.15f / gr; }
    for (int k = 0; k < 9; k++) {
      dc.setColor(mix(hor, k > 5 ? 0xffd08a : 0xf0905a, gl * (0.08f + k * 0.055f)));
      dc.fillCircle(CX + gx, CX + gy, 160 - k * 16);
    }
  }

  // ---- Milchstraße
  if (dark > 0.05f) {
    for (int pass = 0; pass < 2; pass++) {
      dc.setPenWidth(pass == 0 ? 30 : 12);
      for (int i = 0; i < NM; i++) {
        int j = i + 1 < NM ? i + 1 : 0;
        if (WU[i] < -0.2f || WU[j] < -0.2f) continue;
        dc.setColor(MWC[pass * NM + i]);
        float ax = WX[i] - CX, ay = WY[i] - CX, bx = WX[j] - CX, by = WY[j] - CX;
        dc.drawLine(CX + ax * cr - ay * sr, CX + ax * sr + ay * cr, CX + bx * cr - by * sr, CX + bx * sr + by * cr, true);
      }
    }
  }

  // ---- Höhenkreise 30° / 60°
  dc.setPenWidth(1);
  dc.setColor(mix(bg, acc, 0.16f));
  dc.drawCircle(CX, CX, R * 0.577f);
  dc.drawCircle(CX, CX, R * 0.268f);

  // ---- Sternbilder (zeichnen sich nach den Sternen)
  float lp = clamp((p - 0.45f) / 0.5f, 0, 1);
  if (lp > 0) {
    dc.setColor(mix(bg, acc, 0.18f + 0.22f * dark));
    dc.setPenWidth(1);
    for (int i = 0; i < NL; i++) {
      int sa = SK_LN[i] >> 8, sb = SK_LN[i] & 255;
      if (SU[sa] <= 0 || SU[sb] <= 0) continue;
      dc.drawLine(rx[sa], ry[sa], rx[sa] + (rx[sb] - rx[sa]) * lp, ry[sa] + (ry[sb] - ry[sa]) * lp);
    }
  }

  // ---- Sterne (im Intro die hellsten zuerst; die hellen funkeln)
  float thr = -1.5f + 7.5f * ease(p * 1.3f);
  bool twk = a.live && rest;
  for (int i = 0; i < NS; i++) {
    int pi = SCI[i];
    if (pi < 0 || SU[i] <= 0) continue;
    float mag = MAG[i];
    if (!rest) {
      if (mag > thr) continue;
      int q = (int)(SFF[i] * clamp(thr - mag, 0, 1) * 15 + 0.5f);
      if (q <= 0) continue;
      pi = (SK_ST[i] & 3) * 16 + q;
    } else if (twk && mag < 2) {
      pi = (SK_ST[i] & 3) * 16 + (int)(SFF[i] * (0.8f + 0.2f * noise(t * 0.004f + i * 1.37f)) * 15 + 0.5f);
    }
    if (mag < 1.6f) star(dc, rx[i], ry[i], mag, pi);
    else {
      dc.setColor(SPAL[pi]);
      float s = mag < 2.6f ? 3 : 2;
      dc.fillRect(rx[i] - 1, ry[i] - 1, s, s);
    }
  }

  // ---- Sternbildnamen
  if (rest && dark > 0.4f) {
    dc.setColor(mix(bg, acc, 0.3f + 0.25f * dark));
    for (int q = 0; q < NLB; q++) dc.text(SK_CN[LBI[q]], LBX[q], LBY[q], &font_text_20, 1);
  }

  // ---- Sonne und Mond
  float pop = spring(stag(p, 0, 0.3f, 0.5f));
  if (SUNU > -0.02f && pop > 0) {
    dc.setColor(mix(bg, 0xfff0c0, 0.25f)); dc.fillCircle(SUNX, SUNY, 22 * pop);
    dc.setColor(mix(bg, 0xfff0c0, 0.5f)); dc.fillCircle(SUNX, SUNY, 15 * pop);
    dc.setColor(0xfffbe8); dc.fillCircle(SUNX, SUNY, 10 * pop);
  }
  float mx = MOX, my = MOY;
  if (rot != 0) { float dx = mx - CX, dy = my - CX; mx = CX + dx * cr - dy * sr; my = CX + dx * sr + dy * cr; }
  if (MOU > -0.02f && pop > 0) {
    dc.setColor(mix(bg, 0xe8ecff, 0.12f));
    dc.fillCircle(mx, my, 17 * pop);
    moon(dc, mx, my, 11 * pop, d.moon, 0xf4f0dc, mix(bg, 0x39405a, 0.6f));
  }

  // ---- Rand mit Kompass
  const uint32_t rim = 0x04060c;
  dc.setColor(rim);
  dc.setPenWidth(36);
  dc.drawCircle(CX, CX, R + 18);
  uint32_t ink = mix(rim, acc, 0.55f);
  dc.setColor(ink);
  dc.setPenWidth(1);
  dc.drawCircle(CX, CX, R + 1);
  for (int k = 0; k < 36; k++) {
    if (k % 9 == 0) continue;
    tick(dc, CX, CX, R + 3, R + (k % 3 == 0 ? 11 : 6), k * 10);
  }
  float lr = R + 19;
  dc.setColor(acc);
  st(dc, "N", CX, CX - lr, 15, 2, 1, 1.0f);
  dc.setColor(ink);
  st(dc, "S", CX, CX + lr, 15, 2, 1, 1.0f);
  st(dc, "O", CX - lr, CX, 15, 2, 1, 1.0f);
  st(dc, "W", CX + lr, CX, 15, 2, 1, 1.0f);

  // ---- Text
  bool day = EL > 0;
  char b[40];
  dc.setColor(day ? 0xffffff : acc);
  snprintf(b, sizeof(b), "%s %d. %s", d.dowStr, d.day, d.monStr);
  st(dc, b, CX, 150, 17, 2, 1, ease(p * 1.6f));
  dc.setColor(0xffffff);
  st(dc, ts, CX, 206, th, 7, 1, easeInOut(p * 1.15f));
  for (int k = 0; k < 3; k++) {
    float s2 = stag(p, k, 0.12f, 0.5f);
    if (s2 <= 0) continue;
    float x = CX + (k - 1) * 100;
    dc.setColor(mix(bg, day ? 0xe0ecff : 0x8a93a8, s2));
    dc.text(d.slots[k].lbl, x, 268, &font_text_20, 1);
    dc.setColor(mix(bg, d.dcol, s2));
    st(dc, d.slots[k].val, x, 298, 20, 2, 1, 1.0f);
  }
}

const Face FACE_STERNE = {"Sternenhimmel", "Der echte Himmel über dir", 1, draw, true};

}  // namespace fc
